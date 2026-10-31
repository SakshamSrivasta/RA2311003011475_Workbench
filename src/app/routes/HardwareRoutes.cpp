#include "app/HttpHelpers.h"
#include "app/Routes.h"

#include <iostream>

namespace wb::app {

namespace {

json toJson(const hw::DeviceSnapshot& d) {
    json caps = json::array();
    for (const auto& c : d.capabilities) caps.push_back({{"name", c.name}, {"detail", c.detail}});
    return {{"id", d.info.id},
            {"kind", hw::toString(d.info.kind)},
            {"model", d.info.model},
            {"port", d.info.port},
            {"transport", d.info.transport},
            {"ioVoltage", d.info.ioVoltage},
            {"simulated", d.info.simulated},
            {"state", hw::toString(d.state)},
            {"capabilities", caps}};
}

json toJson(const std::vector<hw::DeviceSnapshot>& list) {
    json out = json::array();
    for (const auto& d : list) out.push_back(toJson(d));
    return out;
}

}  // namespace

void registerHardwareRoutes(httplib::Server& server, AppContext& ctx) {
    server.Get("/api/hw/devices", [&](const httplib::Request&, httplib::Response& res) {
        respondJson(res, [&] { return toJson(ctx.devices.list()); });
    });

    server.Post("/api/hw/discover", [&](const httplib::Request&, httplib::Response& res) {
        respondJson(res, [&] {
            std::cout << "[hw] discovery (simulated)" << std::endl;
            return toJson(ctx.devices.discover());
        });
    });

    server.Post(R"(/api/hw/([\w-]+)/connect)", [&](const httplib::Request& req, httplib::Response& res) {
        respondJson(res, [&] { return toJson(ctx.devices.connect(req.matches[1])); });
    });

    server.Post(R"(/api/hw/([\w-]+)/disconnect)", [&](const httplib::Request& req, httplib::Response& res) {
        respondJson(res, [&] { return toJson(ctx.devices.disconnect(req.matches[1])); });
    });

    server.Post(R"(/api/hw/([\w-]+)/fault)", [&](const httplib::Request& req, httplib::Response& res) {
        respondJson(res, [&] {
            const json body = parseBody(req);
            const std::string kind = body.value("kind", "timeout");
            std::cout << "[hw] injecting " << kind << " on " << req.matches[1] << std::endl;
            return toJson(ctx.devices.injectFault(req.matches[1], hw::faultFromString(kind)));
        });
    });

    server.Post(R"(/api/hw/([\w-]+)/recover)", [&](const httplib::Request& req, httplib::Response& res) {
        respondJson(res, [&] {
            const int attempts = ctx.devices.recover(req.matches[1]);
            std::cout << "[hw] " << req.matches[1] << " recovered after " << attempts << " attempt(s)" << std::endl;
            return json{{"attempts", attempts}, {"devices", toJson(ctx.devices.list())}};
        });
    });

    server.Get(R"(/api/hw/([\w-]+)/telemetry)", [&](const httplib::Request& req, httplib::Response& res) {
        respondJson(res, [&] {
            const hw::TelemetryMessage m = ctx.devices.readTelemetry(req.matches[1]);
            json fields = json::array();
            for (const auto& f : m.fields) {
                json jf = {{"name", f.name}, {"unit", f.unit}};
                if (f.text.empty()) jf["value"] = f.value;
                else jf["text"] = f.text;
                fields.push_back(jf);
            }
            return json{{"deviceId", m.deviceId},
                        {"sequence", m.sequence},
                        {"time_s", m.hostTime_s},
                        {"simulated", m.simulated},
                        {"fields", fields}};
        });
    });
}

}  // namespace wb::app
