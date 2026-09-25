#include "storage/Database.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;

    ec::Database db(dir.path() + "/reliability.sqlite3");
    db.migrate();

    ec::OutboxEntry out;
    out.id = "message|m1|bob";
    out.targetId = "bob";
    out.payload = R"({"type":"message"})";
    out.policy = ec::TransportPolicy::Auto;
    out.kind = "message";
    out.logicalId = "m1";
    out.createdAtMs = 100;
    out.nextAttemptMs = 100;
    out.expiresAtMs = 10000;
    db.saveOutbox(out);
    if (db.outboxCount() != 1 || db.dueOutbox(100).size() != 1) {
        std::cerr << "outbox insert/due failed\n";
        return 2;
    }
    db.updateOutboxRetry(out.id, 500, 1);
    if (!db.dueOutbox(499).isEmpty() || db.dueOutbox(500).size() != 1) {
        std::cerr << "outbox retry schedule failed\n";
        return 3;
    }
    db.updateOutboxPolicy(out.id, ec::TransportPolicy::LanOnly, 120, 0);
    const auto rebound = db.dueOutbox(120);
    if (rebound.size() != 1 || rebound.first().policy != ec::TransportPolicy::LanOnly || rebound.first().attempts != 0) {
        std::cerr << "outbox policy rebind failed\n";
        return 11;
    }
    if (db.outboxEntries().size() != 1) {
        std::cerr << "outbox enumeration failed\n";
        return 12;
    }
    db.deleteOutboxFor("m1", "bob", "message");
    if (db.outboxCount() != 0) {
        std::cerr << "outbox delete failed\n";
        return 4;
    }

    ec::Message message;
    message.id = "m-delete";
    message.conversationId = "c1";
    message.senderId = "alice";
    message.text = "delete me";
    message.timestampMs = 123;
    db.saveMessage(message, "local");
    db.saveDelivery(message.id, "bob", "pending-retry");
    out.id = "message|m-delete|bob";
    out.logicalId = message.id;
    out.targetId = "bob";
    db.saveOutbox(out);
    db.saveMessageTombstone(message.id, message.senderId, message.conversationId, 456);
    if (db.messageTombstoneSender(message.id) != "alice") {
        std::cerr << "tombstone insert failed\n";
        return 5;
    }
    db.deleteMessage(message.id);
    if (db.hasMessage(message.id) || db.outboxCount() != 0) {
        std::cerr << "message delete cleanup failed\n";
        return 6;
    }

    ec::Conversation group;
    group.id = "group-1";
    group.type = ec::ConversationType::Group;
    group.name = "Group";
    group.memberIds = {"alice", "bob"};
    group.updatedAtMs = 999;
    db.saveConversation(group);
    const auto groups = db.conversations();
    bool foundGroup = false;
    for (const auto &c : groups) {
        if (c.id == group.id && c.updatedAtMs == group.updatedAtMs && c.memberIds.contains("bob")) {
            foundGroup = true;
            break;
        }
    }
    if (!foundGroup) {
        std::cerr << "group metadata persistence failed\n";
        return 7;
    }

    ec::RelaySpoolEntry relay;
    relay.packetId = "p1";
    relay.targetId = "carol";
    relay.payload = R"({"type":"relay"})";
    relay.createdAtMs = 100;
    relay.nextAttemptMs = 100;
    relay.expiresAtMs = 10000;
    db.saveRelaySpool(relay);
    if (db.relaySpoolCount() != 1 || db.dueRelaySpool(100).size() != 1) {
        std::cerr << "relay spool insert/due failed\n";
        return 8;
    }
    db.updateRelaySpoolRetry("p1", 700, 2);
    if (!db.dueRelaySpool(699).isEmpty() || db.dueRelaySpool(700).size() != 1) {
        std::cerr << "relay spool retry schedule failed\n";
        return 9;
    }
    db.deleteExpiredReliability(10001);
    if (db.relaySpoolCount() != 0) {
        std::cerr << "reliability expiry failed\n";
        return 10;
    }

    std::cout << "EChat persistent reliability + deletion/group metadata self-test: OK\n";
    return 0;
}
