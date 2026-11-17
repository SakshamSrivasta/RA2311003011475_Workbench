// Steady conduction in 3D:  -div( k grad T ) = q,  k piecewise constant.
//
// Boundary conditions are attached to face tags (see mesh::FaceRegion):
//   Dirichlet   prescribed temperature, eliminated symmetrically at assembly
//   flux        prescribed heat flux into the solid [W/m^2]
//   convection  h (T - T_ambient) leaving the solid
// Faces with no condition are insulated (the natural boundary condition).
// Units: metres, W/(m K), W/m^3, W/m^2, W/(m^2 K), degrees Celsius.
#pragma once

#include "core/mesh/HexMesh.h"

#include <functional>
#include <string>
#include <vector>

namespace wb::fem3d {

struct Material {
    std::string name;
    double conductivity_W_mK = 1.0;
    double heatGeneration_W_m3 = 0.0;
};

struct DirichletBc {
    int tag = 0;
    std::function<double(const mesh::Point&)> temperature_C;  // may vary over the surface
};

struct FluxBc {
    int tag = 0;
    double flux_W_m2 = 0.0;  // positive into the solid
};

struct ConvectionBc {
    int tag = 0;
    double h_W_m2K = 0.0;
    double ambient_C = 0.0;
};

struct ThermalProblem3D {
    std::vector<Material> materials;        // indexed by the geometry's material ids
    std::vector<mesh::FaceRegion> regions;  // applied to the mesh faces before assembly
    std::vector<DirichletBc> dirichlet;     // on shared nodes a later entry wins
    std::vector<FluxBc> flux;
    std::vector<ConvectionBc> convection;
    // Optional source that varies in space, on top of the materials' uniform q.
    std::function<double(const mesh::Point&)> source_W_m3;
};

DirichletBc fixedTemperature(int tag, double temperature_C);

// Throws InvalidParameter for: a material with non-positive or non-finite k, a
// non-finite q, h or ambient, an element whose material id has no entry, a
// condition on a tag that no face carries, or no Dirichlet and no convection
// anywhere (then the temperature level is undefined and the matrix singular).
void validate(const mesh::HexMesh& mesh, const ThermalProblem3D& problem);

}  // namespace wb::fem3d
