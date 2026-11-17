// Host hardware and build description, reported next to benchmark timings.
// Fields that cannot be detected on a platform are left empty or zero and
// shown as "unknown" rather than guessed.
#pragma once

#include <cstdint>
#include <string>

namespace wb::platform {

struct SystemInfo {
    std::string cpuName;
    unsigned physicalCores = 0;
    unsigned logicalCpus = 0;
    std::uint64_t totalRamBytes = 0;
    std::string os;
    std::string compiler;
    std::string buildType;   // e.g. "Release"
    std::string buildFlags;  // optimisation flags passed in by CMake
};

SystemInfo querySystemInfo();

// Highest resident memory of this process so far, in bytes (0 if unknown).
// The solver study runs each configuration in a fresh process so this is a
// clean per-configuration peak.
std::uint64_t peakResidentBytes();

}  // namespace wb::platform
