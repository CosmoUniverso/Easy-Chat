#include "transport/lan/LanTransport.h"
#include "core/Protocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QtGlobal>

namespace ec {

LanTransport::LanTransport(QObject *parent)
    : Transport(parent), quic_(this) {
    connect(&server_, &QTcpServer::newConnection, this, [this] {
        while (server_.hasPendingConnections()) {
            if (auto *socket = server_.nextPendingConnection()) attachTcpSocket(socket);
        }
    });
    connect(&server_, &QTcpServer::acceptError, this, [this](QAbstractSocket::SocketError) {
        emit statusMessage(QStringLiteral("LAN TCP server: %1").arg(server_.errorString()));
    });

    connect(&discovery_, &QUdpSocket::readyRead, this, &LanTransport::readDiscovery);
    announceTimer_.setInterval(3000);
    connect(&announceTimer_, &QTimer::timeout, this, &LanTransport::announce);

    connect(&quic_, &QuicEndpoint::bytesReceived, this,
            [this](const QString &host, const QByteArray &bytes) {
                emit bytesReceived(QStringLiteral("quic|") + host, bytes);
            });
    connect(&quic_, &QuicEndpoint::statusMessage, this, &LanTransport::statusMessage);
    connect(&quic_, &QuicEndpoint::connectionChanged, this, [this](const QString &host, bool connected) {
        if (connected) sendQuicHello(host);
        emitAggregateConnectionState(host);
    });
}

bool LanTransport::localAvailable() const {
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) ||
            !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack)) continue;
        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.ip().isLoopback())
                return true;
        }
    }
    return false;
}

bool LanTransport::peerReachable(const Peer &peer) const {
    if (peer.lanHost.isEmpty()) return false;
    return quic_.connected(peer.lanHost) || tcpConnected(peer.lanHost);
}

QString LanTransport::linkDescription(const Peer &peer) const {
    if (peer.lanHost.isEmpty()) return QStringLiteral("LAN");
    if (quic_.connected(peer.lanHost)) {
        const int rtt = quic_.rttMs(peer.lanHost);
        return rtt >= 0 ? QStringLiteral("LAN/QUIC · %1 ms").arg(rtt)
                        : QStringLiteral("LAN/QUIC");
    }
    if (tcpConnected(peer.lanHost)) return QStringLiteral("LAN/TCP fallback");
    return QStringLiteral("LAN");
}

int LanTransport::linkCost(const Peer &peer) const {
    if (peer.lanHost.isEmpty()) return 1000;
    if (quic_.connected(peer.lanHost)) {
        const int rtt = quic_.rttMs(peer.lanHost);
        return rtt >= 0 ? qBound(15, 20 + rtt, 400) : 40;
    }
    if (tcpConnected(peer.lanHost)) return 70;
    return 1000;
}

void LanTransport::setIdentity(const LocalIdentity &identity) { identity_ = identity; }

void LanTransport::start() {
    if (!localAvailable()) {
        emit statusMessage(QStringLiteral("LAN non disponibile"));
        return;
    }

    quicStarted_ = quic_.start(QuicPort);

    if (!server_.isListening()) {
        if (!server_.listen(QHostAddress::AnyIPv4, TcpPort)) {
            emit statusMessage(QStringLiteral("Impossibile aprire EChat LAN TCP/%1: %2")
                               .arg(TcpPort).arg(server_.errorString()));
        }
    }

    if (discovery_.state() == QAbstractSocket::UnconnectedState) {
        if (!discovery_.bind(QHostAddress::AnyIPv4, DiscoveryPort,
                             QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            emit statusMessage(QStringLiteral("LAN discovery UDP/%1 non disponibile: %2")
                               .arg(DiscoveryPort).arg(discovery_.errorString()));
        }
    }

    announceTimer_.start();
    announce();
    emit statusMessage(QStringLiteral("EChat LAN: %1 · TCP fallback/%2 · discovery UDP/%3")
                       .arg(quicStarted_ ? QStringLiteral("QUIC/UDP 45454") : QStringLiteral("QUIC non disponibile"))
                       .arg(TcpPort).arg(DiscoveryPort));
}

void LanTransport::stop() {
    announceTimer_.stop();
    discovery_.close();
    quic_.stop();
    quicStarted_ = false;
    server_.close();
    const auto copy = tcpSockets_;
    for (auto *socket : copy) socket->disconnectFromHost();
    tcpSockets_.clear();
    tcpConnectingHosts_.clear();
}

bool LanTransport::sendFrame(const Peer &peer, const QByteArray &frame) {
    if (peer.lanHost.isEmpty()) return false;
    if (quic_.connected(peer.lanHost) && quic_.send(peer.lanHost, frame)) return true;
    auto *socket = connectedTcpSocket(peer.lanHost);
    if (!socket) return false;
    return socket->write(frame) == frame.size();
}

void LanTransport::announce() {
    if (identity_.userId.isEmpty() || discovery_.state() != QAbstractSocket::BoundState) return;

    const QJsonObject object{
        {"type", "ec-lan-discovery"},
        {"protocol", protocol::ProtocolVersion},
        {"userId", identity_.userId},
        {"quic", quicStarted_},
        {"quicPort", static_cast<int>(QuicPort)},
        {"tcpPort", static_cast<int>(TcpPort)}
    };
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);

    QSet<QString> sent;
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) ||
            !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack)) continue;
        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol || entry.broadcast().isNull()) continue;
            const QString key = entry.broadcast().toString();
            if (sent.contains(key)) continue;
            discovery_.writeDatagram(payload, entry.broadcast(), DiscoveryPort);
            sent.insert(key);
        }
    }
    discovery_.writeDatagram(payload, QHostAddress::Broadcast, DiscoveryPort);
}

void LanTransport::readDiscovery() {
    while (discovery_.hasPendingDatagrams()) {
        const auto datagram = discovery_.receiveDatagram();
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(datagram.data(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) continue;
        const auto object = doc.object();
        if (object.value("type").toString() != QStringLiteral("ec-lan-discovery") ||
            object.value("protocol").toInt() != protocol::ProtocolVersion) continue;
        if (object.value("userId").toString() == identity_.userId) continue;

        const QString host = datagram.senderAddress().toString();
        if (host.isEmpty()) continue;

        if (quicStarted_ && object.value("quic").toBool()) {
            const quint16 quicPort = static_cast<quint16>(object.value("quicPort").toInt());
            if (quicPort) quic_.connectToHost(host, quicPort);
        }

        const quint16 tcpPort = static_cast<quint16>(object.value("tcpPort").toInt());
        if (tcpPort) connectTcp(host, tcpPort);
    }
}

void LanTransport::connectTcp(const QString &host, quint16 port) {
    if (host.isEmpty() || tcpConnected(host) || tcpConnectingHosts_.contains(host)) return;
    tcpConnectingHosts_.insert(host);
    auto *socket = new QTcpSocket(this);
    socket->setProperty("ecTargetHost", host);
    attachTcpSocket(socket);
    socket->connectToHost(host, port);
}

void LanTransport::attachTcpSocket(QTcpSocket *socket) {
    if (!socket || tcpSockets_.contains(socket)) return;
    tcpSockets_.insert(socket);

    connect(socket, &QTcpSocket::connected, this, [this, socket] {
        const QString host = socket->peerAddress().toString();
        tcpConnectingHosts_.remove(socket->property("ecTargetHost").toString());
        sendTcpHello(socket);
        emitAggregateConnectionState(host);
        emit statusMessage(QStringLiteral("LAN TCP fallback connesso: %1").arg(host));
    });

    connect(socket, &QIODevice::readyRead, this, [this, socket] {
        emit bytesReceived(QStringLiteral("tcp|") + socket->peerAddress().toString(), socket->readAll());
    });

    connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
        const QString host = socket->peerAddress().toString();
        tcpConnectingHosts_.remove(socket->property("ecTargetHost").toString());
        tcpSockets_.remove(socket);
        emitAggregateConnectionState(host);
        socket->deleteLater();
    });

    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        tcpConnectingHosts_.remove(socket->property("ecTargetHost").toString());
        emit statusMessage(QStringLiteral("LAN TCP: %1").arg(socket->errorString()));
    });

    if (socket->state() == QAbstractSocket::ConnectedState) {
        sendTcpHello(socket);
        emitAggregateConnectionState(socket->peerAddress().toString());
    }
}

void LanTransport::sendTcpHello(QTcpSocket *socket) {
    if (!socket || identity_.userId.isEmpty()) return;
    socket->write(protocol::frame(protocol::hello(identity_)));
}

void LanTransport::sendQuicHello(const QString &host) {
    if (identity_.userId.isEmpty() || host.isEmpty()) return;
    quic_.send(host, protocol::frame(protocol::hello(identity_)));
    emit statusMessage(QStringLiteral("LAN QUIC connesso: %1").arg(host));
}

QTcpSocket *LanTransport::connectedTcpSocket(const QString &host) const {
    for (auto *socket : tcpSockets_) {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) continue;
        if (socket->peerAddress().toString() == host) return socket;
    }
    return nullptr;
}

bool LanTransport::tcpConnected(const QString &host) const {
    return connectedTcpSocket(host) != nullptr;
}

void LanTransport::emitAggregateConnectionState(const QString &host) {
    if (host.isEmpty()) return;
    emit connectionChanged(host, quic_.connected(host) || tcpConnected(host));
}

} // namespace ec
