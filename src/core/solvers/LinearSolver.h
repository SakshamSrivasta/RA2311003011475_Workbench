// Types shared by the three linear solvers of the study. Every solver takes
// the same CSR matrix and right-hand side, respects the same cancel token and
// fills the same report, so the benchmark harness can treat them alike.
#pragma once

#include "core/common/Cancellation.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace wb::solvers {

enum class SolveStatus {
    Converged,      // true relative residual at or below the tolerance
    MaxIterations,  // iteration cap hit
    TimeLimit,      // wall-clock budget used up before converging
    Refused,        // a direct solver would need more memory than allowed
    Diverged,       // the residual grew without bound (e.g. SOR with omega >= 2)
    Stagnated,      // CG: the true residual stopped falling below the tolerance (round-off floor)
    Cancelled,      // stopped by the cancel token; x holds the last iterate
    Breakdown       // the matrix (or preconditioner) is not positive definite
};

const char* toString(SolveStatus status) noexcept;

struct SolveOptions {
    double relativeTolerance = 1e-10;    // stop when ||b - A x|| / ||b|| <= this
    std::size_t maxIterations = 200000;
    double maxSeconds = 0.0;             // 0 = no time limit
    std::size_t trueResidualEvery = 50;  // CG: recompute b - A x explicitly this often (0 = never)
                                         // SOR: check the residual this often (0 = every sweep)
    bool recordHistory = false;          // keep the residual of every iteration (SOR: of every check)
    bool recordCoefficients = false;     // CG: keep alpha_k and beta_k for the Lanczos eigenvalue estimate
    std::size_t maxResidualReplacements = 5;  // CG: restarts allowed when the recursive residual drifts
    const CancelToken* cancel = nullptr;
};

struct SolveReport {
    SolveStatus status = SolveStatus::MaxIterations;
    std::string detail;             // e.g. why a factorisation failed or was refused
    std::size_t iterations = 0;     // sweeps for SOR, 0 for the direct solver
    double relativeResidual = 0.0;  // true residual at exit
    double setupSeconds = 0.0;      // ordering + factorisation / preconditioner build
    double solveSeconds = 0.0;      // iterations / triangular solves
    std::vector<double> history;    // relative residual per iteration if recorded (CG: the recursive one)
    std::vector<std::pair<std::size_t, double>> trueResidualHistory;  // (iteration, explicit residual)
    std::vector<double> alphas, betas;  // CG coefficients if recorded
    std::size_t residualReplacements = 0;  // CG: times the recursive residual was reset to b - A x
    std::size_t bytesAllocated = 0;     // work vectors, factors and preconditioners, beyond A itself
    bool converged() const { return status == SolveStatus::Converged; }
};

}  // namespace wb::solvers
