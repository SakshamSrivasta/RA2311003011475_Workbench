// Conjugate gradient for symmetric positive definite systems.
//
// The residual is updated recursively (r -= alpha A p), which is what makes
// CG one matrix-vector product per iteration. In floating point that
// recursive residual drifts away from the true b - A x, so every
// `trueResidualEvery` iterations, and before declaring convergence, the true
// residual is computed explicitly. The report keeps both so the drift can be
// plotted.
#pragma once

#include "core/solvers/LinearSolver.h"
#include "core/sparse/CsrMatrix.h"

#include <vector>

namespace wb::solvers {

// Solves A x = b starting from x (zeros if x is empty or the wrong size).
// Throws InvalidParameter if the sizes disagree.
SolveReport conjugateGradient(const sparse::CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                              const SolveOptions& options);

}  // namespace wb::solvers
