// Owns the adapters and serialises access to them. HTTP handlers run on a
// thread pool, so every call goes through one mutex; adapter calls are short,
// so contention is not a concern.
#pragma once

#include "core/hardware/DeviceAdapter.h"

#include <mutex>
#include <string>
#include <vector>

namespace wb::hw {

struct DeviceSnapshot {
    DeviceInfo info;
    std::vector<Capability> capabilities;
    LinkState state = LinkState::Disconnected;
};

class DeviceManager {
public:
    // Simulated discovery: always finds one MCU, one FPGA and one JTAG
    // adapter. Re-running it disconnects and replaces the old adapters.
    std::vector<DeviceSnapshot> discover();
    std::vector<DeviceSnapshot> list() const;

    DeviceSnapshot connect(const std::string& id);
    DeviceSnapshot disconnect(const std::string& id);
    DeviceSnapshot injectFault(const std::string& id, FaultKind fault);
    int recover(const std::string& id);
    TelemetryMessage readTelemetry(const std::string& id);

private:
    DeviceAdapter& find(const std::string& id) const;  // caller holds the lock
    static DeviceSnapshot snapshot(const DeviceAdapter& a);

    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<DeviceAdapter>> devices_;
};

}  // namespace wb::hw
