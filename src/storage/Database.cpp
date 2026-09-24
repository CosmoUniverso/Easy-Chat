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

void Database::saveDelivery(const QString &messageId, const QString &recipientId, const QString &state, const QString &transport) {
    QSqlQuery q(db_); q.prepare("INSERT OR REPLACE INTO deliveries(message_id,recipient_id,state,transport) VALUES(?,?,?,?)");
    q.addBindValue(messageId); q.addBindValue(recipientId); q.addBindValue(state); q.addBindValue(transport);
    if (!q.exec()) throw std::runtime_error(q.lastError().text().toStdString());
}

void Database::updateDelivery(const QString &messageId, const QString &recipientId, const QString &state) {
    QSqlQuery q(db_); q.prepare("UPDATE deliveries SET state=? WHERE message_id=? AND recipient_id=?");
    q.addBindValue(state); q.addBindValue(messageId); q.addBindValue(recipientId); q.exec();
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

} // namespace ec
