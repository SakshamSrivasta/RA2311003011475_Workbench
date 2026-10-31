// Reader for nodal temperature results exported from ANSYS (or produced in
// the same shape synthetically).
//
// Required header, in this order:   node_id,x_m,y_m,z_m,temperature_C
// Lines starting with '#' are comments (we use them for provenance notes).
// Blank lines are skipped. Coordinates are metres, temperature is Celsius.
#pragma once

#include "core/io/Geometry.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wb::io {

struct NodalRecord {
    std::int64_t nodeId = 0;
    Vec3 position_m;
    double temperature_C = 0.0;
};

struct NodalDataset {
    std::vector<NodalRecord> nodes;
    std::vector<std::string> comments;  // '#' lines, without the '#'
    Bounds bounds_m;
    double minTemperature_C = 0.0;
    double maxTemperature_C = 0.0;
};

inline constexpr const char* kNodalCsvHeader = "node_id,x_m,y_m,z_m,temperature_C";

NodalDataset parseNodalCsv(std::string_view text, const ReadLimits& limits = {});
NodalDataset readNodalCsvFile(const std::string& path, const ReadLimits& limits = {});

}  // namespace wb::io
