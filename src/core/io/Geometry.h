#pragma once

#include <algorithm>
#include <limits>

namespace wb::io {

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

struct Bounds {
    Vec3 min{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
             std::numeric_limits<double>::max()};
    Vec3 max{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
             std::numeric_limits<double>::lowest()};

    void expand(const Vec3& p) {
        min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
        max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
    }
    bool valid() const { return min.x <= max.x; }
};

// Shared size guard for every text file we read. The numbers are what the
// test suite actually exercises (see docs/REPORT.md, "File size limits").
struct ReadLimits {
    std::size_t maxBytes = 64u * 1024u * 1024u;
    std::size_t maxRecords = 1'000'000;  // triangles for STL, rows for CSV
};

}  // namespace wb::io
