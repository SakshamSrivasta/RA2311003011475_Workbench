#include "core/fem3d/Cases.h"

#include "core/fea/ThermalRod1D.h"
#include "core/fem3d/Assembler.h"
#include "core/fem3d/Manufactured.h"

#include <cmath>
#include <memory>

namespace wb::fem3d {

namespace {

mesh::FaceRegion wholePlane(int axis, int side, double coordinate, int tag) {
    mesh::FaceRegion r;
    r.axis = axis;
    r.side = side;
    r.plane = coordinate;
    r.tag = tag;
    return r;
}

mesh::FaceRegion planeRectangle(int axis, int side, double coordinate, const mesh::Point& lo, const mesh::Point& hi,
                                int tag) {
    mesh::FaceRegion r = wholePlane(axis, side, coordinate, tag);
    r.lo = lo;
    r.hi = hi;
    return r;
}

// Rod data on a box [0,1] x [0,w] x [0,w]: hot at x = 0, cold at x = 1.
StudyCase rodOnBox(const std::string& name, double width, const std::array<double, 3>& spacing) {
    const fea::RodProblem rod;  // the Round 1 defaults
    StudyCase c;
    c.name = name;
    c.geometry.boxes = {mesh::makeBox(0, 0, 0, rod.length_m, width, width)};
    c.problem.materials = {{"rod material", rod.conductivity_W_mK, rod.heatGeneration_W_m3}};
    c.problem.regions = {wholePlane(0, 0, 0.0, 1), wholePlane(0, 1, rod.length_m, 2)};
    c.problem.dirichlet = {fixedTemperature(1, rod.leftTemperature_C), fixedTemperature(2, rod.rightTemperature_C)};
    c.spacing_m = spacing;
    c.exact = [rod](const mesh::Point& p) { return fea::exactTemperature(rod, p[0]); };
    return c;
}

}  // namespace

StudyCase rodCube(int cellsPerSide) {
    const double h = 1.0 / cellsPerSide;
    return rodOnBox("rod-cube-N" + std::to_string(cellsPerSide), 1.0, {h, h, h});
}

StudyCase rodBar(int cellsAlong) {
    return rodOnBox("rod-bar-N" + std::to_string(cellsAlong), 0.01, {1.0 / cellsAlong, 0.01, 0.01});
}

StudyCase manufacturedOn(const std::string& name, const mesh::BoxGeometry& geometry,
                         const std::array<double, 3>& spacing_m, double conductivity_W_mK) {
    auto ms = std::make_shared<ManufacturedSolution>();
    ms->lo = geometry.boundsLo();
    ms->hi = geometry.boundsHi();
    ms->conductivity_W_mK = conductivity_W_mK;
    StudyCase c;
    c.name = name;
    c.geometry = geometry;
    c.problem.materials = {{"manufactured", conductivity_W_mK, 0.0}};
    c.problem.source_W_m3 = [ms](const mesh::Point& p) { return ms->source_W_m3(p); };
    // Tag 0 is every face that no region claimed: here, the whole boundary.
    DirichletBc bc;
    bc.tag = 0;
    bc.temperature_C = [ms](const mesh::Point& p) { return ms->temperature(p); };
    c.problem.dirichlet = {bc};
    c.spacing_m = spacing_m;
    c.exact = [ms](const mesh::Point& p) { return ms->temperature(p); };
    return c;
}

StudyCase manufacturedCube(int cellsPerSide) {
    mesh::BoxGeometry g;
    g.boxes = {mesh::makeBox(0, 0, 0, 1, 1, 1)};
    const double h = 1.0 / cellsPerSide;
    return manufacturedOn("manufactured-cube-N" + std::to_string(cellsPerSide), g, {h, h, h}, 10.0);
}

namespace heatsink {
double finX0(int i) {
    const double pitch = (kBaseX - kFinThickness) / (kFins - 1);
    return i * pitch;
}
}  // namespace heatsink

StudyCase heatSink(double spacing_m, bool contrast) {
    using namespace heatsink;
    StudyCase c;
    c.name = std::string(contrast ? "heatsink-B" : "heatsink-A") + "-h" + std::to_string(spacing_m * 1e3) + "mm";
    c.geometry.boxes.push_back(mesh::makeBox(0, 0, 0, kBaseX, kBaseY, kBaseZ, 0));
    for (int i = 0; i < kFins; ++i) {
        const double x0 = finX0(i);
        c.geometry.boxes.push_back(mesh::makeBox(x0, 0, kBaseZ, x0 + kFinThickness, kBaseY, kBaseZ + kFinHeight, 0));
    }
    const double cx0 = 0.5 * (kBaseX - kChipSize), cx1 = cx0 + kChipSize;
    const double cy0 = 0.5 * (kBaseY - kChipSize), cy1 = cy0 + kChipSize;
    if (contrast) {
        // Interface layer directly under the chip, then a copper spreader above it.
        c.geometry.boxes.push_back(mesh::makeBox(cx0, cy0, 0, cx1, cy1, kPadThickness, 1));
        c.geometry.boxes.push_back(
            mesh::makeBox(cx0 - 0.005, cy0 - 0.005, kPadThickness, cx1 + 0.005, cy1 + 0.005, 0.003, 2));
    }
    c.problem.materials = {{"aluminium", kAluminium_W_mK, 0.0}, {"thermal pad", kPad_W_mK, 0.0}, {"copper", kCopper_W_mK, 0.0}};
    // Board side first (insulated, no condition on tag 2), then the chip footprint on top of it.
    c.problem.regions = {wholePlane(2, 0, 0.0, kTagBoardSide),
                         planeRectangle(2, 0, 0.0, {cx0, cy0, -1e300}, {cx1, cy1, 1e300}, kTagChip)};
    c.problem.flux = {{kTagChip, kChipPower_W / (kChipSize * kChipSize)}};
    c.problem.convection = {{0, kConvection_W_m2K, kAmbient_C}};
    c.spacing_m = {spacing_m, spacing_m, spacing_m};
    return c;
}

StudyCase singleFin(double spacing_m, double rootTemperature_C) {
    using namespace heatsink;
    StudyCase c;
    c.name = "single-fin";
    c.geometry.boxes = {mesh::makeBox(0, 0, 0, kFinThickness, kBaseY, kFinHeight, 0)};
    c.problem.materials = {{"aluminium", kAluminium_W_mK, 0.0}};
    c.problem.regions = {wholePlane(2, 0, 0.0, 1)};
    c.problem.dirichlet = {fixedTemperature(1, rootTemperature_C)};
    c.problem.convection = {{0, kConvection_W_m2K, kAmbient_C}};
    c.spacing_m = {spacing_m, spacing_m, spacing_m};
    return c;
}

mesh::HexMesh buildMesh(const StudyCase& c) {
    mesh::GridSpec spec = c.geometry.gridSpec(c.spacing_m);
    // A region's plane and rectangle bounds become grid lines too, otherwise a
    // face straddling the chip edge would be left out of the chip tag.
    const mesh::Point lo = c.geometry.boundsLo(), hi = c.geometry.boundsHi();
    const auto addKey = [&](int axis, double value) {
        if (value >= lo[axis] && value <= hi[axis]) spec.keys[axis].push_back(value);
    };
    for (const mesh::FaceRegion& r : c.problem.regions) {
        addKey(r.axis, r.plane);
        for (int a = 0; a < 3; ++a) {
            if (a == r.axis) continue;
            addKey(a, r.lo[a]);
            addKey(a, r.hi[a]);
        }
    }
    mesh::HexMesh m = mesh::buildHexMesh(c.geometry, mesh::buildGrid(spec));
    applyRegions(m, c.problem);
    return m;
}

double finExcessRatio(double m, double length, double hOverMk, double z) {
    const double s = m * (length - z);
    return (std::cosh(s) + hOverMk * std::sinh(s)) / (std::cosh(m * length) + hOverMk * std::sinh(m * length));
}

double finRootHeat_W(double sqrtHPkA, double thetaRoot, double m, double length, double hOverMk) {
    const double mL = m * length;
    return sqrtHPkA * thetaRoot * (std::sinh(mL) + hOverMk * std::cosh(mL)) / (std::cosh(mL) + hOverMk * std::sinh(mL));
}

}  // namespace wb::fem3d
