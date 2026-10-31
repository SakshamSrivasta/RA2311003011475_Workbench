#include "TestHarness.h"

#include "core/common/Errors.h"
#include "core/hardware/DeviceManager.h"

using namespace wb;
using namespace wb::hw;

TEST(hw_discovery_finds_three_simulated_device_types) {
    DeviceManager mgr;
    const auto devices = mgr.discover();
    CHECK(devices.size() == 3);
    CHECK(devices[0].info.kind == DeviceKind::Microcontroller);
    CHECK(devices[1].info.kind == DeviceKind::Fpga);
    CHECK(devices[2].info.kind == DeviceKind::JtagAdapter);
    for (const auto& d : devices) {
        CHECK(d.info.simulated);
        CHECK(d.state == LinkState::Disconnected);
        CHECK(!d.capabilities.empty());
    }
}

TEST(hw_connect_read_disconnect) {
    DeviceManager mgr;
    mgr.discover();
    CHECK(mgr.connect("fpga0").state == LinkState::Connected);
    const TelemetryMessage a = mgr.readTelemetry("fpga0");
    const TelemetryMessage b = mgr.readTelemetry("fpga0");
    CHECK(a.simulated);
    CHECK(a.sequence == 1 && b.sequence == 2);
    CHECK(!a.fields.empty());
    CHECK(mgr.disconnect("fpga0").state == LinkState::Disconnected);
    CHECK_THROWS_CODE(mgr.readTelemetry("fpga0"), ErrorCode::DeviceFailure);
}

TEST(hw_jtag_reports_target_idcode) {
    DeviceManager mgr;
    mgr.discover();
    mgr.connect("jtag0");
    bool found = false;
    for (const auto& f : mgr.readTelemetry("jtag0").fields) {
        if (f.name == "idcode") found = f.text.find("0x0362D093") != std::string::npos;
    }
    CHECK(found);
}

TEST(hw_timeout_is_recoverable) {
    DeviceManager mgr;
    mgr.discover();
    mgr.connect("mcu0");
    mgr.injectFault("mcu0", FaultKind::Timeout);
    CHECK_THROWS_CODE(mgr.readTelemetry("mcu0"), ErrorCode::DeviceFailure);
    CHECK(mgr.list()[0].state == LinkState::Faulted);
    CHECK_THROWS_CODE(mgr.readTelemetry("mcu0"), ErrorCode::DeviceFailure);  // stays faulted
    CHECK_THROWS_CODE(mgr.connect("mcu0"), ErrorCode::DeviceFailure);       // must recover, not reconnect
    CHECK(mgr.recover("mcu0") == 1);
    CHECK(mgr.list()[0].state == LinkState::Connected);
    CHECK(mgr.readTelemetry("mcu0").sequence == 1);
}

TEST(hw_link_drop_needs_retry) {
    DeviceManager mgr;
    mgr.discover();
    mgr.connect("jtag0");
    mgr.injectFault("jtag0", FaultKind::LinkDrop);
    CHECK_THROWS_CODE(mgr.readTelemetry("jtag0"), ErrorCode::DeviceFailure);
    CHECK(mgr.recover("jtag0") == 2);
    CHECK(mgr.list()[2].state == LinkState::Connected);
}

TEST(hw_faults_on_one_device_do_not_affect_others) {
    DeviceManager mgr;
    mgr.discover();
    mgr.connect("mcu0");
    mgr.connect("fpga0");
    mgr.injectFault("mcu0", FaultKind::Timeout);
    CHECK_THROWS_CODE(mgr.readTelemetry("mcu0"), ErrorCode::DeviceFailure);
    CHECK(mgr.readTelemetry("fpga0").sequence == 1);
}

TEST(hw_rejects_unknown_ids_and_faults) {
    DeviceManager mgr;
    mgr.discover();
    CHECK_THROWS_CODE(mgr.connect("usb-toaster"), ErrorCode::DeviceFailure);
    CHECK_THROWS_CODE(faultFromString("meteor"), ErrorCode::InvalidParameter);
    CHECK_THROWS_CODE(mgr.injectFault("mcu0", FaultKind::Timeout), ErrorCode::DeviceFailure);  // not connected
}
