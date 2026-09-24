#include "core/PeerManager.h"
#include "core/Protocol.h"
#include "storage/Database.h"
#include <QCryptographicHash>

namespace ec {

PeerManager::PeerManager(Database &db, QObject *parent) : QObject(parent), db_(db) {
    for (const auto &p : db_.peers()) peers_.insert(p.userId, p);
}

QList<Peer> PeerManager::peers() const { return peers_.values(); }
Peer PeerManager::peer(const QString &userId) const { return peers_.value(userId); }
bool PeerManager::hasPeer(const QString &userId) const { return peers_.contains(userId); }

QString PeerManager::fingerprint(const QByteArray &signPk, const QByteArray &kxPk) {
    const QByteArray hash = QCryptographicHash::hash(QByteArray("ECIdentity-v2") + signPk + kxPk,
                                                     QCryptographicHash::Sha256).toHex().toUpper().left(32);
    QStringList groups;
    for (int i = 0; i < hash.size(); i += 4) groups << QString::fromLatin1(hash.mid(i, 4));
    return groups.join('-');
}

bool PeerManager::updateFromHello(const QString &transportPeerKey,
                                  TransportType transport,
                                  const QJsonObject &hello) {
    if (!protocol::verifyHello(hello)) {
        emit securityWarning(QStringLiteral("HELLO EC non valido: firma Ed25519 rifiutata"));
        return false;
    }
    const QString userId = hello.value("userId").toString();
    if (userId.isEmpty()) return false;
    const QByteArray signPk = QByteArray::fromBase64(hello.value("signingPublicKey").toString().toLatin1());
    const QByteArray kxPk = QByteArray::fromBase64(hello.value("kxPublicKey").toString().toLatin1());

    Peer p = peers_.value(userId);
    if (!p.signingPublicKey.isEmpty() && p.signingPublicKey != signPk) {
        emit securityWarning(QStringLiteral("CHIAVE IDENTITA' CAMBIATA per %1. Connessione rifiutata.")
                             .arg(p.username.isEmpty() ? userId : p.username));
        return false;
    }

    p.userId = userId;
    p.username = hello.value("username").toString();
    p.signingPublicKey = signPk;
    p.kxPublicKey = kxPk;
    p.identityFingerprint = fingerprint(signPk, kxPk);

    const auto caps = hello.value("capabilities").toObject();
    p.connectivity.bluetoothCapable = caps.value("bluetooth").toBool();
    p.connectivity.lanCapable = caps.value("lan").toBool();
    p.connectivity.internetCapable = caps.value("internet").toBool();
    const auto now = QDateTime::currentDateTimeUtc();
    switch (transport) {
    case TransportType::Bluetooth:
        p.bluetoothAddress = transportPeerKey;
        p.connectivity.bluetoothReachable = true;
        p.connectivity.bluetoothLastSeen = now;
        break;
    case TransportType::Lan:
        p.lanHost = transportPeerKey;
        p.connectivity.lanReachable = true;
        p.connectivity.lanLastSeen = now;
        break;
    case TransportType::Internet:
        p.relayDeviceId = transportPeerKey;
        p.connectivity.internetReachable = true;
        p.connectivity.internetLastSeen = now;
        break;
    }
    peers_[userId] = p;
    persist(p);
    emit peerUpdated(p);
    return true;
}

void PeerManager::setReachable(const QString &userId, TransportType transport, bool reachable) {
    if (!peers_.contains(userId)) return;
    auto p = peers_.value(userId);
    const auto now = QDateTime::currentDateTimeUtc();
    switch (transport) {
    case TransportType::Bluetooth: p.connectivity.bluetoothReachable = reachable; if (reachable) p.connectivity.bluetoothLastSeen = now; break;
    case TransportType::Lan: p.connectivity.lanReachable = reachable; if (reachable) p.connectivity.lanLastSeen = now; break;
    case TransportType::Internet: p.connectivity.internetReachable = reachable; if (reachable) p.connectivity.internetLastSeen = now; break;
    }
    peers_[userId] = p;
    emit peerUpdated(p);
}

void PeerManager::persist(const Peer &peer) { db_.upsertPeer(peer); }

} // namespace ec
