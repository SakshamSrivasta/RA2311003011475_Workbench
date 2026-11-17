#include "TestHarness.h"

#include "core/common/Errors.h"
#include "core/fem3d/Cases.h"
#include "core/mesh/BoxGeometry.h"
#include "core/mesh/Grid3D.h"
#include "core/mesh/HexMesh.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace wb;

namespace {

constexpr double mm = 1e-3;

bool onGridLine(const mesh::Grid3D& g, int axis, double coordinate) {
    for (double x : g.lines[axis]) {
        if (std::abs(x - coordinate) <= g.tolerance()) return true;
    }
    return false;
}

}  // namespace

TEST(grid_lines_include_every_box_face) {
    const auto c = fem3d::heatSink(2 * mm, true);
    const auto grid = mesh::buildGrid(c.geometry.gridSpec(c.spacing_m));
    for (const auto& b : c.geometry.boxes) {
        for (int a = 0; a < 3; ++a) {
            CHECK(onGridLine(grid, a, b.lo[a]));
            CHECK(onGridLine(grid, a, b.hi[a]));
        }
    }
    for (int a = 0; a < 3; ++a) {
        for (int i = 0; i < grid.cells(a); ++i) {
            CHECK(grid.cellSize(a, i) > 0.0);
            CHECK(grid.cellSize(a, i) <= 2 * mm + 1e-12);
        }
    }
}

TEST(grid_spacing_respects_target_and_refinement) {
    mesh::GridSpec s;
    s.keys = {{{0.0, 1.0}, {0.0, 1.0}, {0.0, 1.0}}};
    s.spacing_m = {0.25, 0.25, 0.25};
    s.refinements.push_back({0, 0.5, 0.75, 0.05});
    const auto g = mesh::buildGrid(s);
    CHECK(g.cells(1) == 4);
    CHECK(g.cells(2) == 4);
    // x: [0, 0.5] -> 2 cells, [0.5, 0.75] -> 5 cells of 0.05, [0.75, 1] -> 1 cell
    CHECK(g.cells(0) == 8);
    CHECK_NEAR(g.cellSize(0, 0), 0.25, 1e-12);
    CHECK_NEAR(g.cellSize(0, 3), 0.05, 1e-12);
    CHECK_NEAR(g.cellSize(0, 7), 0.25, 1e-12);
    CHECK(g.lines[0].front() == 0.0);
    CHECK(g.lines[0].back() == 1.0);
    CHECK(g.gridNodeCount() == 9 * 5 * 5);
}

TEST(grid_rejects_bad_input) {
    mesh::GridSpec s;
    s.keys = {{{0.0, 1.0}, {0.0, 1.0}, {0.0, 1.0}}};
    s.spacing_m = {0.0, 0.25, 0.25};
    CHECK_THROWS_CODE(mesh::buildGrid(s), ErrorCode::InvalidParameter);
    s.spacing_m = {0.25, 0.25, 0.25};
    s.keys[2] = {0.5, 0.5};  // flat along z
    CHECK_THROWS_CODE(mesh::buildGrid(s), ErrorCode::InvalidParameter);
    s.keys[2] = {0.0, 1.0};
    s.spacing_m = {1e-5, 1e-5, 1e-5};  // 1e15 nodes
    CHECK_THROWS_CODE(mesh::buildGrid(s), ErrorCode::InvalidParameter);
    s.spacing_m = {0.25, 0.25, 0.25};
    s.refinements.push_back({0, 0.7, 0.3, 0.1});  // inverted interval
    CHECK_THROWS_CODE(mesh::buildGrid(s), ErrorCode::InvalidParameter);
}

TEST(cube_mesh_counts_volume_area_and_bandwidth) {
    const auto m = fem3d::buildMesh(fem3d::rodCube(8));
    CHECK(m.elementCount() == 512);
    CHECK(m.nodeCount() == 729);
    CHECK(m.faces.size() == 6 * 64);
    CHECK_NEAR(m.solidVolume(), 1.0, 1e-12);
    CHECK_NEAR(m.boundaryArea(), 6.0, 1e-12);
    // All axes equal, so x stays fastest: neighbours differ by at most 1 + 9 + 81.
    CHECK(mesh::halfBandwidth(m) == 91);
}

TEST(heat_sink_mesh_is_an_exact_box_union) {
    const auto m = fem3d::buildMesh(fem3d::heatSink(2 * mm, false));
    CHECK_NEAR(m.solidVolume(), 24000e-9, 1e-15);   // 12,000 mm^3 base + six 2,000 mm^3 fins
    CHECK_NEAR(m.boundaryArea(), 18400e-6, 1e-13);  // worked out by hand from the box dimensions
    double chip = 0.0, board = 0.0, exposed = 0.0;
    for (const auto& f : m.faces) {
        if (f.tag == fem3d::heatsink::kTagChip) chip += f.area();
        else if (f.tag == fem3d::heatsink::kTagBoardSide) board += f.area();
        else exposed += f.area();
    }
    CHECK_NEAR(chip, 400e-6, 1e-15);
    CHECK_NEAR(board, 2000e-6, 1e-15);
    CHECK_NEAR(exposed, 16000e-6, 1e-13);
    // z is the shortest axis (30 mm): it varies fastest and bounds the bandwidth.
    CHECK(m.order[0] == 2);
    const std::size_t bound = static_cast<std::size_t>(m.grid.nodes(2)) * m.grid.nodes(1) + m.grid.nodes(2) + 1;
    CHECK(mesh::halfBandwidth(m) <= bound);
    CHECK(mesh::halfBandwidth(m) > bound / 4);
}

TEST(contrast_variant_has_three_materials_and_stretched_cells) {
    const auto m = fem3d::buildMesh(fem3d::heatSink(2 * mm, true));
    double volume[3] = {0.0, 0.0, 0.0};
    double worstAspect = 1.0;
    for (std::size_t e = 0; e < m.elementCount(); ++e) {
        volume[m.elementMaterial[e]] += m.elementVolume(e);
        const auto s = m.elementSize(e);
        const double lo = std::min({s[0], s[1], s[2]}), hi = std::max({s[0], s[1], s[2]});
        worstAspect = std::max(worstAspect, hi / lo);
    }
    CHECK_NEAR(volume[1], 200e-9, 1e-16);   // pad 20 x 20 x 0.5 mm
    CHECK_NEAR(volume[2], 2250e-9, 1e-15);  // copper 30 x 30 x 2.5 mm
    CHECK_NEAR(volume[0], 24000e-9 - 2450e-9, 1e-15);
    CHECK(worstAspect >= 3.5);  // the 0.5 mm pad layer under 2 mm cells
}

TEST(void_boxes_cut_holes_and_expose_their_walls) {
    mesh::BoxGeometry g;
    g.boxes = {mesh::makeBox(0, 0, 0, 1, 1, 1, 0), mesh::makeBox(0.25, 0.25, 0, 0.75, 0.75, 1, mesh::kVoid)};
    const auto m = mesh::buildHexMesh(g, mesh::buildGrid(g.gridSpec(0.25)));
    CHECK(m.elementCount() == 64 - 16);
    CHECK_NEAR(m.solidVolume(), 0.75, 1e-12);
    // outer 6, minus two hole openings of 0.25, plus four hole walls of 0.5 x 1
    CHECK_NEAR(m.boundaryArea(), 6.0 - 0.5 + 2.0, 1e-12);
}

TEST(regions_that_match_no_face_are_an_error) {
    auto c = fem3d::rodCube(4);
    mesh::FaceRegion r;
    r.axis = 0;
    r.side = 0;
    r.plane = 0.3;  // not a grid line
    r.tag = 7;
    c.problem.regions.push_back(r);
    CHECK_THROWS_CODE(fem3d::buildMesh(c), ErrorCode::InvalidParameter);
}

TEST(later_regions_override_earlier_tags) {
    // The chip rectangle is painted after the whole board side.
    const auto m = fem3d::buildMesh(fem3d::heatSink(2 * mm, false));
    for (const auto& f : m.faces) {
        if (f.axis != 2 || f.side != 0) {
            CHECK(f.tag == 0);
            continue;
        }
        const auto& cc = m.elementCell[f.element];
        const double x = 0.5 * (m.grid.lines[0][cc[0]] + m.grid.lines[0][cc[0] + 1]);
        const double y = 0.5 * (m.grid.lines[1][cc[1]] + m.grid.lines[1][cc[1] + 1]);
        const bool underChip = x > 20 * mm && x < 40 * mm && y > 10 * mm && y < 30 * mm;
        CHECK(f.tag == (underChip ? fem3d::heatsink::kTagChip : fem3d::heatsink::kTagBoardSide));
    }
}

TEST(bar_mesh_positions_and_sizes_round_trip) {
    const auto m = fem3d::buildMesh(fem3d::rodBar(10));
    CHECK(m.elementCount() == 10);
    CHECK(m.nodeCount() == 44);
    for (std::size_t e = 0; e < m.elementCount(); ++e) {
        const auto s = m.elementSize(e);
        CHECK_NEAR(s[0], 0.1, 1e-12);
        CHECK_NEAR(s[1], 0.01, 1e-12);
        CHECK_NEAR(s[2], 0.01, 1e-12);
        CHECK_NEAR(m.elementVolume(e), 1e-5, 1e-17);
    }
    std::map<long, int> perStation;  // 4 nodes at every x
    for (std::size_t n = 0; n < m.nodeCount(); ++n) ++perStation[std::lround(m.nodePosition(n)[0] * 10)];
    CHECK(perStation.size() == 11);
    for (const auto& [x, count] : perStation) {
        (void)x;
        CHECK(count == 4);
    }
    for (int axis = 0; axis < 3; ++axis) {
        for (int side = 0; side < 2; ++side) {
            for (int l : mesh::faceLocalNodes(axis, side)) CHECK(((l >> axis) & 1) == side);
        }
    }
}

TEST(mesh_rejects_a_grid_that_misses_the_geometry) {
    mesh::BoxGeometry g;
    g.boxes = {mesh::makeBox(0, 0, 0, 1, 1, 1)};
    mesh::GridSpec s;
    s.keys = {{{5.0, 6.0}, {0.0, 1.0}, {0.0, 1.0}}};
    s.spacing_m = {0.5, 0.5, 0.5};
    CHECK_THROWS_CODE(mesh::buildHexMesh(g, mesh::buildGrid(s)), ErrorCode::InvalidParameter);
    mesh::BoxGeometry empty;
    CHECK_THROWS_CODE(mesh::buildHexMesh(empty, mesh::buildGrid(g.gridSpec(0.5))), ErrorCode::InvalidParameter);
    mesh::BoxGeometry flat;
    flat.boxes = {mesh::makeBox(0, 0, 0, 1, 1, 0)};
    CHECK_THROWS_CODE(mesh::validate(flat), ErrorCode::InvalidParameter);
}
