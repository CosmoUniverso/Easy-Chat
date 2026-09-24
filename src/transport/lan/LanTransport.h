#pragma once

#include "transport/Transport.h"
#include "transport/lan/QuicEndpoint.h"

#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

namespace ec {

class LanTransport : public Transport {
    Q_OBJECT
public:
    explicit LanTransport(QObject *parent = nullptr);

    TransportType type() const override { return TransportType::Lan; }
    bool localAvailable() const override;
    bool peerReachable(const Peer &peer) const override;
    void start() override;
    void stop() override;
    bool sendFrame(const Peer &peer, const QByteArray &frame) override;
    QString linkDescription(const Peer &peer) const override;
    int linkCost(const Peer &peer) const override;

    void setIdentity(const LocalIdentity &identity);

signals:
    void connectionChanged(const QString &host, bool connected);

private:
    static constexpr quint16 QuicPort = 45454;
    static constexpr quint16 DiscoveryPort = 45455;
    static constexpr quint16 TcpPort = 45456;

    void announce();
    void readDiscovery();
    void connectTcp(const QString &host, quint16 port);
    void attachTcpSocket(QTcpSocket *socket);
    void sendTcpHello(QTcpSocket *socket);
    void sendQuicHello(const QString &host);
    QTcpSocket *connectedTcpSocket(const QString &host) const;
    bool tcpConnected(const QString &host) const;
    void emitAggregateConnectionState(const QString &host);

    LocalIdentity identity_;
    QuicEndpoint quic_;
    bool quicStarted_ = false;
    QTcpServer server_;
    QUdpSocket discovery_;
    QTimer announceTimer_;
    QSet<QTcpSocket *> tcpSockets_;
    QSet<QString> tcpConnectingHosts_;
};

} // namespace ec
