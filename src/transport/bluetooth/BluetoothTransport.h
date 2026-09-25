#pragma once

#include "transport/Transport.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothServer>
#include <QBluetoothServiceInfo>
#include <QBluetoothSocket>
#include <QHash>
#include <QPointer>
#include <QSet>

namespace ec {

class BluetoothTransport : public Transport {
    Q_OBJECT
public:
    explicit BluetoothTransport(QObject *parent = nullptr);

    TransportType type() const override { return TransportType::Bluetooth; }
    bool localAvailable() const override;
    bool peerReachable(const Peer &peer) const override;
    void start() override;
    void stop() override;
    bool sendFrame(const Peer &peer, const QByteArray &frame) override;
    QString linkDescription(const Peer &) const override { return QStringLiteral("Bluetooth/RFCOMM"); }
    int linkCost(const Peer &) const override { return 80; }

    void setIdentity(const LocalIdentity &identity) override;
    void scan();
    void connectToDevice(const QString &address);

signals:
    void deviceFound(const QString &name, const QString &address);
    void scanFinished();
    void connectionChanged(const QString &address, bool connected);

private:
    static QBluetoothUuid serviceUuid();
    void attachSocket(QBluetoothSocket *socket);
    void sendHello(QBluetoothSocket *socket);
    QBluetoothSocket *connectedSocket(const QString &address) const;

    LocalIdentity identity_;
    QBluetoothDeviceDiscoveryAgent discovery_;
    QBluetoothServer server_;
    QBluetoothServiceInfo serviceInfo_;
    QSet<QBluetoothSocket *> sockets_;
    QHash<QString, QList<QByteArray>> pending_;
};

} // namespace ec
