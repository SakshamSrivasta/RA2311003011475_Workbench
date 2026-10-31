#include "app/HttpHelpers.h"
#include "app/Routes.h"
#include "core/platform/SystemInfo.h"

#include <iostream>
#include <thread>

namespace wb::app {

void registerSystemRoutes(httplib::Server& server, AppContext& ctx) {
    server.Get("/api/health", [&](const httplib::Request&, httplib::Response& res) {
        respondJson(res, [&] { return json{{"status", "ok"}, {"app", "Unified Engineering Workbench"}}; });
    });

    server.Get("/api/system", [&](const httplib::Request&, httplib::Response& res) {
        respondJson(res, [&] {
            const auto s = platform::querySystemInfo();
            return json{{"cpu", s.cpuName.empty() ? "unknown" : s.cpuName},
                        {"physicalCores", s.physicalCores},
                        {"logicalCpus", s.logicalCpus},
                        {"ramBytes", s.totalRamBytes},
                        {"os", s.os},
                        {"compiler", s.compiler},
                        {"buildType", s.buildType},
                        {"buildFlags", s.buildFlags},
                        {"launchToReady_s", ctx.launchToReady_s},
                        {"launchClock", ctx.launchClock},
                        {"uiReady_s", ctx.uiReady_s.load()}};
        });
    });

    // The page reports how long it took to become usable (navigation start ->
    // first data loaded), so the console log has the full launch picture.
    server.Post("/api/ui-ready", [&](const httplib::Request& req, httplib::Response& res) {
        respondJson(res, [&] {
            const double ms = numberField(parseBody(req), "pageReady_ms", 0.0);
            ctx.uiReady_s = ms / 1000.0;
            std::cout << "[ready] browser UI usable " << ms << " ms after page navigation" << std::endl;
            return json{{"ok", true}};
        });
    });

    server.Post("/api/shutdown", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"ok":true})", "application/json");
        // Stop from another thread: calling stop() inside a handler would wait on itself.
        std::thread([&] { ctx.requestShutdown(); }).detach();
    });
}

}  // namespace wb::app
