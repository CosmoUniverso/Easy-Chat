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
    db.deleteOutboxFor("m1", "bob", "message");
    if (db.outboxCount() != 0) {
        std::cerr << "outbox delete failed\n";
        return 4;
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
        return 5;
    }
    db.updateRelaySpoolRetry("p1", 700, 2);
    if (!db.dueRelaySpool(699).isEmpty() || db.dueRelaySpool(700).size() != 1) {
        std::cerr << "relay spool retry schedule failed\n";
        return 6;
    }
    db.deleteExpiredReliability(10001);
    if (db.relaySpoolCount() != 0) {
        std::cerr << "reliability expiry failed\n";
        return 7;
    }

    std::cout << "EChat persistent reliability self-test: OK\n";
    return 0;
}
