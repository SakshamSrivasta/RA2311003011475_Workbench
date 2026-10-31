// Launch.exe - starts the workbench's local web server and opens the UI in
// the default browser. Everything is served from 127.0.0.1 only; nothing is
// fetched from the internet.
//
//   Launch.exe                 normal start, opens the browser
//   Launch.exe --no-browser    start the server only (print the URL)
//   Launch.exe --smoke-test    start, run a few API checks, exit 0/1
//   Launch.exe --port 9000     preferred port (falls back if busy)

#include "app/AppContext.h"
#include "app/Platform.h"
#include "app/Routes.h"
#include "core/common/Stopwatch.h"

#include <httplib.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace wb;

namespace {

constexpr const char* kSampleStl = "heatsink_fins_mm.stl";
constexpr const char* kSampleCsv = "heatsink_nodal_SYNTHETIC.csv";

struct Options {
    int port = 8765;
    bool openBrowser = true;
    bool smokeTest = false;
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-browser") o.openBrowser = false;
        else if (a == "--smoke-test") { o.smokeTest = true; o.openBrowser = false; }
        else if (a == "--port" && i + 1 < argc) o.port = std::atoi(argv[++i]);
        else std::cerr << "Ignoring unknown option " << a << '\n';
    }
    return o;
}

// Packaged layout keeps the UI in app/web next to Launch.exe. When running a
// development build from build/, walk up to the source tree instead.
fs::path findFolder(const fs::path& exeDir, const std::vector<fs::path>& relativeCandidates, const char* marker) {
    for (fs::path dir = exeDir; !dir.empty(); dir = dir.parent_path()) {
        for (const auto& rel : relativeCandidates) {
            const fs::path p = dir / rel;
            if (fs::exists(p / marker)) return p;
        }
        if (dir == dir.parent_path()) break;
    }
    return {};
}

int runSmokeTest(int port) {
    httplib::Client cli("127.0.0.1", port);
    cli.set_read_timeout(30, 0);
    int failures = 0;
    auto check = [&](const char* what, const httplib::Result& r, int expected, const char* mustContain = nullptr) {
        const bool ok = r && r->status == expected && (!mustContain || r->body.find(mustContain) != std::string::npos);
        std::cout << (ok ? "  [ok]   " : "  [FAIL] ") << what << " -> " << (r ? std::to_string(r->status) : "no response")
                  << '\n';
        if (!ok) ++failures;
    };
    check("GET  /api/health", cli.Get("/api/health"), 200, "\"ok\"");
    check("GET  / (index.html)", cli.Get("/"), 200, "<title>");
    check("GET  three.js bundle", cli.Get("/vendor/three/three.module.min.js"), 200);
    check("POST thermal solve (defaults)", cli.Post("/api/thermal/solve", "{}", "application/json"), 200,
          "maxMidpointError_C");
    check("POST thermal solve N=0 rejected", cli.Post("/api/thermal/solve", R"({"N":0})", "application/json"), 400);
    check("POST example STL", cli.Post(("/api/geometry/stl?sample=" + std::string(kSampleStl)).c_str(), "", "text/plain"),
          200, "\"triangles\"");
    check("POST example nodal CSV",
          cli.Post(("/api/results/nodal?sample=" + std::string(kSampleCsv)).c_str(), "", "text/plain"), 200, "\"nodes\"");
    check("POST malformed CSV rejected", cli.Post("/api/results/nodal", "id,x\n1,2\n", "text/plain"), 400);
    check("POST hardware discovery", cli.Post("/api/hw/discover", "", "application/json"), 200, "jtag0");
    check("GET  backprop", cli.Get("/api/backprop"), 200, "gradientCheck");
    std::cout << (failures == 0 ? "Smoke test passed\n" : "Smoke test FAILED\n");
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    Stopwatch sinceMain;
    const Options opt = parseArgs(argc, argv);

#if defined(_WIN32)
    SetConsoleTitleA("Unified Engineering Workbench - close this window to stop");
#endif

    app::AppContext ctx;
    const fs::path exeDir = app::executableDir();
    ctx.webRoot = findFolder(exeDir, {"app/web", "src/web"}, "index.html");
    ctx.dataDir = findFolder(exeDir, {"data"}, kSampleStl);
    if (ctx.webRoot.empty()) {
        std::cerr << "Could not find the UI files (app/web/index.html) next to " << exeDir.u8string()
                  << ".\nRe-extract the whole ZIP so Launch.exe keeps its app folder beside it.\n";
#if defined(_WIN32)
        if (!opt.smokeTest) system("pause");
#endif
        return 1;
    }
    if (ctx.dataDir.empty()) ctx.dataDir = exeDir / "data";  // examples just won't be listed

    httplib::Server server;
    server.set_payload_max_length(80u * 1024u * 1024u);  // a bit above the 64 MB reader limit
    server.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        // Telemetry and job polling fire every second; logging them would bury everything else.
        if (req.path.rfind("/api/", 0) != 0 || req.path.find("/telemetry") != std::string::npos ||
            (req.method == "GET" && req.path.rfind("/api/jobs/", 0) == 0)) {
            return;
        }
        std::cout << "[http] " << req.method << ' ' << req.path << " -> " << res.status << std::endl;
    });
    app::registerAllRoutes(server, ctx);
    ctx.requestShutdown = [&server] { server.stop(); };

    int port = -1;
    for (int p = opt.port; p < opt.port + 10 && port < 0; ++p) {
        if (server.bind_to_port("127.0.0.1", p)) port = p;
    }
    if (port < 0) port = server.bind_to_any_port("127.0.0.1");
    if (port <= 0) {
        std::cerr << "Could not open a local port. Another program may be blocking 127.0.0.1.\n";
        return 1;
    }

    std::thread serverThread([&server] { server.listen_after_bind(); });
    server.wait_until_ready();

    const double sinceProcess = app::secondsSinceProcessStart();
    ctx.launchToReady_s = sinceProcess >= 0 ? sinceProcess : sinceMain.elapsedSeconds();
    ctx.launchClock = sinceProcess >= 0 ? "OS process creation time" : "entry of main()";

    const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    std::cout << "Unified Engineering Workbench\n"
              << "  UI:        " << url << "\n"
              << "  UI files:  " << ctx.webRoot.u8string() << "\n"
              << "  Examples:  " << ctx.dataDir.u8string() << "\n"
              << "  Ready in " << ctx.launchToReady_s * 1000.0 << " ms (measured from " << ctx.launchClock << ")\n";

    int exitCode = 0;
    if (opt.smokeTest) {
        exitCode = runSmokeTest(port);
        server.stop();
    } else {
        std::cout << "Close this window (or use 'Quit' in the UI) to stop the workbench.\n" << std::endl;
        if (opt.openBrowser && !app::openBrowser(url)) {
            std::cout << "Could not open a browser automatically - open " << url << " yourself.\n";
        }
    }

    serverThread.join();
    ctx.jobs.shutdown();
    std::cout << "Workbench stopped.\n";
    return exitCode;
}
