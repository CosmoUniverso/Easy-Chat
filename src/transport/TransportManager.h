#pragma once

#include <QList>
#include "transport/Transport.h"
#include <QHash>

namespace ec {

class TransportManager : public QObject {
    Q_OBJECT
public:
    explicit TransportManager(QObject *parent = nullptr);

    void addTransport(Transport *transport);
    void startAll();
    void stopAll();

    RouteState routesFor(const Peer &peer) const;
    QList<TransportType> candidates(const Peer &peer, TransportPolicy policy) const;
    bool send(const Peer &peer, const QByteArray &bytes, TransportPolicy policy,
              TransportType *used = nullptr);

signals:
    void bytesReceived(ec::TransportType type, const QString &transportPeerKey, const QByteArray &bytes);
    void statusMessage(const QString &message);

private:
    QHash<int, Transport *> transports_;
};

} // namespace ec
