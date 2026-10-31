// Host-side adapter interface for the three bench devices, plus SIMULATED
// implementations. No real USB/serial/JTAG traffic happens anywhere in this
// module; every value is generated in software and labelled as such.
//
// A real driver would implement the same interface on top of a serial port
// (MCU), the FPGA board's UART bridge, or the JTAG adapter's vendor library,
// and the UI would not need to change.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wb::hw {

enum class DeviceKind { Microcontroller, Fpga, JtagAdapter };
enum class LinkState { Disconnected, Connected, Faulted };
enum class FaultKind { None, Timeout, LinkDrop };

const char* toString(DeviceKind kind) noexcept;
const char* toString(LinkState state) noexcept;
const char* toString(FaultKind fault) noexcept;
FaultKind faultFromString(const std::string& name);  // throws on unknown names

struct DeviceInfo {
    std::string id;         // stable id used by the UI, e.g. "mcu0"
    DeviceKind kind = DeviceKind::Microcontroller;
    std::string model;      // the board we assume for pinout/voltage notes
    std::string port;       // simulated host port name
    std::string transport;  // how the host would talk to it
    std::string ioVoltage;  // logic level on the header we use
    bool simulated = true;
};

struct Capability {
    std::string name;
    std::string detail;
};

struct TelemetryField {
    std::string name;
    double value = 0.0;
    std::string unit;
    std::string text;  // set instead of value for non-numeric fields (e.g. IDCODE)
};

struct TelemetryMessage {
    std::string deviceId;
    std::uint64_t sequence = 0;
    double hostTime_s = 0.0;  // seconds since the adapter connected
    bool simulated = true;
    std::vector<TelemetryField> fields;
};

class DeviceAdapter {
public:
    virtual ~DeviceAdapter() = default;

    virtual const DeviceInfo& info() const = 0;
    virtual std::vector<Capability> capabilities() const = 0;
    virtual LinkState state() const = 0;

    virtual void connect() = 0;      // throws WorkbenchError(DeviceFailure) on failure
    virtual void disconnect() = 0;   // always succeeds
    virtual TelemetryMessage readTelemetry() = 0;  // throws on timeout / dropped link

    // Simulation hooks so the UI can demonstrate failure handling.
    virtual void injectFault(FaultKind fault) = 0;
    // Disconnect, clear the fault, reconnect. Returns the number of attempts used.
    virtual int recover() = 0;
};

std::unique_ptr<DeviceAdapter> makeMockMicrocontroller(const std::string& id, std::uint64_t seed);
std::unique_ptr<DeviceAdapter> makeMockFpga(const std::string& id, std::uint64_t seed);
std::unique_ptr<DeviceAdapter> makeMockJtagAdapter(const std::string& id, std::uint64_t seed);

}  // namespace wb::hw
