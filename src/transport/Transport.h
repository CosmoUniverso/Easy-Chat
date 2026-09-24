#pragma once

#include "core/Types.h"
#include <QObject>

namespace ec {

class Transport : public QObject {
    Q_OBJECT
public:
    explicit Transport(QObject *parent = nullptr) : QObject(parent) {}
    ~Transport() override = default;

    virtual TransportType type() const = 0;
    virtual bool localAvailable() const = 0;
    virtual bool peerReachable(const Peer &peer) const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool sendFrame(const Peer &peer, const QByteArray &frame) = 0;
    virtual QString linkDescription(const Peer &) const { return transportName(type()); }
    virtual int linkCost(const Peer &) const { return 100; }

signals:
    void bytesReceived(const QString &transportPeerKey, const QByteArray &bytes);
    void peerLinkChanged(const QString &peerKey, ec::TransportType type, bool reachable);
    void statusMessage(const QString &message);
};

} // namespace ec

Q_DECLARE_METATYPE(ec::TransportType)
