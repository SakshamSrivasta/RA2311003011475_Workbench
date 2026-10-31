// Everything the HTTP handlers share. Lives for the whole process.
#pragma once

#include "app/JobManager.h"
#include "core/hardware/DeviceManager.h"

#include <atomic>
#include <filesystem>
#include <functional>

namespace wb::app {

struct AppContext {
    std::filesystem::path webRoot;  // index.html, js, css, vendor/
    std::filesystem::path dataDir;  // bundled example files

    JobManager jobs;
    hw::DeviceManager devices;

    double launchToReady_s = 0.0;   // process start -> server listening
    const char* launchClock = "";   // how launchToReady_s was measured
    std::atomic<double> uiReady_s{0.0};  // reported by the browser once the page is usable

    std::function<void()> requestShutdown;
};

}  // namespace wb::app
