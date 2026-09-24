#pragma once
#include "transport/Transport.h"

namespace ec {
class RelayTransport : public Transport {
    Q_OBJECT
public:
    using Transport::Transport;
    TransportType type() const override { return TransportType::Internet; }
    bool localAvailable() const override { return false; }
    bool peerReachable(const Peer &) const override { return false; }
    void start() override {}
    void stop() override {}
    bool sendFrame(const Peer &, const QByteArray &) override { return false; }
    int linkCost(const Peer &) const override { return 160; }
};
}
