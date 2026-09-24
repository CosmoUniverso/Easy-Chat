#include "transport/lan/LanTransport.h"
#include <QNetworkInterface>

namespace ec {
bool LanTransport::localAvailable() const {
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        const auto f = iface.flags();
        if (f.testFlag(QNetworkInterface::IsUp) && f.testFlag(QNetworkInterface::IsRunning) &&
            !f.testFlag(QNetworkInterface::IsLoopBack))
            return true;
    }
    return false;
}
}
