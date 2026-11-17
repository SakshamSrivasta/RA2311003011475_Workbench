// Stationary iterations: Jacobi, Gauss-Seidel and SOR.
//
// One SOR sweep updates the unknowns in order, each with the newest values:
//     x_i <- x_i + omega (b_i - sum_j a_ij x_j) / a_ii
// omega = 1 is Gauss-Seidel. For a symmetric positive definite matrix SOR
// converges for every 0 < omega < 2 (Ostrowski-Reich) and cannot converge
// for omega outside that range. A sweep costs about one matrix-vector
// product but does not leave the residual behind, so the residual is
// computed explicitly every `trueResidualEvery` sweeps; that check is the
// only stopping test.
#pragma once

#include "core/solvers/LinearSolver.h"
#include "core/sparse/CsrMatrix.h"

#include <vector>

namespace wb::solvers {

// Throws InvalidParameter if omega is not finite or not in (0, 2.5], or the
// sizes disagree. Values >= 2 are allowed on purpose, to show divergence.
SolveReport sor(const sparse::CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x, double omega,
                const SolveOptions& options);

// Plain Jacobi (all unknowns updated from the previous sweep). Converges for
// these matrices only because they are diagonally dominant enough; kept as
// the baseline that Gauss-Seidel halves.
SolveReport jacobi(const sparse::CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                   const SolveOptions& options);

// Optimal omega for the model Poisson problem with n cells per side:
// 2 / (1 + sin(pi h)), h = 1/n. Used as the prediction to compare against.
double modelProblemOptimalOmega(int cellsPerSide);

}  // namespace wb::solvers
