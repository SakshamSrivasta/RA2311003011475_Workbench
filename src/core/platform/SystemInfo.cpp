#include "core/platform/SystemInfo.h"

#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#  include <sys/types.h>
#elif defined(__linux__)
#  include <fstream>
#  include <set>
#  include <unistd.h>
#endif

// Set by CMake; fall back to "unknown" when built some other way.
#ifndef WB_BUILD_TYPE
#  define WB_BUILD_TYPE "unknown"
#endif
#ifndef WB_BUILD_FLAGS
#  define WB_BUILD_FLAGS "unknown"
#endif

namespace wb::platform {

namespace {

std::string compilerDescription() {
    std::ostringstream s;
#if defined(__clang__)
    s << "Clang " << __clang_major__ << '.' << __clang_minor__ << '.' << __clang_patchlevel__;
#elif defined(__GNUC__)
    s << "GCC " << __GNUC__ << '.' << __GNUC_MINOR__ << '.' << __GNUC_PATCHLEVEL__;
#elif defined(_MSC_VER)
    s << "MSVC " << _MSC_FULL_VER;
#else
    s << "unknown compiler";
#endif
#if defined(__MINGW64__)
    s << " (MinGW-w64)";
#endif
    return s.str();
}

#if defined(_WIN32)

void fillPlatform(SystemInfo& info) {
    info.os = "Windows";

    char name[256] = {};
    DWORD size = sizeof(name);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     "ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &size) == ERROR_SUCCESS) {
        info.cpuName = name;
    }

    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (bytes > 0) {
        std::vector<char> buffer(bytes);
        auto* first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, first, &bytes)) {
            unsigned cores = 0;
            for (DWORD offset = 0; offset < bytes;) {
                auto* entry = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
                if (entry->Relationship == RelationProcessorCore) ++cores;
                offset += entry->Size;
            }
            info.physicalCores = cores;
        }
    }

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) info.totalRamBytes = mem.ullTotalPhys;
}

#elif defined(__APPLE__)

std::string sysctlString(const char* key) {
    std::size_t len = 0;
    if (sysctlbyname(key, nullptr, &len, nullptr, 0) != 0 || len == 0) return {};
    std::string value(len, '\0');
    if (sysctlbyname(key, value.data(), &len, nullptr, 0) != 0) return {};
    value.resize(len > 0 ? len - 1 : 0);  // drop trailing NUL
    return value;
}

template <typename T>
T sysctlNumber(const char* key) {
    T value{};
    std::size_t len = sizeof(value);
    if (sysctlbyname(key, &value, &len, nullptr, 0) != 0) return T{};
    return value;
}

void fillPlatform(SystemInfo& info) {
    info.os = "macOS";
    info.cpuName = sysctlString("machdep.cpu.brand_string");
    info.physicalCores = static_cast<unsigned>(sysctlNumber<int>("hw.physicalcpu"));
    info.totalRamBytes = sysctlNumber<std::uint64_t>("hw.memsize");
}

#elif defined(__linux__)

void fillPlatform(SystemInfo& info) {
    info.os = "Linux";
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    std::set<std::string> coreIds;
    std::string physicalId;
    while (std::getline(cpuinfo, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(0, line.find_last_not_of(" \t", colon - 1) + 1);
        const std::string value = colon + 2 <= line.size() ? line.substr(colon + 2) : "";
        if (key == "model name" && info.cpuName.empty()) info.cpuName = value;
        if (key == "physical id") physicalId = value;
        if (key == "core id") coreIds.insert(physicalId + ":" + value);
    }
    info.physicalCores = static_cast<unsigned>(coreIds.size());
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0) info.totalRamBytes = static_cast<std::uint64_t>(pages) * pageSize;
}

#else

void fillPlatform(SystemInfo& info) { info.os = "unknown"; }

#endif

}  // namespace

SystemInfo querySystemInfo() {
    SystemInfo info;
    info.logicalCpus = std::thread::hardware_concurrency();
    info.compiler = compilerDescription();
    info.buildType = WB_BUILD_TYPE;
    info.buildFlags = WB_BUILD_FLAGS;
    fillPlatform(info);
    return info;
}

}  // namespace wb::platform
