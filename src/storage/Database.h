#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QStringList>
#include "core/Types.h"

namespace ec {

class Database {
public:
    explicit Database(const QString &path);
    ~Database();

    void migrate();
    bool hasIdentity() const;
    LocalIdentity loadIdentity() const;
    void saveIdentity(const LocalIdentity &identity);

    void upsertPeer(const Peer &peer);
    QList<Peer> peers() const;
    void saveConversation(const Conversation &conversation);
    QList<Conversation> conversations() const;
    void saveMessage(const Message &message, const QString &state);
    Message messageById(const QString &messageId) const;
    void saveDelivery(const QString &messageId, const QString &recipientId,
                      const QString &state, const QString &transport = {});
    void updateDelivery(const QString &messageId, const QString &recipientId,
                        const QString &state, const QString &transport = {});
    QStringList deliveryStates(const QString &messageId) const;
    QStringList pendingPeerMessageIds(const QString &recipientId, const QString &senderId) const;
    bool hasMessage(const QString &messageId) const;
    QList<Message> messages(const QString &conversationId) const;
    void deleteMessage(const QString &messageId);
    void saveMessageTombstone(const QString &messageId, const QString &senderId,
                              const QString &conversationId, qint64 deletedAtMs);
    QString messageTombstoneSender(const QString &messageId) const;

    void saveOutbox(const OutboxEntry &entry);
    QList<OutboxEntry> dueOutbox(qint64 nowMs, int limit = 64) const;
    void updateOutboxRetry(const QString &id, qint64 nextAttemptMs, int attempts);
    void deleteOutbox(const QString &id);
    void deleteOutboxFor(const QString &logicalId, const QString &targetId, const QString &kind);
    int outboxCount() const;

    void saveRelaySpool(const RelaySpoolEntry &entry);
    QList<RelaySpoolEntry> dueRelaySpool(qint64 nowMs, int limit = 64) const;
    void updateRelaySpoolRetry(const QString &packetId, qint64 nextAttemptMs, int attempts);
    void deleteRelaySpool(const QString &packetId);
    int relaySpoolCount() const;
    void trimRelaySpool(int maxEntries);
    void deleteExpiredReliability(qint64 nowMs);

private:
    bool hasColumn(const QString &table, const QString &column) const;
    void addColumnIfMissing(const QString &table, const QString &column, const QString &definition);

    QString connectionName_;
    QSqlDatabase db_;
};

} // namespace ec
