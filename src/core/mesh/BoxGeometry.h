// Geometry as a list of axis-aligned boxes painted in order. Each box carries
// a material index; kVoid cuts material away (holes, slots). Enough for a
// cube, a bar, a finned heat sink or a perforated block, and exact on a grid
// whose lines include every box face (see Grid3D).
#pragma once

#include "core/mesh/Grid3D.h"

#include <vector>

namespace wb::mesh {

constexpr int kVoid = -1;

struct Box {
    Point lo{}, hi{};  // metres; lo < hi on every axis
    int material = 0;  // index into the problem's material table, or kVoid
};

struct BoxGeometry {
    std::vector<Box> boxes;

    // Material at a point: the last box containing it wins, kVoid if none.
    int materialAt(const Point& p) const;
    Point boundsLo() const;
    Point boundsHi() const;
    // Grid keys from every box face, with the given target spacing.
    GridSpec gridSpec(const std::array<double, 3>& spacing_m) const;
    GridSpec gridSpec(double spacing_m) const { return gridSpec({spacing_m, spacing_m, spacing_m}); }
};

Box makeBox(double x0, double y0, double z0, double x1, double y1, double z1, int material = 0);

// Throws InvalidParameter for an empty list, a non-finite coordinate, a box
// with lo >= hi, or a material index below kVoid.
void validate(const BoxGeometry& geometry);

}  // namespace wb::mesh
