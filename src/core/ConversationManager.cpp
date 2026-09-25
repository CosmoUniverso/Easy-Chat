#include "core/ConversationManager.h"
#include "core/PeerManager.h"
#include "core/Protocol.h"
#include "crypto/CryptoEngine.h"
#include "storage/Database.h"
#include "transport/TransportManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <QtGlobal>
#include <algorithm>

namespace ec {
namespace {
constexpr int MeshTtl = 4;
constexpr qint64 SeenPacketLifetimeMs = 2 * 60 * 1000;
constexpr qint64 OutboxLifetimeMs = 7LL * 24 * 60 * 60 * 1000;
constexpr qint64 RelaySpoolLifetimeMs = 24LL * 60 * 60 * 1000;
constexpr int RelaySpoolMaxEntries = 256;
constexpr int RetryBatchSize = 64;

QJsonObject makeMeshAnnounce(const LocalIdentity &identity) {
    return {
        {"type", "mesh-announce"},
        {"packetId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"originId", identity.userId},
        {"ttl", MeshTtl},
        {"hello", protocol::hello(identity)}
    };
}

QJsonObject objectFromBytes(const QByteArray &bytes) {
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    return error.error == QJsonParseError::NoError && doc.isObject() ? doc.object() : QJsonObject{};
}
}

ConversationManager::ConversationManager(Database &db, CryptoEngine &crypto, PeerManager &peers,
                                         TransportManager &transports, const LocalIdentity &identity,
                                         QObject *parent)
    : QObject(parent), db_(db), crypto_(crypto), peers_(peers), transports_(transports), identity_(identity) {
    for (const auto &c : db_.conversations()) conversations_.insert(c.id, c);
    connect(&transports_, &TransportManager::bytesReceived, this, &ConversationManager::onBytes);

    connect(&peers_, &PeerManager::peerUpdated, this, [this](const Peer &peer) {
        for (auto it = conversations_.begin(); it != conversations_.end(); ++it) {
            Conversation &c = it.value();
            if (c.type != ConversationType::Direct || !c.memberIds.contains(peer.userId)) continue;
            const QString displayName = peer.username.isEmpty() ? peer.userId : peer.username;
            if (c.name == displayName) continue;
            c.name = displayName;
            db_.saveConversation(c);
            emit conversationUpdated(c);
        }
        QTimer::singleShot(0, this, [this, peerId = peer.userId] {
            materializePendingPeerDeliveries(peerId);
            retryPersistentQueues();
        });
    });

    meshAnnounceTimer_.setInterval(10000);
    connect(&meshAnnounceTimer_, &QTimer::timeout, this, &ConversationManager::broadcastIdentity);
    meshAnnounceTimer_.start();
    QTimer::singleShot(1500, this, &ConversationManager::broadcastIdentity);

    retryTimer_.setInterval(2000);
    connect(&retryTimer_, &QTimer::timeout, this, &ConversationManager::retryPersistentQueues);
    retryTimer_.start();
    QTimer::singleShot(1000, this, &ConversationManager::retryPersistentQueues);
}

QList<Conversation> ConversationManager::conversations() const { return conversations_.values(); }
QList<Message> ConversationManager::messages(const QString &conversationId) const { return db_.messages(conversationId); }
Conversation ConversationManager::conversation(const QString &id) const { return conversations_.value(id); }

QString ConversationManager::deliveryIndicator(const QString &messageId) const {
    const QStringList states = db_.deliveryStates(messageId);
    if (states.isEmpty()) return {};
    int delivered = 0;
    bool sent = false;
    bool pending = false;
    for (const auto &state : states) {
        if (state == QStringLiteral("delivered")) ++delivered;
        else if (state == QStringLiteral("sent")) sent = true;
        else if (state.startsWith(QStringLiteral("pending"))) pending = true;
    }
    if (delivered == states.size()) return states.size() == 1 ? QStringLiteral("✓✓") : QStringLiteral("✓✓ %1/%2").arg(delivered).arg(states.size());
    if (delivered > 0) return QStringLiteral("✓ %1/%2").arg(delivered).arg(states.size());
    if (sent) return QStringLiteral("✓");
    if (pending) return QStringLiteral("…");
    return QStringLiteral("!");
}

QString ConversationManager::reliabilitySummary() const {
    return QStringLiteral("coda %1 · relay store %2").arg(db_.outboxCount()).arg(db_.relaySpoolCount());
}

QString ConversationManager::ensureDirectConversation(const QString &peerId) {
    QStringList ids{identity_.userId, peerId};
    std::sort(ids.begin(), ids.end());
    const QString id = QString::fromLatin1(QCryptographicHash::hash(ids.join('|').toUtf8(), QCryptographicHash::Sha256).toHex());
    if (!conversations_.contains(id)) {
        Conversation c;
        c.id = id;
        c.type = ConversationType::Direct;
        c.memberIds = ids;
        c.name = peers_.hasPeer(peerId) ? peers_.peer(peerId).username : peerId;
        conversations_[id] = c;
        db_.saveConversation(c);
        emit conversationUpdated(c);
    }
    return id;
}

QString ConversationManager::createGroup(const QString &name, const QStringList &peerIds) {
    QSet<QString> unique;
    for (const auto &id : peerIds) unique.insert(id);
    unique.insert(identity_.userId);
    Conversation c;
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type = ConversationType::Group;
    c.name = name.trimmed().isEmpty() ? QStringLiteral("Gruppo EChat") : name.trimmed().left(80);
    c.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
    c.memberIds = unique.values();
    std::sort(c.memberIds.begin(), c.memberIds.end());
    conversations_[c.id] = c;
    db_.saveConversation(c);
    emit conversationUpdated(c);

    Message control;
    control.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    control.conversationId = c.id;
    control.senderId = identity_.userId;
    control.kind = QStringLiteral("group-update");
    control.timestampMs = c.updatedAtMs;
    control.conversationType = ConversationType::Group;
    control.conversationName = c.name;
    control.conversationMembers = c.memberIds;
    sendControlMessage(control, c.memberIds, TransportPolicy::Auto);
    return c.id;
}

bool ConversationManager::updateGroup(const QString &conversationId, const QString &name,
                                      const QStringList &addedPeerIds, TransportPolicy policy) {
    if (!conversations_.contains(conversationId)) return false;
    Conversation c = conversations_.value(conversationId);
    if (c.type != ConversationType::Group || !c.memberIds.contains(identity_.userId)) return false;

    bool changed = false;
    const QString trimmedName = name.trimmed();
    if (!trimmedName.isEmpty() && trimmedName != c.name) {
        c.name = trimmedName.left(80);
        changed = true;
    }

    QSet<QString> members;
    for (const auto &id : c.memberIds) members.insert(id);
    members.insert(identity_.userId);
    for (const auto &peerId : addedPeerIds) {
        if (peerId.isEmpty() || peerId == identity_.userId || !peers_.hasPeer(peerId)) continue;
        if (!members.contains(peerId)) {
            members.insert(peerId);
            changed = true;
        }
    }
    if (!changed) return false;

    c.memberIds = members.values();
    std::sort(c.memberIds.begin(), c.memberIds.end());
    c.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
    conversations_[c.id] = c;
    db_.saveConversation(c);
    emit conversationUpdated(c);

    Message control;
    control.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    control.conversationId = c.id;
    control.senderId = identity_.userId;
    control.kind = QStringLiteral("group-update");
    control.timestampMs = c.updatedAtMs;
    control.conversationType = ConversationType::Group;
    control.conversationName = c.name;
    control.conversationMembers = c.memberIds;
    sendControlMessage(control, c.memberIds, policy);
    return true;
}

bool ConversationManager::relayPossible(const QString &targetPeerId, TransportPolicy policy) const {
    for (const auto &candidate : peers_.peers()) {
        if (candidate.userId == targetPeerId || candidate.userId == identity_.userId) continue;
        if (!candidate.connectivity.meshCapable) continue;
        if (!transports_.candidates(candidate, policy).isEmpty()) return true;
    }
    return false;
}

bool ConversationManager::sendObjectToPeer(const Peer &peer, const QJsonObject &object,
                                           TransportPolicy policy, QString *routeDescription) {
    const QByteArray directWire = protocol::frame(object);
    TransportType used{};
    QString description;
    if (transports_.send(peer, directWire, policy, &used, &description)) {
        if (routeDescription) *routeDescription = description.isEmpty() ? transportName(used) : description;
        return true;
    }

    QJsonObject relay = protocol::relay(identity_, peer.userId, object, policy, MeshTtl);
    const QString packetId = relay.value("packetId").toString();
    markPacketSeen(packetId);
    if (floodRelay(relay, policy)) {
        if (routeDescription) *routeDescription = QStringLiteral("Relay P2P");
        return true;
    }
    return false;
}

qint64 ConversationManager::retryDelayMs(int attempts, bool awaitingAck) {
    const qint64 base = awaitingAck ? 10000 : 2000;
    const int shift = qBound(0, attempts, 5);
    return qMin<qint64>(60000, base * (1LL << shift));
}

QString ConversationManager::outboxId(const QString &kind, const QString &logicalId, const QString &targetId) {
    return QStringLiteral("%1|%2|%3").arg(kind).arg(logicalId).arg(targetId);
}

void ConversationManager::queueOutbound(const QString &targetId, const QJsonObject &object, TransportPolicy policy,
                                        const QString &kind, const QString &logicalId, qint64 firstAttemptMs) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    OutboxEntry e;
    e.id = outboxId(kind, logicalId, targetId);
    e.targetId = targetId;
    e.payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    e.policy = policy;
    e.kind = kind;
    e.logicalId = logicalId;
    e.createdAtMs = now;
    e.nextAttemptMs = firstAttemptMs;
    e.expiresAtMs = now + OutboxLifetimeMs;
    e.attempts = 0;
    db_.saveOutbox(e);
    emit reliabilityStateChanged();
}

bool ConversationManager::sendControlMessage(const Message &message, const QStringList &recipientIds,
                                             TransportPolicy policy) {
    bool accepted = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QString &recipientId : recipientIds) {
        if (recipientId == identity_.userId || !peers_.hasPeer(recipientId)) continue;
        accepted = true;
        const Peer recipient = peers_.peer(recipientId);
        try {
            const QJsonObject object = protocol::encryptedMessage(crypto_.encryptFor(message, identity_, recipient));
            db_.saveDelivery(message.id, recipientId, QStringLiteral("pending-retry"));
            queueOutbound(recipientId, object, policy, QStringLiteral("message"), message.id, now);

            QString route;
            if (sendObjectToPeer(recipient, object, policy, &route)) {
                db_.saveDelivery(message.id, recipientId, QStringLiteral("sent"), route);
                db_.updateOutboxRetry(outboxId(QStringLiteral("message"), message.id, recipientId),
                                      now + retryDelayMs(0, true), 1);
            } else {
                db_.updateOutboxRetry(outboxId(QStringLiteral("message"), message.id, recipientId),
                                      now + retryDelayMs(0, false), 1);
            }
        } catch (const std::exception &e) {
            db_.saveDelivery(message.id, recipientId, QStringLiteral("crypto-error"));
            emit protocolError(QString::fromUtf8(e.what()));
        }
    }
    return accepted;
}

bool ConversationManager::sendMessage(const QString &conversationId, const QString &text, TransportPolicy policy) {
    if (!conversations_.contains(conversationId) || text.trimmed().isEmpty()) return false;

    // A newly selected routing policy applies to already queued E2EE messages in
    // this conversation too. Otherwise an old BluetoothOnly/LanOnly choice can
    // strand older messages while newer messages use the newly selected route.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool wokeQueuedMessages = adoptPolicyForQueuedMessages(conversationId, policy, now);
    if (wokeQueuedMessages) {
        emit reliabilityStateChanged();
        QTimer::singleShot(0, this, &ConversationManager::retryPersistentQueues);
    }

    const Conversation c = conversations_.value(conversationId);
    Message m;
    m.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m.kind = QStringLiteral("text");
    m.conversationId = c.id;
    m.senderId = identity_.userId;
    m.text = text;
    m.timestampMs = QDateTime::currentMSecsSinceEpoch();
    m.conversationType = c.type;
    m.conversationName = c.name;
    m.conversationMembers = c.memberIds;
    db_.saveMessage(m, QStringLiteral("local"));
    emit messageAdded(m);

    bool accepted = false;
    for (const QString &recipientId : c.memberIds) {
        if (recipientId == identity_.userId) continue;
        accepted = true;
        if (!peers_.hasPeer(recipientId)) {
            db_.saveDelivery(m.id, recipientId, QStringLiteral("pending-peer"));
            emit deliveryInfo(m.id, QStringLiteral("%1: identita' non ancora conosciuta; messaggio conservato").arg(recipientId));
            continue;
        }
        const Peer recipient = peers_.peer(recipientId);
        try {
            const auto envelope = crypto_.encryptFor(m, identity_, recipient);
            const QJsonObject object = protocol::encryptedMessage(envelope);
            db_.saveDelivery(m.id, recipientId, QStringLiteral("pending-retry"));
            queueOutbound(recipientId, object, policy, QStringLiteral("message"), m.id, now);

            QString route;
            if (sendObjectToPeer(recipient, object, policy, &route)) {
                db_.saveDelivery(m.id, recipientId, QStringLiteral("sent"), route);
                db_.updateOutboxRetry(outboxId(QStringLiteral("message"), m.id, recipientId),
                                      now + retryDelayMs(0, true), 1);
                emit deliveryInfo(m.id, QStringLiteral("%1 via %2 · in attesa di ACK").arg(recipient.username, route));
            } else {
                db_.saveDelivery(m.id, recipientId, QStringLiteral("pending-retry"));
                db_.updateOutboxRetry(outboxId(QStringLiteral("message"), m.id, recipientId),
                                      now + retryDelayMs(0, false), 1);
                emit deliveryInfo(m.id, QStringLiteral("%1: accodato, nessuna route disponibile").arg(recipient.username));
            }
        } catch (const std::exception &e) {
            db_.saveDelivery(m.id, recipientId, QStringLiteral("crypto-error"));
            emit protocolError(QString::fromUtf8(e.what()));
        }
    }
    return accepted;
}

bool ConversationManager::deleteOwnMessage(const QString &messageId, TransportPolicy policy) {
    const Message original = db_.messageById(messageId);
    if (original.id.isEmpty() || original.senderId != identity_.userId ||
        !conversations_.contains(original.conversationId)) return false;

    const Conversation c = conversations_.value(original.conversationId);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    Message control;
    control.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    control.conversationId = c.id;
    control.senderId = identity_.userId;
    control.kind = QStringLiteral("delete");
    control.targetMessageId = original.id;
    control.timestampMs = now;
    control.conversationType = c.type;
    control.conversationName = c.name;
    control.conversationMembers = c.memberIds;

    db_.saveMessageTombstone(original.id, identity_.userId, c.id, now);
    db_.deleteMessage(original.id);
    emit messageRemoved(c.id, original.id);
    emit reliabilityStateChanged();

    sendControlMessage(control, c.memberIds, policy);
    return true;
}

bool ConversationManager::changeUsername(const QString &username) {
    QString next = username.trimmed();
    next.replace('\n', ' ');
    next.replace('\r', ' ');
    next = next.simplified();
    if (next.isEmpty() || next.size() > 48 || next == identity_.username) return false;

    identity_.username = next;
    db_.saveIdentity(identity_);
    transports_.setIdentity(identity_);
    emit localUsernameChanged(identity_.username);
    QTimer::singleShot(0, this, &ConversationManager::broadcastIdentity);
    return true;
}

TransportPolicy ConversationManager::onlyPolicy(TransportType type) {
    switch (type) {
    case TransportType::Bluetooth: return TransportPolicy::BluetoothOnly;
    case TransportType::Lan: return TransportPolicy::LanOnly;
    case TransportType::Internet: return TransportPolicy::InternetOnly;
    }
    return TransportPolicy::Auto;
}

void ConversationManager::onBytes(TransportType type, const QString &transportPeerKey, const QByteArray &bytes) {
    const QString key = QString::number(static_cast<int>(type)) + '|' + transportPeerKey;
    auto &buffer = receiveBuffers_[key];
    buffer.append(bytes);
    for (const auto &object : protocol::consume(buffer)) handleObject(type, transportPeerKey, object);
}

void ConversationManager::acknowledgeMessage(const Message &message, const Peer &sender) {
    const QJsonObject ackObject = protocol::ack(message.id, identity_);
    QString ackRoute;
    if (!sendObjectToPeer(sender, ackObject, TransportPolicy::Auto, &ackRoute)) {
        queueOutbound(sender.userId, ackObject, TransportPolicy::Auto, QStringLiteral("ack"), message.id,
                      QDateTime::currentMSecsSinceEpoch() + 2000);
        emit deliveryInfo(message.id, QStringLiteral("ACK accodato: nessuna route verso %1").arg(sender.username));
    }
}

bool ConversationManager::applyGroupUpdate(const Message &message, const Peer &sender) {
    if (message.conversationType != ConversationType::Group ||
        message.conversationId.isEmpty() || message.conversationMembers.isEmpty() ||
        !message.conversationMembers.contains(sender.userId) ||
        !message.conversationMembers.contains(identity_.userId)) return false;

    Conversation c;
    if (conversations_.contains(message.conversationId)) {
        c = conversations_.value(message.conversationId);
        if (c.type != ConversationType::Group || !c.memberIds.contains(sender.userId)) return false;
    } else {
        c.id = message.conversationId;
        c.type = ConversationType::Group;
        c.name = message.conversationName.trimmed().isEmpty() ? QStringLiteral("Gruppo EChat")
                                                               : message.conversationName.trimmed().left(80);
    }

    QSet<QString> members;
    for (const auto &id : c.memberIds) members.insert(id);
    for (const auto &id : message.conversationMembers) if (!id.isEmpty()) members.insert(id);
    members.insert(identity_.userId);
    members.insert(sender.userId);
    c.memberIds = members.values();
    std::sort(c.memberIds.begin(), c.memberIds.end());

    if (message.timestampMs >= c.updatedAtMs) {
        const QString nextName = message.conversationName.trimmed();
        if (!nextName.isEmpty()) c.name = nextName.left(80);
        c.updatedAtMs = message.timestampMs;
    }

    conversations_[c.id] = c;
    db_.saveConversation(c);
    emit conversationUpdated(c);
    return true;
}

void ConversationManager::handleObject(TransportType type, const QString &transportPeerKey, const QJsonObject &object) {
    const QString kind = object.value("type").toString();

    if (kind == "relay") {
        handleRelay(type, transportPeerKey, object);
        return;
    }
    if (kind == "mesh-announce") {
        handleMeshAnnounce(type, transportPeerKey, object);
        return;
    }
    if (kind == "hello") {
        const QString userId = object.value("userId").toString();
        if (userId.isEmpty() || userId == identity_.userId) return;
        if (!peers_.updateFromHello(transportPeerKey, type, object)) return;
        ensureDirectConversation(userId);
        QTimer::singleShot(0, this, &ConversationManager::broadcastIdentity);
        return;
    }
    if (kind == "ack") {
        const QString from = object.value("senderId").toString();
        const QString messageId = object.value("messageId").toString();
        if (from.isEmpty() || messageId.isEmpty() || !peers_.hasPeer(from)) return;
        if (!protocol::verifyAck(object, peers_.peer(from).signingPublicKey)) {
            emit protocolError(QStringLiteral("ACK con firma Ed25519 non valida da %1").arg(from));
            return;
        }
        db_.updateDelivery(messageId, from, QStringLiteral("delivered"));
        db_.deleteOutboxFor(messageId, from, QStringLiteral("message"));
        emit reliabilityStateChanged();
        emit deliveryInfo(messageId, QStringLiteral("consegnato a %1").arg(peers_.peer(from).username));
        return;
    }
    if (kind != "message") return;

    EncryptedEnvelope e;
    e.cryptoVersion = object.value("cryptoVersion").toInt();
    e.messageId = object.value("messageId").toString();
    e.conversationId = object.value("conversationId").toString();
    e.senderId = object.value("senderId").toString();
    e.recipientId = object.value("recipientId").toString();
    e.ephemeralPublicKey = QByteArray::fromBase64(object.value("ephemeralPublicKey").toString().toLatin1());
    e.nonce = QByteArray::fromBase64(object.value("nonce").toString().toLatin1());
    e.cipherText = QByteArray::fromBase64(object.value("cipherText").toString().toLatin1());
    e.signature = QByteArray::fromBase64(object.value("signature").toString().toLatin1());
    if (e.recipientId != identity_.userId || !peers_.hasPeer(e.senderId)) return;

    const Peer sender = peers_.peer(e.senderId);
    Message message;
    if (!crypto_.decryptFrom(e, identity_, sender, message)) {
        emit protocolError(QStringLiteral("Messaggio E2EE v2 non autenticabile da %1").arg(sender.username));
        return;
    }
    if (message.id != e.messageId || message.conversationId != e.conversationId || message.senderId != e.senderId) return;

    if (message.kind == QStringLiteral("delete")) {
        if (message.targetMessageId.isEmpty()) return;
        const Message target = db_.messageById(message.targetMessageId);
        if (!target.id.isEmpty() && (target.senderId != message.senderId || target.conversationId != message.conversationId)) {
            emit protocolError(QStringLiteral("Eliminazione rifiutata: %1 non e' l'autore del messaggio").arg(sender.username));
            return;
        }
        const QString tombstoneSender = db_.messageTombstoneSender(message.targetMessageId);
        if (!tombstoneSender.isEmpty() && tombstoneSender != message.senderId) return;

        db_.saveMessageTombstone(message.targetMessageId, message.senderId,
                                 message.conversationId, message.timestampMs);
        if (!target.id.isEmpty()) db_.deleteMessage(message.targetMessageId);
        emit messageRemoved(message.conversationId, message.targetMessageId);
        emit reliabilityStateChanged();
        acknowledgeMessage(message, sender);
        return;
    }

    if (message.kind == QStringLiteral("group-update")) {
        if (!applyGroupUpdate(message, sender)) {
            emit protocolError(QStringLiteral("Aggiornamento gruppo rifiutato da %1").arg(sender.username));
            return;
        }
        acknowledgeMessage(message, sender);
        return;
    }

    if (message.kind != QStringLiteral("text")) {
        emit protocolError(QStringLiteral("Tipo messaggio EChat sconosciuto: %1").arg(message.kind));
        return;
    }

    if (!conversations_.contains(message.conversationId)) {
        Conversation c;
        c.id = message.conversationId;
        c.type = message.conversationType;
        c.name = message.conversationType == ConversationType::Group ? message.conversationName : sender.username;
        c.memberIds = message.conversationMembers;
        if (c.memberIds.isEmpty()) c.memberIds = {identity_.userId, sender.userId};
        if (c.type == ConversationType::Group) c.updatedAtMs = message.timestampMs;
        conversations_[c.id] = c;
        db_.saveConversation(c);
        emit conversationUpdated(c);
    }

    const QString tombstoneSender = db_.messageTombstoneSender(message.id);
    if (tombstoneSender.isEmpty() || tombstoneSender != message.senderId) {
        if (!db_.hasMessage(message.id)) {
            db_.saveMessage(message, QStringLiteral("received"));
            emit messageAdded(message);
        }
    }

    acknowledgeMessage(message, sender);
}

bool ConversationManager::floodRelay(QJsonObject relay, TransportPolicy policy, const QString &excludePeerId) {
    int sent = 0;
    const QByteArray wire = protocol::frame(relay);
    for (const auto &neighbor : peers_.peers()) {
        if (neighbor.userId == identity_.userId || neighbor.userId == excludePeerId) continue;
        if (!neighbor.connectivity.meshCapable) continue;
        if (transports_.candidates(neighbor, policy).isEmpty()) continue;
        if (transports_.send(neighbor, wire, policy)) ++sent;
    }
    return sent > 0;
}

bool ConversationManager::forwardRelay(const QJsonObject &relay, TransportPolicy policy, const QString &excludePeerId) {
    const QString targetId = relay.value("targetId").toString();
    const QByteArray wire = protocol::frame(relay);
    if (peers_.hasPeer(targetId)) {
        const Peer target = peers_.peer(targetId);
        if (!transports_.candidates(target, policy).isEmpty() && transports_.send(target, wire, policy)) return true;
    }
    if (relay.value("ttl").toInt(-1) <= 0) return false;
    return floodRelay(relay, policy, excludePeerId);
}

void ConversationManager::queueRelaySpool(const QJsonObject &relay, TransportPolicy policy) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 created = static_cast<qint64>(relay.value("createdAtMs").toDouble());
    const qint64 expires = created > 0 ? created + RelaySpoolLifetimeMs : now + RelaySpoolLifetimeMs;
    if (expires <= now) return;

    RelaySpoolEntry e;
    e.packetId = relay.value("packetId").toString();
    e.targetId = relay.value("targetId").toString();
    e.payload = QJsonDocument(relay).toJson(QJsonDocument::Compact);
    e.policy = policy;
    e.createdAtMs = created > 0 ? created : now;
    e.nextAttemptMs = now + 2000;
    e.expiresAtMs = expires;
    e.attempts = 0;
    if (e.packetId.isEmpty() || e.targetId.isEmpty()) return;
    db_.saveRelaySpool(e);
    db_.trimRelaySpool(RelaySpoolMaxEntries);
    emit reliabilityStateChanged();
}

void ConversationManager::handleRelay(TransportType type, const QString &transportPeerKey, const QJsonObject &object) {
    QJsonObject inner;
    if (!protocol::verifyRelay(object, &inner)) {
        emit protocolError(QStringLiteral("Relay P2P rifiutato: firma/header non validi"));
        return;
    }

    const QString packetId = object.value("packetId").toString();
    const QString targetId = object.value("targetId").toString();
    const QString originId = object.value("originId").toString();
    const int ttl = object.value("ttl").toInt(-1);
    const int policyInt = object.value("policy").toInt(static_cast<int>(TransportPolicy::Auto));
    const QJsonObject originHello = object.value("originHello").toObject();
    if (!markPacketSeen(packetId)) return;

    if (originId != identity_.userId) peers_.updateIdentityFromHello(originHello);

    if (targetId == identity_.userId) {
        handleObject(type, transportPeerKey, inner);
        return;
    }

    QJsonObject forwarded = object;
    forwarded["ttl"] = ttl > 0 ? ttl - 1 : 0;
    const QString previousPeer = peers_.peerIdForTransportKey(type, transportPeerKey);
    const auto policy = static_cast<TransportPolicy>(policyInt);
    if (!forwardRelay(forwarded, policy, previousPeer)) {
        queueRelaySpool(forwarded, policy);
    }
}

void ConversationManager::handleMeshAnnounce(TransportType type, const QString &transportPeerKey, const QJsonObject &object) {
    const QString packetId = object.value("packetId").toString();
    const int ttl = object.value("ttl").toInt(-1);
    const QJsonObject hello = object.value("hello").toObject();
    if (packetId.isEmpty() || ttl < 0 || ttl > 8 || hello.isEmpty()) return;
    if (!markPacketSeen(packetId)) return;

    const QString announcedId = hello.value("userId").toString();
    const QString originId = object.value("originId").toString();
    if (announcedId.isEmpty() || originId != announcedId || !protocol::verifyHello(hello)) return;
    if (announcedId != identity_.userId) peers_.updateIdentityFromHello(hello);

    if (ttl == 0) return;
    QJsonObject forwarded = object;
    forwarded["ttl"] = ttl - 1;
    const QString previousPeer = peers_.peerIdForTransportKey(type, transportPeerKey);
    const QByteArray wire = protocol::frame(forwarded);
    for (const auto &neighbor : peers_.peers()) {
        if (neighbor.userId == identity_.userId || neighbor.userId == previousPeer) continue;
        if (transports_.candidates(neighbor, TransportPolicy::Auto).isEmpty()) continue;
        transports_.send(neighbor, wire, TransportPolicy::Auto);
    }
}

void ConversationManager::broadcastIdentity() {
    QJsonObject announce = makeMeshAnnounce(identity_);
    const QString packetId = announce.value("packetId").toString();
    markPacketSeen(packetId);
    const QByteArray wire = protocol::frame(announce);
    for (const auto &neighbor : peers_.peers()) {
        if (neighbor.userId == identity_.userId) continue;
        if (transports_.candidates(neighbor, TransportPolicy::Auto).isEmpty()) continue;
        transports_.send(neighbor, wire, TransportPolicy::Auto);
    }
}

void ConversationManager::materializePendingPeerDeliveries(const QString &peerId) {
    if (!peers_.hasPeer(peerId)) return;
    const Peer recipient = peers_.peer(peerId);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto &messageId : db_.pendingPeerMessageIds(peerId, identity_.userId)) {
        Message m = db_.messageById(messageId);
        if (m.id.isEmpty() || !conversations_.contains(m.conversationId)) continue;
        const Conversation c = conversations_.value(m.conversationId);
        m.conversationType = c.type;
        m.conversationName = c.name;
        m.conversationMembers = c.memberIds;
        try {
            const QJsonObject object = protocol::encryptedMessage(crypto_.encryptFor(m, identity_, recipient));
            queueOutbound(peerId, object, TransportPolicy::Auto, QStringLiteral("message"), m.id, now);
            db_.updateDelivery(m.id, peerId, QStringLiteral("pending-retry"));
        } catch (const std::exception &e) {
            db_.updateDelivery(m.id, peerId, QStringLiteral("crypto-error"));
            emit protocolError(QString::fromUtf8(e.what()));
        }
    }
}

bool ConversationManager::adoptPolicyForQueuedMessages(const QString &conversationId,
                                                        TransportPolicy policy, qint64 nowMs) {
    if (conversationId.isEmpty()) return false;
    bool changed = false;
    for (const auto &entry : db_.outboxEntries()) {
        // ACKs use Auto deliberately; only encrypted message/control envelopes
        // inherit the conversation's newly selected transport policy.
        if (entry.kind != QStringLiteral("message")) continue;
        const QJsonObject object = objectFromBytes(entry.payload);
        if (object.isEmpty() || object.value("type").toString() != QStringLiteral("message")) continue;
        if (object.value("conversationId").toString() != conversationId) continue;
        db_.updateOutboxPolicy(entry.id, policy, nowMs, 0);
        changed = true;
    }
    return changed;
}

void ConversationManager::retryQueuedMessages(const QString &conversationId, TransportPolicy policy) {
    if (!conversations_.contains(conversationId)) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!adoptPolicyForQueuedMessages(conversationId, policy, now)) return;
    emit reliabilityStateChanged();
    // Run after the UI signal/ongoing send returns, avoiding nested retry loops.
    QTimer::singleShot(0, this, &ConversationManager::retryPersistentQueues);
}

void ConversationManager::retryPersistentQueues() {
    if (retrying_) return;
    retrying_ = true;
    retryOutbox();
    retryRelaySpool();
    retrying_ = false;
}

void ConversationManager::retryOutbox() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto &entry : db_.dueOutbox(now, RetryBatchSize)) {
        if (entry.expiresAtMs <= now) {
            if (entry.kind == QStringLiteral("message")) {
                db_.updateDelivery(entry.logicalId, entry.targetId, QStringLiteral("expired"));
                emit deliveryInfo(entry.logicalId, QStringLiteral("consegna scaduta verso %1").arg(entry.targetId));
            }
            db_.deleteOutbox(entry.id);
            emit reliabilityStateChanged();
            continue;
        }
        if (!peers_.hasPeer(entry.targetId)) {
            db_.updateOutboxRetry(entry.id, now + retryDelayMs(entry.attempts, false), entry.attempts + 1);
            continue;
        }
        const QJsonObject object = objectFromBytes(entry.payload);
        if (object.isEmpty()) {
            db_.deleteOutbox(entry.id);
            emit reliabilityStateChanged();
            continue;
        }

        QString route;
        const bool sent = sendObjectToPeer(peers_.peer(entry.targetId), object, entry.policy, &route);
        const int attempts = entry.attempts + 1;
        if (sent) {
            if (entry.kind == QStringLiteral("ack")) {
                db_.deleteOutbox(entry.id);
                emit reliabilityStateChanged();
            } else {
                db_.updateDelivery(entry.logicalId, entry.targetId, QStringLiteral("sent"), route);
                db_.updateOutboxRetry(entry.id, now + retryDelayMs(attempts, true), attempts);
                emit deliveryInfo(entry.logicalId, QStringLiteral("ritentato via %1 · attesa ACK").arg(route));
            }
        } else {
            if (entry.kind == QStringLiteral("message"))
                db_.updateDelivery(entry.logicalId, entry.targetId, QStringLiteral("pending-retry"));
            db_.updateOutboxRetry(entry.id, now + retryDelayMs(attempts, false), attempts);
        }
    }
}

void ConversationManager::retryRelaySpool() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto &entry : db_.dueRelaySpool(now, RetryBatchSize)) {
        if (entry.expiresAtMs <= now) {
            db_.deleteRelaySpool(entry.packetId);
            emit reliabilityStateChanged();
            continue;
        }
        const QJsonObject relay = objectFromBytes(entry.payload);
        if (relay.isEmpty() || !protocol::verifyRelay(relay, nullptr)) {
            db_.deleteRelaySpool(entry.packetId);
            emit reliabilityStateChanged();
            continue;
        }
        if (forwardRelay(relay, entry.policy)) {
            db_.deleteRelaySpool(entry.packetId);
            emit reliabilityStateChanged();
        } else {
            const int attempts = entry.attempts + 1;
            db_.updateRelaySpoolRetry(entry.packetId, now + retryDelayMs(attempts, false), attempts);
        }
    }
}

bool ConversationManager::markPacketSeen(const QString &packetId) {
    if (packetId.isEmpty()) return false;
    cleanupSeenPackets();
    if (seenPackets_.contains(packetId)) return false;
    seenPackets_.insert(packetId, QDateTime::currentMSecsSinceEpoch());
    return true;
}

void ConversationManager::cleanupSeenPackets() {
    if (seenPackets_.size() < 256) return;
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - SeenPacketLifetimeMs;
    for (auto it = seenPackets_.begin(); it != seenPackets_.end();) {
        if (it.value() < cutoff) it = seenPackets_.erase(it);
        else ++it;
    }
}

} // namespace ec
