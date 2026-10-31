#include "core/hardware/DeviceManager.h"

#include "core/common/Errors.h"

namespace wb::hw {

std::vector<DeviceSnapshot> DeviceManager::discover() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& d : devices_) d->disconnect();
    devices_.clear();
    devices_.push_back(makeMockMicrocontroller("mcu0", 11));
    devices_.push_back(makeMockFpga("fpga0", 22));
    devices_.push_back(makeMockJtagAdapter("jtag0", 33));

    std::vector<DeviceSnapshot> out;
    for (const auto& d : devices_) out.push_back(snapshot(*d));
    return out;
}

std::vector<DeviceSnapshot> DeviceManager::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DeviceSnapshot> out;
    for (const auto& d : devices_) out.push_back(snapshot(*d));
    return out;
}

DeviceSnapshot DeviceManager::connect(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    DeviceAdapter& d = find(id);
    d.connect();
    return snapshot(d);
}

DeviceSnapshot DeviceManager::disconnect(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    DeviceAdapter& d = find(id);
    d.disconnect();
    return snapshot(d);
}

DeviceSnapshot DeviceManager::injectFault(const std::string& id, FaultKind fault) {
    std::lock_guard<std::mutex> lock(mutex_);
    DeviceAdapter& d = find(id);
    if (d.state() != LinkState::Connected) {
        throw WorkbenchError(ErrorCode::DeviceFailure, "Connect " + id + " before injecting a fault.");
    }
    d.injectFault(fault);
    return snapshot(d);
}

int DeviceManager::recover(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return find(id).recover();
}

TelemetryMessage DeviceManager::readTelemetry(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return find(id).readTelemetry();
}

DeviceAdapter& DeviceManager::find(const std::string& id) const {
    for (const auto& d : devices_) {
        if (d->info().id == id) return *d;
    }
    throw WorkbenchError(ErrorCode::DeviceFailure, "No device '" + id + "'. Run discovery again.");
}

DeviceSnapshot DeviceManager::snapshot(const DeviceAdapter& a) {
    return {a.info(), a.capabilities(), a.state()};
}

}  // namespace wb::hw
