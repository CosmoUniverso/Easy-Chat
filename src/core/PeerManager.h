#pragma once

#include <QList>
#include "core/Types.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>

namespace ec {
class Database;

class PeerManager : public QObject {
    Q_OBJECT
public:
    explicit PeerManager(Database &db, QObject *parent = nullptr);

    QList<Peer> peers() const;
    Peer peer(const QString &userId) const;
    bool hasPeer(const QString &userId) const;

    bool updateFromHello(const QString &transportPeerKey,
                         TransportType transport,
                         const QJsonObject &hello);
    void setReachable(const QString &userId, TransportType transport, bool reachable);

signals:
    void peerUpdated(const ec::Peer &peer);
    void securityWarning(const QString &message);

private:
    void persist(const Peer &peer);
    static QString fingerprint(const QByteArray &signingPublicKey, const QByteArray &kxPublicKey);

    Database &db_;
    QHash<QString, Peer> peers_;
};

} // namespace ec

Q_DECLARE_METATYPE(ec::Peer)
