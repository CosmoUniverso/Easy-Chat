#pragma once

#include <QList>
#include "core/Types.h"
#include <QSqlDatabase>

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
    void saveDelivery(const QString &messageId, const QString &recipientId, const QString &state, const QString &transport = {});
    void updateDelivery(const QString &messageId, const QString &recipientId, const QString &state);
    bool hasMessage(const QString &messageId) const;
    QList<Message> messages(const QString &conversationId) const;

private:
    bool hasColumn(const QString &table, const QString &column) const;
    void addColumnIfMissing(const QString &table, const QString &column, const QString &definition);

    QString connectionName_;
    QSqlDatabase db_;
};

} // namespace ec
