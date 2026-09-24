#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace ec {

enum class TransportType { Bluetooth, Lan, Internet };

enum class TransportPolicy {
    Auto,
    BluetoothOnly,
    LanOnly,
    InternetOnly,
    PreferBluetooth,
    PreferLan,
    PreferInternet
};

enum class ConversationType { Direct, Group };

struct LocalIdentity {
    QString userId;
    QString username;
    QByteArray kxPublicKey;       // X25519
    QByteArray kxSecretKey;       // X25519
    QByteArray signingPublicKey;  // Ed25519
    QByteArray signingSecretKey;  // Ed25519
};

struct PeerConnectivity {
    bool bluetoothCapable = false;
    bool bluetoothReachable = false;
    bool lanCapable = false;
    bool lanReachable = false;
    bool internetCapable = false;
    bool internetReachable = false;
    bool meshCapable = false;
    bool quicCapable = false;
    bool tcpFallbackCapable = false;
    QDateTime bluetoothLastSeen;
    QDateTime lanLastSeen;
    QDateTime internetLastSeen;
};

struct Peer {
    QString userId;
    QString username;
    QByteArray kxPublicKey;
    QByteArray signingPublicKey;
    QString identityFingerprint;
    QString bluetoothAddress;
    QString lanHost;
    quint16 lanPort = 0;
    QString relayDeviceId;
    PeerConnectivity connectivity;
};

struct Conversation {
    QString id;
    ConversationType type = ConversationType::Direct;
    QString name;
    QStringList memberIds;
};

struct Message {
    QString id;
    QString conversationId;
    QString senderId;
    QString text;
    qint64 timestampMs = 0;
    ConversationType conversationType = ConversationType::Direct;
    QString conversationName;
    QStringList conversationMembers;
};

struct EncryptedEnvelope {
    int cryptoVersion = 2;
    QString messageId;
    QString conversationId;
    QString senderId;
    QString recipientId;
    QByteArray ephemeralPublicKey; // one-use X25519 public key
    QByteArray nonce;              // XChaCha20 192-bit nonce
    QByteArray cipherText;
    QByteArray signature;          // Ed25519 over envelope metadata+ciphertext
};

struct OutboxEntry {
    QString id;
    QString targetId;
    QByteArray payload;
    TransportPolicy policy = TransportPolicy::Auto;
    QString kind;
    QString logicalId;
    qint64 createdAtMs = 0;
    qint64 nextAttemptMs = 0;
    qint64 expiresAtMs = 0;
    int attempts = 0;
};

struct RelaySpoolEntry {
    QString packetId;
    QString targetId;
    QByteArray payload;
    TransportPolicy policy = TransportPolicy::Auto;
    qint64 createdAtMs = 0;
    qint64 nextAttemptMs = 0;
    qint64 expiresAtMs = 0;
    int attempts = 0;
};

struct RouteState {
    bool bluetooth = false;
    bool lan = false;
    bool internet = false;
};

inline QString transportName(TransportType type) {
    switch (type) {
    case TransportType::Bluetooth: return QStringLiteral("Bluetooth");
    case TransportType::Lan: return QStringLiteral("LAN");
    case TransportType::Internet: return QStringLiteral("Internet");
    }
    return QStringLiteral("Unknown");
}

} // namespace ec
