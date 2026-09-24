#include "core/ConversationManager.h"
#include "core/PeerManager.h"
#include "core/Protocol.h"
#include "crypto/CryptoEngine.h"
#include "storage/Database.h"
#include "transport/TransportManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace ec {
namespace {
constexpr int MeshTtl = 4;
constexpr qint64 SeenPacketLifetimeMs = 2 * 60 * 1000;

QJsonObject makeMeshAnnounce(const LocalIdentity &identity) {
    return {
        {"type", "mesh-announce"},
        {"packetId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"originId", identity.userId},
        {"ttl", MeshTtl},
        {"hello", protocol::hello(identity)}
    };
}
}

ConversationManager::ConversationManager(Database &db, CryptoEngine &crypto, PeerManager &peers,
                                         TransportManager &transports, const LocalIdentity &identity,
                                         QObject *parent)
    : QObject(parent), db_(db), crypto_(crypto), peers_(peers), transports_(transports), identity_(identity) {
    for (const auto &c : db_.conversations()) conversations_.insert(c.id, c);
    connect(&transports_, &TransportManager::bytesReceived, this, &ConversationManager::onBytes);

    meshAnnounceTimer_.setInterval(10000);
    connect(&meshAnnounceTimer_, &QTimer::timeout, this, &ConversationManager::broadcastIdentity);
    meshAnnounceTimer_.start();
    QTimer::singleShot(1500, this, &ConversationManager::broadcastIdentity);
}

QList<Conversation> ConversationManager::conversations() const { return conversations_.values(); }
QList<Message> ConversationManager::messages(const QString &conversationId) const { return db_.messages(conversationId); }
Conversation ConversationManager::conversation(const QString &id) const { return conversations_.value(id); }

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
    c.name = name.trimmed().isEmpty() ? QStringLiteral("Gruppo EChat") : name.trimmed();
    c.memberIds = unique.values();
    std::sort(c.memberIds.begin(), c.memberIds.end());
    conversations_[c.id] = c;
    db_.saveConversation(c);
    emit conversationUpdated(c);
    return c.id;
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

bool ConversationManager::sendMessage(const QString &conversationId, const QString &text, TransportPolicy policy) {
    if (!conversations_.contains(conversationId) || text.trimmed().isEmpty()) return false;
    const Conversation c = conversations_.value(conversationId);
    Message m;
    m.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m.conversationId = c.id;
    m.senderId = identity_.userId;
    m.text = text;
    m.timestampMs = QDateTime::currentMSecsSinceEpoch();
    m.conversationType = c.type;
    m.conversationName = c.name;
    m.conversationMembers = c.memberIds;
    db_.saveMessage(m, QStringLiteral("local"));
    emit messageAdded(m);

    bool anySent = false;
    for (const QString &recipientId : c.memberIds) {
        if (recipientId == identity_.userId) continue;
        if (!peers_.hasPeer(recipientId)) {
            db_.saveDelivery(m.id, recipientId, QStringLiteral("pending-peer"));
            emit deliveryInfo(m.id, QStringLiteral("%1: identita' non ancora conosciuta").arg(recipientId));
            continue;
        }
        const Peer recipient = peers_.peer(recipientId);
        try {
            const auto envelope = crypto_.encryptFor(m, identity_, recipient);
            QString route;
            if (sendObjectToPeer(recipient, protocol::encryptedMessage(envelope), policy, &route)) {
                db_.saveDelivery(m.id, recipientId, QStringLiteral("sent"), route);
                emit deliveryInfo(m.id, QStringLiteral("%1 via %2").arg(recipient.username, route));
                anySent = true;
            } else {
                db_.saveDelivery(m.id, recipientId, QStringLiteral("pending-no-route"));
                emit deliveryInfo(m.id, QStringLiteral("%1: nessuna route diretta o relay disponibile").arg(recipient.username));
            }
        } catch (const std::exception &e) {
            db_.saveDelivery(m.id, recipientId, QStringLiteral("crypto-error"));
            emit protocolError(QString::fromUtf8(e.what()));
        }
    }
    return anySent;
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

    if (!conversations_.contains(message.conversationId)) {
        Conversation c;
        c.id = message.conversationId;
        c.type = message.conversationType;
        c.name = message.conversationType == ConversationType::Group ? message.conversationName : sender.username;
        c.memberIds = message.conversationMembers;
        if (c.memberIds.isEmpty()) c.memberIds = {identity_.userId, sender.userId};
        conversations_[c.id] = c;
        db_.saveConversation(c);
        emit conversationUpdated(c);
    }
    if (!db_.hasMessage(message.id)) {
        db_.saveMessage(message, QStringLiteral("received"));
        emit messageAdded(message);
    }

    QString ackRoute;
    if (!sendObjectToPeer(sender, protocol::ack(message.id, identity_), TransportPolicy::Auto, &ackRoute)) {
        emit deliveryInfo(message.id, QStringLiteral("ACK in attesa: nessuna route verso %1").arg(sender.username));
    }
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
    if (ttl <= 0) return;

    QJsonObject forwarded = object;
    forwarded["ttl"] = ttl - 1;
    const QString previousPeer = peers_.peerIdForTransportKey(type, transportPeerKey);
    floodRelay(forwarded, static_cast<TransportPolicy>(policyInt), previousPeer);
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
