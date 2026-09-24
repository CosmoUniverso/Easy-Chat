#include "transport/TransportManager.h"

#include <algorithm>
#include <QtGlobal>

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

bool TransportManager::routeAvailable(const Peer &peer, TransportType type) const {
    auto *t = transports_.value(static_cast<int>(type), nullptr);
    if (!t || !t->localAvailable() || !t->peerReachable(peer)) return false;
    switch (type) {
    case TransportType::Bluetooth: return peer.connectivity.bluetoothCapable;
    case TransportType::Lan: return peer.connectivity.lanCapable;
    case TransportType::Internet: return peer.connectivity.internetCapable;
    }
    return false;
}

RouteState TransportManager::routesFor(const Peer &peer) const {
    RouteState r;
    r.bluetooth = routeAvailable(peer, TransportType::Bluetooth);
    r.lan = routeAvailable(peer, TransportType::Lan);
    r.internet = routeAvailable(peer, TransportType::Internet);
    return r;
}

int TransportManager::policyBias(TransportType type, TransportPolicy policy) const {
    switch (policy) {
    case TransportPolicy::PreferBluetooth: return type == TransportType::Bluetooth ? -500 : 0;
    case TransportPolicy::PreferLan: return type == TransportType::Lan ? -500 : 0;
    case TransportPolicy::PreferInternet: return type == TransportType::Internet ? -500 : 0;
    default: return 0;
    }
}

int TransportManager::routeCost(const Peer &peer, TransportType type, TransportPolicy policy) const {
    if (!routeAvailable(peer, type)) return 1000000;
    auto *t = transports_.value(static_cast<int>(type), nullptr);
    return qMax(0, t->linkCost(peer) + policyBias(type, policy));
}

QList<TransportType> TransportManager::candidates(const Peer &peer, TransportPolicy policy) const {
    QList<TransportType> allowed;
    switch (policy) {
    case TransportPolicy::BluetoothOnly: allowed = {TransportType::Bluetooth}; break;
    case TransportPolicy::LanOnly: allowed = {TransportType::Lan}; break;
    case TransportPolicy::InternetOnly: allowed = {TransportType::Internet}; break;
    default: allowed = {TransportType::Bluetooth, TransportType::Lan, TransportType::Internet}; break;
    }

    QList<TransportType> result;
    for (const auto type : allowed) if (routeAvailable(peer, type)) result << type;
    std::stable_sort(result.begin(), result.end(), [this, &peer, policy](TransportType a, TransportType b) {
        return routeCost(peer, a, policy) < routeCost(peer, b, policy);
    });
    return result;
}

QString TransportManager::bestRouteDescription(const Peer &peer, TransportPolicy policy, int *cost) const {
    const auto list = candidates(peer, policy);
    if (list.isEmpty()) {
        if (cost) *cost = -1;
        return {};
    }
    const auto type = list.first();
    if (cost) *cost = routeCost(peer, type, policy);
    auto *t = transports_.value(static_cast<int>(type), nullptr);
    return t ? t->linkDescription(peer) : transportName(type);
}

bool TransportManager::send(const Peer &peer, const QByteArray &bytes, TransportPolicy policy,
                            TransportType *used, QString *description) {
    for (auto type : candidates(peer, policy)) {
        auto *t = transports_.value(static_cast<int>(type), nullptr);
        if (t && t->sendFrame(peer, bytes)) {
            if (used) *used = type;
            if (description) *description = t->linkDescription(peer);
            return true;
        }
    }
    return false;
}

} // namespace ec
