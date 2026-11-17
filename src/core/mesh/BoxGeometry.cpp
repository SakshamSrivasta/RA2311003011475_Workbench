#include "core/mesh/BoxGeometry.h"

#include "core/common/Errors.h"

#include <algorithm>
#include <string>

namespace wb::mesh {

int BoxGeometry::materialAt(const Point& p) const {
    int material = kVoid;
    for (const Box& b : boxes) {
        bool inside = true;
        for (int a = 0; a < 3; ++a) {
            if (p[a] < b.lo[a] || p[a] > b.hi[a]) {
                inside = false;
                break;
            }
        }
        if (inside) material = b.material;
    }
    return material;
}

Point BoxGeometry::boundsLo() const {
    Point lo = boxes.front().lo;
    for (const Box& b : boxes)
        for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], b.lo[a]);
    return lo;
}

Point BoxGeometry::boundsHi() const {
    Point hi = boxes.front().hi;
    for (const Box& b : boxes)
        for (int a = 0; a < 3; ++a) hi[a] = std::max(hi[a], b.hi[a]);
    return hi;
}

GridSpec BoxGeometry::gridSpec(const std::array<double, 3>& spacing_m) const {
    GridSpec spec;
    spec.spacing_m = spacing_m;
    for (const Box& b : boxes) {
        for (int a = 0; a < 3; ++a) {
            spec.keys[a].push_back(b.lo[a]);
            spec.keys[a].push_back(b.hi[a]);
        }
    }
    return spec;
}

Box makeBox(double x0, double y0, double z0, double x1, double y1, double z1, int material) {
    Box b;
    b.lo = {x0, y0, z0};
    b.hi = {x1, y1, z1};
    b.material = material;
    return b;
}

void validate(const BoxGeometry& geometry) {
    if (geometry.boxes.empty()) {
        throw WorkbenchError(ErrorCode::InvalidParameter, "the geometry has no boxes");
    }
    for (std::size_t i = 0; i < geometry.boxes.size(); ++i) {
        const Box& b = geometry.boxes[i];
        for (int a = 0; a < 3; ++a) {
            requireFinite(b.lo[a], "box corner coordinate", "m");
            requireFinite(b.hi[a], "box corner coordinate", "m");
            if (!(b.lo[a] < b.hi[a])) {
                throw WorkbenchError(ErrorCode::InvalidParameter,
                                     "box " + std::to_string(i) + " is flat or inverted along axis " +
                                         std::to_string(a) + " (lo must be below hi)");
            }
        }
        if (b.material < kVoid) {
            throw WorkbenchError(ErrorCode::InvalidParameter,
                                 "box " + std::to_string(i) + " has material index " + std::to_string(b.material));
        }
    }
}

}  // namespace wb::mesh
