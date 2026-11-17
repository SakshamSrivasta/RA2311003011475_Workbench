// Global assembly of the 3D conduction problem into a CSR system.
//
// Two systems are kept apart on purpose:
//   FullSystem     every mesh node, before any temperature is prescribed;
//                  needed afterwards for reactions and the heat balance.
//   ReducedSystem  free nodes only, Dirichlet rows and columns removed and
//                  their known values moved to the right-hand side. This is
//                  what the solvers see, and it is symmetric positive definite.
// Prescribing temperatures by a large penalty number would keep one matrix
// but ruins the condition number; the solver study shows this on purpose.
#pragma once

#include "core/common/Cancellation.h"
#include "core/fem3d/ThermalProblem3D.h"
#include "core/sparse/CsrMatrix.h"

#include <limits>
#include <vector>

namespace wb::fem3d {

struct FullSystem {
    sparse::CsrMatrix K;    // conduction + convection
    std::vector<double> b;  // heat generation + flux + convection ambient terms
    std::vector<std::size_t> dirichletNodes;  // sorted mesh node ids
    std::vector<double> dirichletValues;      // same order
};

// Tags the mesh faces from problem.regions and validates the problem.
// Throws InvalidParameter naming the region if one matches no face.
void applyRegions(mesh::HexMesh& mesh, const ThermalProblem3D& problem);

// The mesh must have had applyRegions() run on it. Throws Cancelled if the
// token is set between element batches.
FullSystem assemble(const mesh::HexMesh& mesh, const ThermalProblem3D& problem, const CancelToken* cancel = nullptr);

constexpr std::size_t kFixed = std::numeric_limits<std::size_t>::max();

struct ReducedSystem {
    sparse::CsrMatrix A;
    std::vector<double> b;
    std::vector<std::size_t> freeNodes;      // reduced index -> mesh node
    std::vector<std::size_t> reducedIndex;   // mesh node -> reduced index, or kFixed
    std::vector<double> fixedTemperature_C;  // per mesh node; meaningful at Dirichlet nodes
};

ReducedSystem eliminateDirichlet(const FullSystem& full);

// Nodal temperatures over the whole mesh from the reduced solution.
std::vector<double> expandSolution(const ReducedSystem& reduced, const std::vector<double>& x);

}  // namespace wb::fem3d
