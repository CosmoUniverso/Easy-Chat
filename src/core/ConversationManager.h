#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QTimer>
#include "core/Types.h"

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
    bool updateGroup(const QString &conversationId, const QString &name, const QStringList &addedPeerIds,
                     TransportPolicy policy = TransportPolicy::Auto);
    bool sendMessage(const QString &conversationId, const QString &text,
                     TransportPolicy policy = TransportPolicy::Auto);
    bool deleteOwnMessage(const QString &messageId,
                          TransportPolicy policy = TransportPolicy::Auto);
    bool changeUsername(const QString &username);
    void retryQueuedMessages(const QString &conversationId,
                             TransportPolicy policy = TransportPolicy::Auto);
    bool relayPossible(const QString &targetPeerId,
                       TransportPolicy policy = TransportPolicy::Auto) const;
    QString deliveryIndicator(const QString &messageId) const;
    QString reliabilitySummary() const;

signals:
    void conversationUpdated(const ec::Conversation &conversation);
    void messageAdded(const ec::Message &message);
    void messageRemoved(const QString &conversationId, const QString &messageId);
    void localUsernameChanged(const QString &username);
    void deliveryInfo(const QString &messageId, const QString &info);
    void protocolError(const QString &error);
    void reliabilityStateChanged();

private:
    static TransportPolicy onlyPolicy(TransportType type);
    static qint64 retryDelayMs(int attempts, bool awaitingAck = false);
    static QString outboxId(const QString &kind, const QString &logicalId, const QString &targetId);

    void onBytes(TransportType type, const QString &transportPeerKey, const QByteArray &bytes);
    void handleObject(TransportType type, const QString &transportPeerKey, const QJsonObject &object);

    bool sendObjectToPeer(const Peer &peer, const QJsonObject &object, TransportPolicy policy,
                          QString *routeDescription = nullptr);
    bool floodRelay(QJsonObject relay, TransportPolicy policy, const QString &excludePeerId = {});
    bool forwardRelay(const QJsonObject &relay, TransportPolicy policy, const QString &excludePeerId = {});
    void handleRelay(TransportType type, const QString &transportPeerKey, const QJsonObject &object);
    void handleMeshAnnounce(TransportType type, const QString &transportPeerKey, const QJsonObject &object);
    void broadcastIdentity();
    bool markPacketSeen(const QString &packetId);
    void cleanupSeenPackets();

    void queueOutbound(const QString &targetId, const QJsonObject &object, TransportPolicy policy,
                       const QString &kind, const QString &logicalId, qint64 firstAttemptMs);
    bool adoptPolicyForQueuedMessages(const QString &conversationId, TransportPolicy policy, qint64 nowMs);
    void retryPersistentQueues();
    void retryOutbox();
    void retryRelaySpool();
    void materializePendingPeerDeliveries(const QString &peerId);
    void queueRelaySpool(const QJsonObject &relay, TransportPolicy policy);
    bool sendControlMessage(const Message &message, const QStringList &recipientIds, TransportPolicy policy);
    void acknowledgeMessage(const Message &message, const Peer &sender);
    bool applyGroupUpdate(const Message &message, const Peer &sender);

    Database &db_;
    CryptoEngine &crypto_;
    PeerManager &peers_;
    TransportManager &transports_;
    LocalIdentity identity_;
    QHash<QString, Conversation> conversations_;
    QHash<QString, QByteArray> receiveBuffers_;
    QHash<QString, qint64> seenPackets_;
    QTimer meshAnnounceTimer_;
    QTimer retryTimer_;
    bool retrying_ = false;
};

} // namespace ec
