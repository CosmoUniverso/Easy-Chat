#pragma once
#include "transport/Transport.h"

namespace ec {
class LanTransport : public Transport {
    Q_OBJECT
public:
    using Transport::Transport;
    TransportType type() const override { return TransportType::Lan; }
    bool localAvailable() const override;
    bool peerReachable(const Peer &peer) const override { return peer.connectivity.lanReachable; }
    void start() override {}
    void stop() override {}
    bool sendFrame(const Peer &, const QByteArray &) override { return false; }
};
}
