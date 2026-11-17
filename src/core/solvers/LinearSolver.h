// Types shared by the three linear solvers of the study. Every solver takes
// the same CSR matrix and right-hand side, respects the same cancel token and
// fills the same report, so the benchmark harness can treat them alike.
#pragma once

#include "core/common/Cancellation.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace wb::solvers {

enum class SolveStatus {
    Converged,      // true relative residual at or below the tolerance
    MaxIterations,  // iteration cap hit (or a direct solver was refused)
    Cancelled,      // stopped by the cancel token; x holds the last iterate
    Breakdown       // the matrix is not symmetric positive definite
};

const char* toString(SolveStatus status) noexcept;

struct SolveOptions {
    double relativeTolerance = 1e-10;   // stop when ||b - A x|| / ||b|| <= this
    std::size_t maxIterations = 200000;
    std::size_t trueResidualEvery = 50; // CG recomputes b - A x explicitly this often (0 = never)
    bool recordHistory = false;         // keep the residual of every iteration
    const CancelToken* cancel = nullptr;
};

struct SolveReport {
    SolveStatus status = SolveStatus::MaxIterations;
    std::size_t iterations = 0;
    double relativeResidual = 0.0;  // true residual at exit
    std::vector<double> history;    // relative residual per iteration, if recorded (CG: the recursive one)
    std::vector<std::pair<std::size_t, double>> trueResidualHistory;  // (iteration, explicit residual)
    std::size_t bytesAllocated = 0;  // work vectors and factors, beyond the matrix itself
    bool converged() const { return status == SolveStatus::Converged; }
};

}  // namespace wb::solvers
