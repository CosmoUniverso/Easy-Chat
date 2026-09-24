#include "transport/bluetooth/BluetoothTransport.h"
#include "core/Protocol.h"

#include <QBluetoothLocalDevice>
#include <QUuid>

namespace ec {

BluetoothTransport::BluetoothTransport(QObject *parent)
    : Transport(parent), server_(QBluetoothServiceInfo::RfcommProtocol, this) {
    discovery_.setParent(this);

    connect(&discovery_, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this,
            [this](const QBluetoothDeviceInfo &info) {
                if (!info.address().isNull())
                    emit deviceFound(info.name(), info.address().toString());
            });
    connect(&discovery_, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BluetoothTransport::scanFinished);
    connect(&discovery_, &QBluetoothDeviceDiscoveryAgent::canceled,
            this, &BluetoothTransport::scanFinished);
    connect(&server_, &QBluetoothServer::newConnection, this, [this] {
        while (server_.hasPendingConnections()) {
            if (auto *socket = server_.nextPendingConnection())
                attachSocket(socket);
        }
    });
    connect(&server_, &QBluetoothServer::errorOccurred, this,
            [this](QBluetoothServer::Error) {
                emit statusMessage(QStringLiteral("Errore Bluetooth server: codice %1").arg(static_cast<int>(server_.error())));
            });
}

QBluetoothUuid BluetoothTransport::serviceUuid() {
    return QBluetoothUuid(QUuid(QStringLiteral("7c7d1001-4d38-4c1c-9a75-0e4343000001")));
}

bool BluetoothTransport::localAvailable() const {
    const auto adapters = QBluetoothLocalDevice::allDevices();
    if (adapters.isEmpty()) return false;
    QBluetoothLocalDevice local(adapters.first().address());
    return local.isValid() && local.hostMode() != QBluetoothLocalDevice::HostPoweredOff;
}

bool BluetoothTransport::peerReachable(const Peer &peer) const {
    return !peer.bluetoothAddress.isEmpty() && connectedSocket(peer.bluetoothAddress) != nullptr;
}

void BluetoothTransport::setIdentity(const LocalIdentity &identity) { identity_ = identity; }

void BluetoothTransport::start() {
    if (!localAvailable()) {
        emit statusMessage(QStringLiteral("Bluetooth non disponibile o spento"));
        return;
    }
    if (server_.isListening()) return;

    serviceInfo_ = server_.listen(serviceUuid(), QStringLiteral("EC Easy Chat"));
    if (!server_.isListening()) {
        emit statusMessage(QStringLiteral("Impossibile avviare il servizio Bluetooth EC"));
        return;
    }
    emit statusMessage(QStringLiteral("EC Bluetooth RFCOMM in ascolto"));
}

void BluetoothTransport::stop() {
    discovery_.stop();
    if (serviceInfo_.isValid()) serviceInfo_.unregisterService();
    server_.close();
    const auto copy = sockets_;
    for (auto *socket : copy) socket->disconnectFromService();
}

void BluetoothTransport::scan() {
    if (!localAvailable()) {
        emit statusMessage(QStringLiteral("Bluetooth non disponibile"));
        emit scanFinished();
        return;
    }
    if (discovery_.isActive()) discovery_.stop();
    discovery_.start(QBluetoothDeviceDiscoveryAgent::ClassicMethod);
    emit statusMessage(QStringLiteral("Scansione Bluetooth Classic avviata"));
}

void BluetoothTransport::connectToDevice(const QString &address) {
    if (address.isEmpty()) return;
    if (connectedSocket(address)) return;

    auto *socket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);
    attachSocket(socket);
    socket->connectToService(QBluetoothAddress(address), serviceUuid());
    emit statusMessage(QStringLiteral("Connessione Bluetooth a %1...").arg(address));
}

bool BluetoothTransport::sendFrame(const Peer &peer, const QByteArray &frame) {
    auto *socket = connectedSocket(peer.bluetoothAddress);
    if (!socket) return false;
    return socket->write(frame) == frame.size();
}

QBluetoothSocket *BluetoothTransport::connectedSocket(const QString &address) const {
    for (auto *socket : sockets_) {
        if (socket && socket->state() == QBluetoothSocket::SocketState::ConnectedState &&
            socket->peerAddress().toString().compare(address, Qt::CaseInsensitive) == 0)
            return socket;
    }
    return nullptr;
}

void BluetoothTransport::attachSocket(QBluetoothSocket *socket) {
    if (!socket || sockets_.contains(socket)) return;
    sockets_.insert(socket);

    connect(socket, &QBluetoothSocket::connected, this, [this, socket] {
        const QString address = socket->peerAddress().toString();
        sendHello(socket);
        emit connectionChanged(address, true);
        emit statusMessage(QStringLiteral("Bluetooth connesso: %1").arg(address));
    });

    connect(socket, &QIODevice::readyRead, this, [this, socket] {
        const QString address = socket->peerAddress().toString();
        emit bytesReceived(address, socket->readAll());
    });

    connect(socket, &QBluetoothSocket::disconnected, this, [this, socket] {
        const QString address = socket->peerAddress().toString();
        sockets_.remove(socket);
        emit connectionChanged(address, false);
        emit statusMessage(QStringLiteral("Bluetooth disconnesso: %1").arg(address));
        socket->deleteLater();
    });

    connect(socket, &QBluetoothSocket::errorOccurred, this,
            [this, socket](QBluetoothSocket::SocketError) {
                emit statusMessage(QStringLiteral("Bluetooth: %1").arg(socket->errorString()));
            });

    if (socket->state() == QBluetoothSocket::SocketState::ConnectedState) {
        sendHello(socket);
        emit connectionChanged(socket->peerAddress().toString(), true);
    }
}

void BluetoothTransport::sendHello(QBluetoothSocket *socket) {
    if (identity_.userId.isEmpty() || !socket) return;
    socket->write(protocol::frame(protocol::hello(identity_)));
}

} // namespace ec
