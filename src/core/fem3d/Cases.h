// The problems used by the tests and the solver study, defined once so that a
// test, the benchmark tool and the report all mean the same geometry.
#pragma once

#include "core/fem3d/ThermalProblem3D.h"
#include "core/mesh/BoxGeometry.h"
#include "core/mesh/HexMesh.h"

#include <array>
#include <functional>
#include <string>

namespace wb::fem3d {

struct StudyCase {
    std::string name;
    mesh::BoxGeometry geometry;
    ThermalProblem3D problem;
    std::array<double, 3> spacing_m{};
    std::function<double(const mesh::Point&)> exact;  // set when a closed-form solution exists
};

// Round 1's rod data on a 1 m cube: 100 C at x = 0, 20 C at x = 1 m, the other
// faces insulated, k = 10 W/(m K), q = 1e4 W/m^3. The answer is the Round 1 formula.
StudyCase rodCube(int cellsPerSide);
// The same data on a 1 m x 10 mm x 10 mm bar (A = 1e-4 m^2 like the rod), one cell across.
StudyCase rodBar(int cellsAlong);

// Smooth manufactured solution with its temperature prescribed on every boundary face.
StudyCase manufacturedOn(const std::string& name, const mesh::BoxGeometry& geometry,
                         const std::array<double, 3>& spacing_m, double conductivity_W_mK);
StudyCase manufacturedCube(int cellsPerSide);

// Geometry and operating point of the heat sink, in SI units.
namespace heatsink {
constexpr double kBaseX = 0.060, kBaseY = 0.040, kBaseZ = 0.005;
constexpr int kFins = 6;
constexpr double kFinThickness = 0.002, kFinHeight = 0.025;
constexpr double kChipSize = 0.020, kChipPower_W = 50.0;
constexpr double kConvection_W_m2K = 50.0, kAmbient_C = 25.0;
constexpr double kAluminium_W_mK = 200.0, kPad_W_mK = 3.0, kCopper_W_mK = 400.0;
constexpr double kPadThickness = 0.0005;
constexpr int kTagChip = 1, kTagBoardSide = 2;
double finX0(int i);  // x of fin i's low face
}  // namespace heatsink

// The Round 1 heat sink (60 x 40 x 5 mm base, six 2 x 25 mm fins along y) with
// a 20 x 20 mm, 50 W chip under the base and air convection on every exposed
// face except the board side. `contrast` adds a 0.5 mm thermal pad (k = 3)
// under the chip and a copper spreader (k = 400): the ill-conditioned variant.
StudyCase heatSink(double spacing_m, bool contrast);

// One fin on its own with a fixed root temperature, for the 1D fin check.
StudyCase singleFin(double spacing_m, double rootTemperature_C);

// Grid + mesh + face tags for a case.
mesh::HexMesh buildMesh(const StudyCase& c);

// Textbook straight fin with a convecting tip: theta(z)/theta_root, with
// m = sqrt(h P / (k A)) and hOverMk = h / (m k).
double finExcessRatio(double m, double length, double hOverMk, double z);
// Heat entering the fin at the root for an excess temperature thetaRoot.
double finRootHeat_W(double sqrtHPkA, double thetaRoot, double m, double length, double hOverMk);

}  // namespace wb::fem3d
