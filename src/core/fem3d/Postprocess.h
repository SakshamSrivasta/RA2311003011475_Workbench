// Checks on a computed temperature field: reactions at prescribed nodes, the
// global heat balance, and error norms against a known solution.
#pragma once

#include "core/fem3d/Assembler.h"

#include <functional>
#include <vector>

namespace wb::fem3d {

// (K T - b) at the Dirichlet nodes, in the order of full.dirichletNodes: the
// heat that has to enter at each node to hold its temperature [W].
std::vector<double> reactions(const FullSystem& full, const std::vector<double>& T);

struct HeatBalance {
    double generated_W = 0.0;     // volumetric sources
    double fluxIn_W = 0.0;        // prescribed-flux faces
    double reactionIn_W = 0.0;    // entering through prescribed-temperature nodes
    double convectedOut_W = 0.0;  // h (T - T_ambient) over convective faces
    // generated + fluxIn + reactionIn - convectedOut. For the discrete system
    // this is exactly the sum of the residual, so it measures how far the
    // solver got, not the discretisation.
    double imbalance_W = 0.0;
};

HeatBalance heatBalance(const mesh::HexMesh& mesh, const ThermalProblem3D& problem, const FullSystem& full,
                        const std::vector<double>& T);

struct ErrorNorms {
    double maxNodal_C = 0.0;  // max |T_h - T| over the nodes
    double rms_C = 0.0;       // sqrt( integral (T_h - T)^2 dV / V ), T_h interpolated trilinearly
};

ErrorNorms errorNorms(const mesh::HexMesh& mesh, const std::vector<double>& T,
                      const std::function<double(const mesh::Point&)>& exact);

}  // namespace wb::fem3d
