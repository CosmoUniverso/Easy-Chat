#pragma once

#include <QHash>
#include <QList>
#include "transport/Transport.h"

namespace ec {

class TransportManager : public QObject {
    Q_OBJECT
public:
    explicit TransportManager(QObject *parent = nullptr);

    void addTransport(Transport *transport);
    void setIdentity(const LocalIdentity &identity);
    void startAll();
    void stopAll();

    RouteState routesFor(const Peer &peer) const;
    QList<TransportType> candidates(const Peer &peer, TransportPolicy policy) const;
    int routeCost(const Peer &peer, TransportType type, TransportPolicy policy = TransportPolicy::Auto) const;
    QString bestRouteDescription(const Peer &peer, TransportPolicy policy, int *cost = nullptr) const;
    bool send(const Peer &peer, const QByteArray &bytes, TransportPolicy policy,
              TransportType *used = nullptr, QString *description = nullptr);

signals:
    void bytesReceived(ec::TransportType type, const QString &transportPeerKey, const QByteArray &bytes);
    void statusMessage(const QString &message);

private:
    bool routeAvailable(const Peer &peer, TransportType type) const;
    int policyBias(TransportType type, TransportPolicy policy) const;
    QHash<int, Transport *> transports_;
};

} // namespace ec
