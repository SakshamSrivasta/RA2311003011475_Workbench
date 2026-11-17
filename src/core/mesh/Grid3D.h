// Graded Cartesian grid for the 3D thermal study.
//
// Each axis is built from "keys" (coordinates that must become grid lines,
// i.e. every face of every box in the geometry) and a target spacing. The
// interval between two keys is split into equal cells no wider than the
// target. Because every box face is a grid line, a geometry made of boxes is
// represented exactly: a cell is fully solid or fully void, never partly
// covered. Refinements make the cells smaller inside a chosen interval.
#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace wb::mesh {

using Point = std::array<double, 3>;

struct Refinement {
    int axis = 0;           // 0 = x, 1 = y, 2 = z
    double lo = 0.0;        // interval on that axis [m]
    double hi = 0.0;
    double spacing_m = 0.0; // target cell size inside the interval
};

struct GridSpec {
    std::array<std::vector<double>, 3> keys;  // per axis; any order, duplicates allowed
    std::array<double, 3> spacing_m{};        // target cell size per axis outside refinements
    std::vector<Refinement> refinements;
};

// More grid nodes than this and the matrix alone would need several GB.
constexpr std::size_t kMaxGridNodes = 20'000'000;

struct Grid3D {
    std::array<std::vector<double>, 3> lines;  // strictly increasing, per axis

    int cells(int axis) const { return static_cast<int>(lines[axis].size()) - 1; }
    int nodes(int axis) const { return static_cast<int>(lines[axis].size()); }
    std::size_t cellCount() const;
    std::size_t gridNodeCount() const;
    double cellSize(int axis, int cell) const { return lines[axis][cell + 1] - lines[axis][cell]; }
    double extent(int axis) const { return lines[axis].back() - lines[axis].front(); }
    // Absolute tolerance for "this coordinate lies on a grid line" tests.
    double tolerance() const;
};

// Throws InvalidParameter if a spacing is not positive and finite, an axis has
// fewer than two distinct keys, or the grid would exceed kMaxGridNodes.
Grid3D buildGrid(const GridSpec& spec);

}  // namespace wb::mesh
