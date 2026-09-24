#include "storage/Database.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <stdexcept>

namespace ec {

static void execOrThrow(QSqlQuery &q, const QString &sql) {
    if (!q.exec(sql)) throw std::runtime_error(q.lastError().text().toStdString());
}

Database::Database(const QString &path)
    : connectionName_(QStringLiteral("ec-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {
    db_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    db_.setDatabaseName(path);
    if (!db_.open()) throw std::runtime_error(db_.lastError().text().toStdString());
}

Database::~Database() {
    const QString name = connectionName_;
    db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
}

bool Database::hasColumn(const QString &table, const QString &column) const {
    QSqlQuery q(db_);
    q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    while (q.next()) if (q.value(1).toString() == column) return true;
    return false;
}

void Database::addColumnIfMissing(const QString &table, const QString &column, const QString &definition) {
    if (hasColumn(table, column)) return;
    QSqlQuery q(db_);
    execOrThrow(q, QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, definition));
}

void Database::migrate() {
    QSqlQuery q(db_);
    execOrThrow(q, "CREATE TABLE IF NOT EXISTS identity (id INTEGER PRIMARY KEY CHECK(id=1), user_id TEXT NOT NULL, username TEXT NOT NULL, public_key BLOB NOT NULL, secret_key BLOB NOT NULL, sign_public_key BLOB, sign_secret_key BLOB)");
    execOrThrow(q, "CREATE TABLE IF NOT EXISTS peers (user_id TEXT PRIMARY KEY, username TEXT NOT NULL, public_key BLOB NOT NULL, sign_public_key BLOB, fingerprint TEXT, bluetooth_address TEXT, lan_host TEXT, lan_port INTEGER, relay_device_id TEXT)");
    execOrThrow(q, "CREATE TABLE IF NOT EXISTS conversations (id TEXT PRIMARY KEY, type INTEGER NOT NULL, name TEXT, member_ids TEXT NOT NULL)");
    execOrThrow(q, "CREATE TABLE IF NOT EXISTS messages (id TEXT PRIMARY KEY, conversation_id TEXT NOT NULL, sender_id TEXT NOT NULL, text TEXT NOT NULL, timestamp_ms INTEGER NOT NULL, state TEXT NOT NULL)");
    execOrThrow(q, "CREATE TABLE IF NOT EXISTS deliveries (message_id TEXT NOT NULL, recipient_id TEXT NOT NULL, state TEXT NOT NULL, transport TEXT, PRIMARY KEY(message_id, recipient_id))");
    execOrThrow(q, "CREATE TABLE IF NOT EXISTS outbox (id TEXT PRIMARY KEY, target_id TEXT NOT NULL, payload BLOB NOT NULL, policy INTEGER NOT NULL, kind TEXT NOT NULL, logical_id TEXT NOT NULL, created_at_ms INTEGER NOT NULL, next_attempt_ms INTEGER NOT NULL, expires_at_ms INTEGER NOT NULL, attempts INTEGER NOT NULL DEFAULT 0)");
    execOrThrow(q, "CREATE INDEX IF NOT EXISTS idx_outbox_due ON outbox(next_attempt_ms)");
    execOrThrow(q, "CREATE TABLE IF NOT EXISTS relay_spool (packet_id TEXT PRIMARY KEY, target_id TEXT NOT NULL, payload BLOB NOT NULL, policy INTEGER NOT NULL, created_at_ms INTEGER NOT NULL, next_attempt_ms INTEGER NOT NULL, expires_at_ms INTEGER NOT NULL, attempts INTEGER NOT NULL DEFAULT 0)");
    execOrThrow(q, "CREATE INDEX IF NOT EXISTS idx_relay_spool_due ON relay_spool(next_attempt_ms)");
    addColumnIfMissing("identity", "sign_public_key", "BLOB");
    addColumnIfMissing("identity", "sign_secret_key", "BLOB");
    addColumnIfMissing("peers", "sign_public_key", "BLOB");
    addColumnIfMissing("peers", "fingerprint", "TEXT");
}

bool Database::hasIdentity() const {
    QSqlQuery q(db_); q.exec("SELECT 1 FROM identity WHERE id=1"); return q.next();
}

LocalIdentity Database::loadIdentity() const {
    QSqlQuery q(db_);
    q.exec("SELECT user_id,username,public_key,secret_key,sign_public_key,sign_secret_key FROM identity WHERE id=1");
    if (!q.next()) return {};
    LocalIdentity i;
    i.userId=q.value(0).toString(); i.username=q.value(1).toString();
    i.kxPublicKey=q.value(2).toByteArray(); i.kxSecretKey=q.value(3).toByteArray();
    i.signingPublicKey=q.value(4).toByteArray(); i.signingSecretKey=q.value(5).toByteArray();
    return i;
}

void Database::saveIdentity(const LocalIdentity &i) {
    QSqlQuery q(db_);
    q.prepare("INSERT OR REPLACE INTO identity(id,user_id,username,public_key,secret_key,sign_public_key,sign_secret_key) VALUES(1,?,?,?,?,?,?)");
    q.addBindValue(i.userId); q.addBindValue(i.username); q.addBindValue(i.kxPublicKey); q.addBindValue(i.kxSecretKey);
    q.addBindValue(i.signingPublicKey); q.addBindValue(i.signingSecretKey);
    if (!q.exec()) throw std::runtime_error(q.lastError().text().toStdString());
}

void Database::upsertPeer(const Peer &p) {
    QSqlQuery q(db_);
    q.prepare("INSERT OR REPLACE INTO peers(user_id,username,public_key,sign_public_key,fingerprint,bluetooth_address,lan_host,lan_port,relay_device_id) VALUES(?,?,?,?,?,?,?,?,?)");
    q.addBindValue(p.userId); q.addBindValue(p.username); q.addBindValue(p.kxPublicKey); q.addBindValue(p.signingPublicKey);
    q.addBindValue(p.identityFingerprint); q.addBindValue(p.bluetoothAddress); q.addBindValue(p.lanHost); q.addBindValue(p.lanPort); q.addBindValue(p.relayDeviceId);
    if (!q.exec()) throw std::runtime_error(q.lastError().text().toStdString());
}

QList<Peer> Database::peers() const {
    QList<Peer> out;
    QSqlQuery q("SELECT user_id,username,public_key,sign_public_key,fingerprint,bluetooth_address,lan_host,lan_port,relay_device_id FROM peers", db_);
    while (q.next()) {
        Peer p;
        p.userId=q.value(0).toString(); p.username=q.value(1).toString(); p.kxPublicKey=q.value(2).toByteArray();
        p.signingPublicKey=q.value(3).toByteArray(); p.identityFingerprint=q.value(4).toString();
        p.bluetoothAddress=q.value(5).toString(); p.lanHost=q.value(6).toString(); p.lanPort=q.value(7).toUInt(); p.relayDeviceId=q.value(8).toString();
        out.append(p);
    }
    return out;
}

void Database::saveConversation(const Conversation &c) {
    QJsonArray a; for (const auto &id : c.memberIds) a.append(id);
    QSqlQuery q(db_); q.prepare("INSERT OR REPLACE INTO conversations(id,type,name,member_ids) VALUES(?,?,?,?)");
    q.addBindValue(c.id); q.addBindValue(static_cast<int>(c.type)); q.addBindValue(c.name);
    q.addBindValue(QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)));
    if (!q.exec()) throw std::runtime_error(q.lastError().text().toStdString());
}

QList<Conversation> Database::conversations() const {
    QList<Conversation> out; QSqlQuery q("SELECT id,type,name,member_ids FROM conversations", db_);
    while (q.next()) {
        Conversation c; c.id=q.value(0).toString(); c.type=static_cast<ConversationType>(q.value(1).toInt()); c.name=q.value(2).toString();
        for (const auto &v : QJsonDocument::fromJson(q.value(3).toString().toUtf8()).array()) c.memberIds << v.toString();
        out << c;
    }
    return out;
}

void Database::saveMessage(const Message &m, const QString &state) {
    QSqlQuery q(db_); q.prepare("INSERT OR IGNORE INTO messages(id,conversation_id,sender_id,text,timestamp_ms,state) VALUES(?,?,?,?,?,?)");
    q.addBindValue(m.id); q.addBindValue(m.conversationId); q.addBindValue(m.senderId); q.addBindValue(m.text); q.addBindValue(m.timestampMs); q.addBindValue(state);
    if (!q.exec()) throw std::runtime_error(q.lastError().text().toStdString());
}

Message Database::messageById(const QString &messageId) const {
    Message m;
    QSqlQuery q(db_);
    q.prepare("SELECT id,conversation_id,sender_id,text,timestamp_ms FROM messages WHERE id=?");
    q.addBindValue(messageId);
    if (!q.exec() || !q.next()) return m;
    m.id=q.value(0).toString(); m.conversationId=q.value(1).toString(); m.senderId=q.value(2).toString();
    m.text=q.value(3).toString(); m.timestampMs=q.value(4).toLongLong();
    return m;
}

void Database::saveDelivery(const QString &messageId, const QString &recipientId, const QString &state, const QString &transport) {
    QSqlQuery q(db_); q.prepare("INSERT OR REPLACE INTO deliveries(message_id,recipient_id,state,transport) VALUES(?,?,?,?)");
    q.addBindValue(messageId); q.addBindValue(recipientId); q.addBindValue(state); q.addBindValue(transport);
    if (!q.exec()) throw std::runtime_error(q.lastError().text().toStdString());
}

void Database::updateDelivery(const QString &messageId, const QString &recipientId, const QString &state, const QString &transport) {
    QSqlQuery q(db_);
    if (transport.isEmpty()) {
        q.prepare("UPDATE deliveries SET state=? WHERE message_id=? AND recipient_id=?");
        q.addBindValue(state); q.addBindValue(messageId); q.addBindValue(recipientId);
    } else {
        q.prepare("UPDATE deliveries SET state=?, transport=? WHERE message_id=? AND recipient_id=?");
        q.addBindValue(state); q.addBindValue(transport); q.addBindValue(messageId); q.addBindValue(recipientId);
    }
    q.exec();
}

QStringList Database::deliveryStates(const QString &messageId) const {
    QStringList states;
    QSqlQuery q(db_); q.prepare("SELECT state FROM deliveries WHERE message_id=? ORDER BY recipient_id");
    q.addBindValue(messageId); q.exec();
    while (q.next()) states << q.value(0).toString();
    return states;
}

QStringList Database::pendingPeerMessageIds(const QString &recipientId, const QString &senderId) const {
    QStringList ids;
    QSqlQuery q(db_);
    q.prepare("SELECT d.message_id FROM deliveries d JOIN messages m ON m.id=d.message_id WHERE d.recipient_id=? AND d.state='pending-peer' AND m.sender_id=? ORDER BY m.timestamp_ms");
    q.addBindValue(recipientId); q.addBindValue(senderId); q.exec();
    while (q.next()) ids << q.value(0).toString();
    return ids;
}

bool Database::hasMessage(const QString &messageId) const {
    QSqlQuery q(db_); q.prepare("SELECT 1 FROM messages WHERE id=?"); q.addBindValue(messageId); q.exec(); return q.next();
}

QList<Message> Database::messages(const QString &conversationId) const {
    QList<Message> out; QSqlQuery q(db_);
    q.prepare("SELECT id,conversation_id,sender_id,text,timestamp_ms FROM messages WHERE conversation_id=? ORDER BY timestamp_ms");
    q.addBindValue(conversationId); q.exec();
    while(q.next()) {
        Message m; m.id=q.value(0).toString(); m.conversationId=q.value(1).toString(); m.senderId=q.value(2).toString();
        m.text=q.value(3).toString(); m.timestampMs=q.value(4).toLongLong(); out << m;
    }
    return out;
}

void Database::saveOutbox(const OutboxEntry &e) {
    QSqlQuery q(db_);
    q.prepare("INSERT OR REPLACE INTO outbox(id,target_id,payload,policy,kind,logical_id,created_at_ms,next_attempt_ms,expires_at_ms,attempts) VALUES(?,?,?,?,?,?,?,?,?,?)");
    q.addBindValue(e.id); q.addBindValue(e.targetId); q.addBindValue(e.payload); q.addBindValue(static_cast<int>(e.policy));
    q.addBindValue(e.kind); q.addBindValue(e.logicalId); q.addBindValue(e.createdAtMs); q.addBindValue(e.nextAttemptMs);
    q.addBindValue(e.expiresAtMs); q.addBindValue(e.attempts);
    if (!q.exec()) throw std::runtime_error(q.lastError().text().toStdString());
}

QList<OutboxEntry> Database::dueOutbox(qint64 nowMs, int limit) const {
    QList<OutboxEntry> out;
    QSqlQuery q(db_);
    q.prepare("SELECT id,target_id,payload,policy,kind,logical_id,created_at_ms,next_attempt_ms,expires_at_ms,attempts FROM outbox WHERE next_attempt_ms<=? ORDER BY next_attempt_ms LIMIT ?");
    q.addBindValue(nowMs); q.addBindValue(limit); q.exec();
    while (q.next()) {
        OutboxEntry e;
        e.id=q.value(0).toString(); e.targetId=q.value(1).toString(); e.payload=q.value(2).toByteArray();
        e.policy=static_cast<TransportPolicy>(q.value(3).toInt()); e.kind=q.value(4).toString(); e.logicalId=q.value(5).toString();
        e.createdAtMs=q.value(6).toLongLong(); e.nextAttemptMs=q.value(7).toLongLong(); e.expiresAtMs=q.value(8).toLongLong(); e.attempts=q.value(9).toInt();
        out << e;
    }
    return out;
}

void Database::updateOutboxRetry(const QString &id, qint64 nextAttemptMs, int attempts) {
    QSqlQuery q(db_); q.prepare("UPDATE outbox SET next_attempt_ms=?, attempts=? WHERE id=?");
    q.addBindValue(nextAttemptMs); q.addBindValue(attempts); q.addBindValue(id); q.exec();
}

void Database::deleteOutbox(const QString &id) {
    QSqlQuery q(db_); q.prepare("DELETE FROM outbox WHERE id=?"); q.addBindValue(id); q.exec();
}

void Database::deleteOutboxFor(const QString &logicalId, const QString &targetId, const QString &kind) {
    QSqlQuery q(db_); q.prepare("DELETE FROM outbox WHERE logical_id=? AND target_id=? AND kind=?");
    q.addBindValue(logicalId); q.addBindValue(targetId); q.addBindValue(kind); q.exec();
}

int Database::outboxCount() const {
    QSqlQuery q("SELECT COUNT(*) FROM outbox", db_); return q.next() ? q.value(0).toInt() : 0;
}

void Database::saveRelaySpool(const RelaySpoolEntry &e) {
    QSqlQuery q(db_);
    q.prepare("INSERT OR REPLACE INTO relay_spool(packet_id,target_id,payload,policy,created_at_ms,next_attempt_ms,expires_at_ms,attempts) VALUES(?,?,?,?,?,?,?,?)");
    q.addBindValue(e.packetId); q.addBindValue(e.targetId); q.addBindValue(e.payload); q.addBindValue(static_cast<int>(e.policy));
    q.addBindValue(e.createdAtMs); q.addBindValue(e.nextAttemptMs); q.addBindValue(e.expiresAtMs); q.addBindValue(e.attempts);
    if (!q.exec()) throw std::runtime_error(q.lastError().text().toStdString());
}

QList<RelaySpoolEntry> Database::dueRelaySpool(qint64 nowMs, int limit) const {
    QList<RelaySpoolEntry> out;
    QSqlQuery q(db_);
    q.prepare("SELECT packet_id,target_id,payload,policy,created_at_ms,next_attempt_ms,expires_at_ms,attempts FROM relay_spool WHERE next_attempt_ms<=? ORDER BY next_attempt_ms LIMIT ?");
    q.addBindValue(nowMs); q.addBindValue(limit); q.exec();
    while (q.next()) {
        RelaySpoolEntry e;
        e.packetId=q.value(0).toString(); e.targetId=q.value(1).toString(); e.payload=q.value(2).toByteArray();
        e.policy=static_cast<TransportPolicy>(q.value(3).toInt()); e.createdAtMs=q.value(4).toLongLong();
        e.nextAttemptMs=q.value(5).toLongLong(); e.expiresAtMs=q.value(6).toLongLong(); e.attempts=q.value(7).toInt();
        out << e;
    }
    return out;
}

void Database::updateRelaySpoolRetry(const QString &packetId, qint64 nextAttemptMs, int attempts) {
    QSqlQuery q(db_); q.prepare("UPDATE relay_spool SET next_attempt_ms=?, attempts=? WHERE packet_id=?");
    q.addBindValue(nextAttemptMs); q.addBindValue(attempts); q.addBindValue(packetId); q.exec();
}

void Database::deleteRelaySpool(const QString &packetId) {
    QSqlQuery q(db_); q.prepare("DELETE FROM relay_spool WHERE packet_id=?"); q.addBindValue(packetId); q.exec();
}

int Database::relaySpoolCount() const {
    QSqlQuery q("SELECT COUNT(*) FROM relay_spool", db_); return q.next() ? q.value(0).toInt() : 0;
}

void Database::trimRelaySpool(int maxEntries) {
    if (maxEntries < 1) return;
    QSqlQuery q(db_);
    q.prepare("DELETE FROM relay_spool WHERE packet_id IN (SELECT packet_id FROM relay_spool ORDER BY created_at_ms DESC LIMIT -1 OFFSET ?)");
    q.addBindValue(maxEntries); q.exec();
}

void Database::deleteExpiredReliability(qint64 nowMs) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM outbox WHERE expires_at_ms<=?"); q.addBindValue(nowMs); q.exec();
    q.prepare("DELETE FROM relay_spool WHERE expires_at_ms<=?"); q.addBindValue(nowMs); q.exec();
}

} // namespace ec
