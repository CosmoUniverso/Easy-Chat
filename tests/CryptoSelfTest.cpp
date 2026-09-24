#include "crypto/CryptoEngine.h"
#include "core/Protocol.h"
#include <QCoreApplication>
#include <iostream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    ec::CryptoEngine crypto;
    const auto alice = crypto.createIdentity("alice");
    const auto bob = crypto.createIdentity("bob");

    ec::Peer bobPeer;
    bobPeer.userId = bob.userId;
    bobPeer.username = bob.username;
    bobPeer.kxPublicKey = bob.kxPublicKey;
    bobPeer.signingPublicKey = bob.signingPublicKey;

    ec::Peer alicePeer;
    alicePeer.userId = alice.userId;
    alicePeer.username = alice.username;
    alicePeer.kxPublicKey = alice.kxPublicKey;
    alicePeer.signingPublicKey = alice.signingPublicKey;

    ec::Message m;
    m.id = "message-test";
    m.conversationId = "conversation-test";
    m.senderId = alice.userId;
    m.text = "EChat crypto self-test";
    m.timestampMs = 1;
    m.conversationMembers = {alice.userId, bob.userId};

    auto envelope = crypto.encryptFor(m, alice, bobPeer);
    ec::Message opened;
    if (!crypto.decryptFrom(envelope, bob, alicePeer, opened) || opened.text != m.text) {
        std::cerr << "decrypt test failed\n";
        return 1;
    }

    envelope.cipherText[0] ^= 1;
    ec::Message tampered;
    if (crypto.decryptFrom(envelope, bob, alicePeer, tampered)) {
        std::cerr << "tamper test failed\n";
        return 2;
    }

    const auto cleanEnvelope = crypto.encryptFor(m, alice, bobPeer);
    const QJsonObject inner = ec::protocol::encryptedMessage(cleanEnvelope);
    auto relay = ec::protocol::relay(alice, bob.userId, inner, ec::TransportPolicy::Auto, 4);
    QJsonObject relayInner;
    if (!ec::protocol::verifyRelay(relay, &relayInner) || relayInner.value("messageId") != m.id) {
        std::cerr << "relay signature test failed\n";
        return 3;
    }
    relay["ttl"] = 2;
    if (!ec::protocol::verifyRelay(relay, nullptr)) {
        std::cerr << "relay ttl forwarding test failed\n";
        return 4;
    }
    relay["targetId"] = QStringLiteral("tampered-target");
    if (ec::protocol::verifyRelay(relay, nullptr)) {
        std::cerr << "relay tamper test failed\n";
        return 5;
    }

    std::cout << "EChat crypto v2 + mesh protocol v3 self-test: OK\n";
    return 0;
}
