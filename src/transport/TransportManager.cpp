#include "transport/TransportManager.h"

namespace ec {

TransportManager::TransportManager(QObject *parent) : QObject(parent) {}

void TransportManager::addTransport(Transport *transport) {
    transports_[static_cast<int>(transport->type())] = transport;
    connect(transport, &Transport::bytesReceived, this,
            [this, transport](const QString &key, const QByteArray &bytes) {
                emit bytesReceived(transport->type(), key, bytes);
            });
    connect(transport, &Transport::statusMessage, this, &TransportManager::statusMessage);
}

void TransportManager::startAll() { for (auto *t : transports_) t->start(); }
void TransportManager::stopAll() { for (auto *t : transports_) t->stop(); }

RouteState TransportManager::routesFor(const Peer &peer) const {
    RouteState r;
    for (auto *t : transports_) {
        bool peerCapable = false;
        switch (t->type()) {
        case TransportType::Bluetooth: peerCapable = peer.connectivity.bluetoothCapable; break;
        case TransportType::Lan: peerCapable = peer.connectivity.lanCapable; break;
        case TransportType::Internet: peerCapable = peer.connectivity.internetCapable; break;
        }
        if (!peerCapable || !t->localAvailable() || !t->peerReachable(peer)) continue;
        switch (t->type()) {
        case TransportType::Bluetooth: r.bluetooth = true; break;
        case TransportType::Lan: r.lan = true; break;
        case TransportType::Internet: r.internet = true; break;
        }
    }
    return r;
}

QList<TransportType> TransportManager::candidates(const Peer &peer, TransportPolicy policy) const {
    const auto r = routesFor(peer);
    auto ok = [&r](TransportType t) {
        if (t == TransportType::Bluetooth) return r.bluetooth;
        if (t == TransportType::Lan) return r.lan;
        return r.internet;
    };

    QList<TransportType> order;
    switch (policy) {
    case TransportPolicy::BluetoothOnly: order = {TransportType::Bluetooth}; break;
    case TransportPolicy::LanOnly: order = {TransportType::Lan}; break;
    case TransportPolicy::InternetOnly: order = {TransportType::Internet}; break;
    case TransportPolicy::PreferLan: order = {TransportType::Lan, TransportType::Bluetooth, TransportType::Internet}; break;
    case TransportPolicy::PreferInternet: order = {TransportType::Internet, TransportType::Bluetooth, TransportType::Lan}; break;
    case TransportPolicy::PreferBluetooth:
    case TransportPolicy::Auto:
        order = {TransportType::Bluetooth, TransportType::Lan, TransportType::Internet};
        break;
    }

    QList<TransportType> result;
    for (auto t : order) if (ok(t)) result << t;
    return result;
}

bool TransportManager::send(const Peer &peer, const QByteArray &bytes, TransportPolicy policy,
                            TransportType *used) {
    for (auto type : candidates(peer, policy)) {
        auto *t = transports_.value(static_cast<int>(type), nullptr);
        if (t && t->sendFrame(peer, bytes)) {
            if (used) *used = type;
            return true;
        }
    }
    return false;
}

} // namespace ec
