#pragma once

#include <QList>
#include "core/Types.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>

namespace ec {
class CryptoEngine;
class Database;
class PeerManager;
class TransportManager;

class ConversationManager : public QObject {
    Q_OBJECT
public:
    ConversationManager(Database &db, CryptoEngine &crypto, PeerManager &peers,
                        TransportManager &transports, const LocalIdentity &identity,
                        QObject *parent = nullptr);

    QList<Conversation> conversations() const;
    QList<Message> messages(const QString &conversationId) const;
    Conversation conversation(const QString &id) const;

    QString ensureDirectConversation(const QString &peerId);
    QString createGroup(const QString &name, const QStringList &peerIds);
    bool sendMessage(const QString &conversationId, const QString &text,
                     TransportPolicy policy = TransportPolicy::Auto);

signals:
    void conversationUpdated(const ec::Conversation &conversation);
    void messageAdded(const ec::Message &message);
    void deliveryInfo(const QString &messageId, const QString &info);
    void protocolError(const QString &error);

private:
    static TransportPolicy onlyPolicy(TransportType type);
    void onBytes(TransportType type, const QString &transportPeerKey, const QByteArray &bytes);
    void handleObject(TransportType type, const QString &transportPeerKey, const QJsonObject &object);

    Database &db_;
    CryptoEngine &crypto_;
    PeerManager &peers_;
    TransportManager &transports_;
    LocalIdentity identity_;
    QHash<QString, Conversation> conversations_;
    QHash<QString, QByteArray> receiveBuffers_;
};

} // namespace ec
