#include "core/ConversationManager.h"
#include "core/PeerManager.h"
#include "core/Protocol.h"
#include "crypto/CryptoEngine.h"
#include "storage/Database.h"
#include "transport/TransportManager.h"

#include <QCryptographicHash>
#include <QSet>
#include <algorithm>

namespace ec {

ConversationManager::ConversationManager(Database &db, CryptoEngine &crypto, PeerManager &peers,
                                         TransportManager &transports, const LocalIdentity &identity,
                                         QObject *parent)
    : QObject(parent), db_(db), crypto_(crypto), peers_(peers), transports_(transports), identity_(identity) {
    for (const auto &c : db_.conversations()) conversations_.insert(c.id, c);
    connect(&transports_, &TransportManager::bytesReceived, this, &ConversationManager::onBytes);
}

QList<Conversation> ConversationManager::conversations() const { return conversations_.values(); }
QList<Message> ConversationManager::messages(const QString &conversationId) const { return db_.messages(conversationId); }
Conversation ConversationManager::conversation(const QString &id) const { return conversations_.value(id); }

QString ConversationManager::ensureDirectConversation(const QString &peerId) {
    QStringList ids{identity_.userId, peerId}; std::sort(ids.begin(), ids.end());
    const QString id = QString::fromLatin1(QCryptographicHash::hash(ids.join('|').toUtf8(), QCryptographicHash::Sha256).toHex());
    if (!conversations_.contains(id)) {
        Conversation c; c.id=id; c.type=ConversationType::Direct; c.memberIds=ids;
        c.name=peers_.hasPeer(peerId) ? peers_.peer(peerId).username : peerId;
        conversations_[id]=c; db_.saveConversation(c); emit conversationUpdated(c);
    }
    return id;
}

QString ConversationManager::createGroup(const QString &name, const QStringList &peerIds) {
    QSet<QString> unique; for (const auto &id : peerIds) unique.insert(id); unique.insert(identity_.userId);
    Conversation c; c.id=QUuid::createUuid().toString(QUuid::WithoutBraces); c.type=ConversationType::Group;
    c.name=name.trimmed().isEmpty()?QStringLiteral("Gruppo EChat"):name.trimmed(); c.memberIds=unique.values();
    std::sort(c.memberIds.begin(), c.memberIds.end()); conversations_[c.id]=c; db_.saveConversation(c); emit conversationUpdated(c); return c.id;
}

bool ConversationManager::sendMessage(const QString &conversationId, const QString &text, TransportPolicy policy) {
    if (!conversations_.contains(conversationId) || text.trimmed().isEmpty()) return false;
    const Conversation c=conversations_.value(conversationId);
    Message m; m.id=QUuid::createUuid().toString(QUuid::WithoutBraces); m.conversationId=c.id; m.senderId=identity_.userId;
    m.text=text; m.timestampMs=QDateTime::currentMSecsSinceEpoch(); m.conversationType=c.type; m.conversationName=c.name; m.conversationMembers=c.memberIds;
    db_.saveMessage(m, QStringLiteral("local")); emit messageAdded(m);

    bool anySent=false;
    for (const QString &recipientId : c.memberIds) {
        if (recipientId==identity_.userId) continue;
        if (!peers_.hasPeer(recipientId)) {
            db_.saveDelivery(m.id, recipientId, QStringLiteral("pending-peer"));
            emit deliveryInfo(m.id, QStringLiteral("%1: peer sconosciuto/non ancora incontrato").arg(recipientId)); continue;
        }
        const Peer recipient=peers_.peer(recipientId);
        try {
            const auto envelope=crypto_.encryptFor(m, identity_, recipient);
            const QByteArray wire=protocol::frame(protocol::encryptedMessage(envelope));
            TransportType used{};
            if (transports_.send(recipient, wire, policy, &used)) {
                db_.saveDelivery(m.id, recipientId, QStringLiteral("sent"), transportName(used));
                emit deliveryInfo(m.id, QStringLiteral("%1 via %2").arg(recipient.username, transportName(used))); anySent=true;
            } else {
                db_.saveDelivery(m.id, recipientId, QStringLiteral("pending-no-route"));
                emit deliveryInfo(m.id, QStringLiteral("%1: nessun transport comune raggiungibile").arg(recipient.username));
            }
        } catch (const std::exception &e) {
            db_.saveDelivery(m.id, recipientId, QStringLiteral("crypto-error")); emit protocolError(QString::fromUtf8(e.what()));
        }
    }
    return anySent;
}

TransportPolicy ConversationManager::onlyPolicy(TransportType type) {
    switch(type) { case TransportType::Bluetooth:return TransportPolicy::BluetoothOnly; case TransportType::Lan:return TransportPolicy::LanOnly; case TransportType::Internet:return TransportPolicy::InternetOnly; }
    return TransportPolicy::Auto;
}

void ConversationManager::onBytes(TransportType type, const QString &transportPeerKey, const QByteArray &bytes) {
    const QString key=QString::number(static_cast<int>(type))+'|'+transportPeerKey;
    auto &buffer=receiveBuffers_[key]; buffer.append(bytes);
    for (const auto &object:protocol::consume(buffer)) handleObject(type, transportPeerKey, object);
}

void ConversationManager::handleObject(TransportType type, const QString &transportPeerKey, const QJsonObject &object) {
    const QString kind=object.value("type").toString();
    if (kind=="hello") {
        const QString userId=object.value("userId").toString();
        if (userId.isEmpty() || userId==identity_.userId) return;
        if (!peers_.updateFromHello(transportPeerKey, type, object)) return;
        ensureDirectConversation(userId); return;
    }
    if (kind=="ack") {
        const QString from=object.value("senderId").toString(); const QString messageId=object.value("messageId").toString();
        if (from.isEmpty() || messageId.isEmpty() || !peers_.hasPeer(from)) return;
        if (!protocol::verifyAck(object, peers_.peer(from).signingPublicKey)) {
            emit protocolError(QStringLiteral("ACK con firma Ed25519 non valida da %1").arg(from)); return;
        }
        db_.updateDelivery(messageId, from, QStringLiteral("delivered"));
        emit deliveryInfo(messageId, QStringLiteral("consegnato a %1").arg(peers_.peer(from).username)); return;
    }
    if (kind!="message") return;

    EncryptedEnvelope e;
    e.cryptoVersion=object.value("cryptoVersion").toInt(); e.messageId=object.value("messageId").toString();
    e.conversationId=object.value("conversationId").toString(); e.senderId=object.value("senderId").toString(); e.recipientId=object.value("recipientId").toString();
    e.ephemeralPublicKey=QByteArray::fromBase64(object.value("ephemeralPublicKey").toString().toLatin1());
    e.nonce=QByteArray::fromBase64(object.value("nonce").toString().toLatin1());
    e.cipherText=QByteArray::fromBase64(object.value("cipherText").toString().toLatin1());
    e.signature=QByteArray::fromBase64(object.value("signature").toString().toLatin1());
    if (e.recipientId!=identity_.userId || !peers_.hasPeer(e.senderId)) return;
    const Peer sender=peers_.peer(e.senderId);
    Message message;
    if (!crypto_.decryptFrom(e, identity_, sender, message)) {
        emit protocolError(QStringLiteral("Messaggio E2EE v2 non autenticabile da %1").arg(sender.username)); return;
    }
    if (message.id!=e.messageId || message.conversationId!=e.conversationId || message.senderId!=e.senderId) return;

    if (!conversations_.contains(message.conversationId)) {
        Conversation c; c.id=message.conversationId; c.type=message.conversationType;
        c.name=message.conversationType==ConversationType::Group?message.conversationName:sender.username;
        c.memberIds=message.conversationMembers; if (c.memberIds.isEmpty()) c.memberIds={identity_.userId,sender.userId};
        conversations_[c.id]=c; db_.saveConversation(c); emit conversationUpdated(c);
    }
    if (!db_.hasMessage(message.id)) { db_.saveMessage(message, QStringLiteral("received")); emit messageAdded(message); }
    transports_.send(sender, protocol::frame(protocol::ack(message.id, identity_)), onlyPolicy(type));
}

} // namespace ec
