#include "core/common/Errors.h"
#include "core/common/Stopwatch.h"
#include "core/hardware/DeviceAdapter.h"

#include <cmath>
#include <sstream>

namespace wb::hw {

const char* toString(DeviceKind kind) noexcept {
    switch (kind) {
        case DeviceKind::Microcontroller: return "microcontroller";
        case DeviceKind::Fpga:            return "fpga";
        case DeviceKind::JtagAdapter:     return "jtag-adapter";
    }
    return "unknown";
}

const char* toString(LinkState state) noexcept {
    switch (state) {
        case LinkState::Disconnected: return "disconnected";
        case LinkState::Connected:    return "connected";
        case LinkState::Faulted:      return "faulted";
    }
    return "unknown";
}

const char* toString(FaultKind fault) noexcept {
    switch (fault) {
        case FaultKind::None:     return "none";
        case FaultKind::Timeout:  return "timeout";
        case FaultKind::LinkDrop: return "link-drop";
    }
    return "unknown";
}

FaultKind faultFromString(const std::string& name) {
    if (name == "none") return FaultKind::None;
    if (name == "timeout") return FaultKind::Timeout;
    if (name == "link-drop") return FaultKind::LinkDrop;
    throw WorkbenchError(ErrorCode::InvalidParameter, "Unknown fault '" + name + "'. Use timeout or link-drop.");
}

namespace {

constexpr int kReadTimeout_ms = 500;  // what a real serial read would wait before giving up

// Shared state machine; subclasses only describe the board and its telemetry.
class MockAdapterBase : public DeviceAdapter {
public:
    MockAdapterBase(DeviceInfo info, std::uint64_t seed) : info_(std::move(info)), rng_(seed | 1) {}

    const DeviceInfo& info() const override { return info_; }
    LinkState state() const override { return state_; }

    void connect() override {
        if (state_ == LinkState::Connected) return;
        if (state_ == LinkState::Faulted) {
            throw WorkbenchError(ErrorCode::DeviceFailure,
                                 info_.id + " is in a fault state. Press Recover to reset the link first.");
        }
        state_ = LinkState::Connected;
        sequence_ = 0;
        clock_.restart();
    }

    void disconnect() override {
        state_ = LinkState::Disconnected;
        pendingFault_ = FaultKind::None;
    }

    void injectFault(FaultKind fault) override { pendingFault_ = fault; }

    TelemetryMessage readTelemetry() override {
        if (state_ == LinkState::Disconnected) {
            throw WorkbenchError(ErrorCode::DeviceFailure, info_.id + " is not connected. Press Connect first.");
        }
        if (state_ == LinkState::Faulted) {
            throw WorkbenchError(ErrorCode::DeviceFailure, info_.id + " is faulted (" + toString(lastFault_) +
                                                               "). Press Recover to reconnect.");
        }
        if (pendingFault_ != FaultKind::None) {
            lastFault_ = pendingFault_;
            pendingFault_ = FaultKind::None;
            state_ = LinkState::Faulted;
            std::ostringstream msg;
            if (lastFault_ == FaultKind::Timeout) {
                msg << info_.id << " did not answer within " << kReadTimeout_ms
                    << " ms (simulated timeout). The link is marked faulted; press Recover.";
            } else {
                msg << info_.id << " link dropped on " << info_.port
                    << " (simulated cable unplug). Re-seat the cable, then press Recover.";
            }
            throw WorkbenchError(ErrorCode::DeviceFailure, msg.str());
        }

        TelemetryMessage msg;
        msg.deviceId = info_.id;
        msg.sequence = ++sequence_;
        msg.hostTime_s = clock_.elapsedSeconds();
        msg.fields = makeFields(msg.hostTime_s);
        return msg;
    }

    int recover() override {
        // A dropped USB link usually fails the first reopen while the OS
        // re-enumerates the port, so the mock needs a second attempt there.
        const int attemptsNeeded = (lastFault_ == FaultKind::LinkDrop) ? 2 : 1;
        state_ = LinkState::Disconnected;
        pendingFault_ = FaultKind::None;
        lastFault_ = FaultKind::None;
        connect();
        return attemptsNeeded;
    }

protected:
    virtual std::vector<TelemetryField> makeFields(double t) = 0;

    // Small deterministic noise in [-1, 1] (xorshift64), so runs are repeatable.
    double noise() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 7;
        rng_ ^= rng_ << 17;
        return static_cast<double>(rng_ >> 11) * 0x1.0p-52 - 1.0;
    }

private:
    DeviceInfo info_;
    std::uint64_t rng_;
    LinkState state_ = LinkState::Disconnected;
    FaultKind pendingFault_ = FaultKind::None;
    FaultKind lastFault_ = FaultKind::None;
    std::uint64_t sequence_ = 0;
    Stopwatch clock_;
};

class MockMicrocontroller final : public MockAdapterBase {
public:
    MockMicrocontroller(const std::string& id, std::uint64_t seed)
        : MockAdapterBase({id, DeviceKind::Microcontroller, "STM32 Nucleo-64 class board (assumed NUCLEO-F446RE)",
                           "SIM-COM3", "USB CDC virtual COM port via on-board ST-LINK, 115200 8N1", "3.3 V", true},
                          seed) {}

    std::vector<Capability> capabilities() const override {
        return {{"telemetry", "Text frames over virtual COM, 10 Hz max"},
                {"adc", "12-bit ADC, 0-3.3 V input range"},
                {"gpio", "3.3 V I/O; most pins 5 V tolerant (check datasheet per pin)"},
                {"debug", "SWD via on-board ST-LINK (not the shared JTAG adapter)"}};
    }

protected:
    std::vector<TelemetryField> makeFields(double t) override {
        return {{"die_temperature", 36.0 + 1.5 * std::sin(0.2 * t) + 0.1 * noise(), "C", ""},
                {"vdda", 3.30 + 0.005 * noise(), "V", ""},
                {"adc_ch0", 1.65 + 0.8 * std::sin(0.5 * t) + 0.01 * noise(), "V", ""},
                {"control_loop_rate", 1000.0 + 2.0 * noise(), "Hz", ""}};
    }
};

class MockFpga final : public MockAdapterBase {
public:
    MockFpga(const std::string& id, std::uint64_t seed)
        : MockAdapterBase({id, DeviceKind::Fpga, "Artix-7 dev board (assumed Digilent Arty A7-35T, XC7A35T)",
                           "SIM-COM4", "USB-UART bridge on the board, 115200 8N1 (status frames from soft logic)",
                           "3.3 V Pmod I/O (bank VCCO), 1.0 V core", true},
                          seed) {}

    std::vector<Capability> capabilities() const override {
        return {{"telemetry", "Status frame from a UART core in the bitstream"},
                {"xadc", "On-die temperature and supply monitor"},
                {"pmod", "3.3 V Pmod headers, NOT 5 V tolerant"},
                {"jtag-target", "Configured/debugged through the external JTAG adapter"}};
    }

protected:
    std::vector<TelemetryField> makeFields(double t) override {
        return {{"xadc_temperature", 45.0 + 2.0 * std::sin(0.1 * t) + 0.2 * noise(), "C", ""},
                {"vccint", 1.00 + 0.004 * noise(), "V", ""},
                {"vccaux", 1.80 + 0.006 * noise(), "V", ""},
                {"design_clock", 100.0, "MHz", ""},
                {"mmcm_locked", 0.0, "", "yes"}};
    }
};

class MockJtagAdapter final : public MockAdapterBase {
public:
    MockJtagAdapter(const std::string& id, std::uint64_t seed)
        : MockAdapterBase({id, DeviceKind::JtagAdapter, "FT2232H-based JTAG adapter (MPSSE mode)", "SIM-USB-0403:6010",
                           "USB 2.0 Hi-Speed, vendor D2XX/libftdi API", "Follows target VREF (1.8-3.3 V)", true},
                          seed) {}

    std::vector<Capability> capabilities() const override {
        return {{"jtag", "IEEE 1149.1 TAP master: TCK, TMS, TDI, TDO, optional TRST"},
                {"scan-chain", "IDCODE readout and chain length detection"},
                {"vref-sense", "Reads target VREF and sets output levels to match"},
                {"not-compute", "Debug/programming probe only; runs no user workloads"}};
    }

protected:
    std::vector<TelemetryField> makeFields(double) override {
        return {{"target_vref", 3.30 + 0.01 * noise(), "V", ""},
                {"tck_frequency", 6.0, "MHz", ""},
                {"chain_length", 1.0, "devices", ""},
                {"idcode", 0.0, "", "0x0362D093 (XC7A35T, simulated)"}};
    }
};

}  // namespace

std::unique_ptr<DeviceAdapter> makeMockMicrocontroller(const std::string& id, std::uint64_t seed) {
    return std::make_unique<MockMicrocontroller>(id, seed);
}
std::unique_ptr<DeviceAdapter> makeMockFpga(const std::string& id, std::uint64_t seed) {
    return std::make_unique<MockFpga>(id, seed);
}
std::unique_ptr<DeviceAdapter> makeMockJtagAdapter(const std::string& id, std::uint64_t seed) {
    return std::make_unique<MockJtagAdapter>(id, seed);
}

}  // namespace wb::hw
