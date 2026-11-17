#include "core/solvers/Stationary.h"

#include "core/common/Errors.h"
#include "core/common/Stopwatch.h"
#include "core/sparse/VectorOps.h"

#include <cmath>
#include <string>

namespace wb::solvers {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Shared loop: `sweep` does one pass; the residual is checked every `every` sweeps.
template <typename Sweep>
SolveReport iterate(const sparse::CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                    const SolveOptions& opt, std::size_t extraBytes, Sweep sweep) {
    const std::size_t n = A.rows();
    if (b.size() != n) throw WorkbenchError(ErrorCode::InvalidParameter, "right-hand side size does not match the matrix");
    if (x.size() != n) x.assign(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const double d = A.at(i, static_cast<std::uint32_t>(i));
        if (!(d > 0.0)) {
            SolveReport r;
            r.status = SolveStatus::Breakdown;
            r.detail = "non-positive diagonal at row " + std::to_string(i);
            return r;
        }
    }

    SolveReport report;
    report.bytesAllocated = n * sizeof(double) + extraBytes;
    const Stopwatch timer;
    const double bNorm = sparse::norm2(b);
    std::vector<double> scratch(n);
    const auto relativeResidual = [&]() {
        A.multiply(x.data(), scratch.data());
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i) s += (b[i] - scratch[i]) * (b[i] - scratch[i]);
        return bNorm == 0.0 ? std::sqrt(s) : std::sqrt(s) / bNorm;
    };
    double rel = relativeResidual();
    const double initial = rel;
    if (opt.recordHistory) report.history.push_back(rel);
    if (rel <= opt.relativeTolerance) {
        report.status = SolveStatus::Converged;
        report.relativeResidual = rel;
        return report;
    }
    const std::size_t every = opt.trueResidualEvery == 0 ? 1 : opt.trueResidualEvery;
    for (std::size_t it = 1; it <= opt.maxIterations; ++it) {
        if (isCancelled(opt.cancel)) {
            report.status = SolveStatus::Cancelled;
            break;
        }
        sweep();
        report.iterations = it;
        if (it % every != 0 && it != opt.maxIterations) continue;
        rel = relativeResidual();
        if (opt.recordHistory) report.history.push_back(rel);
        report.trueResidualHistory.emplace_back(it, rel);
        if (rel <= opt.relativeTolerance) {
            report.status = SolveStatus::Converged;
            break;
        }
        if (!std::isfinite(rel) || rel > 1e8 * std::max(initial, 1.0)) {
            report.status = SolveStatus::Diverged;
            break;
        }
        if (opt.maxSeconds > 0.0 && timer.elapsedSeconds() > opt.maxSeconds) {
            report.status = SolveStatus::TimeLimit;
            break;
        }
    }
    report.solveSeconds = timer.elapsedSeconds();
    report.relativeResidual = rel;
    return report;
}

}  // namespace

SolveReport sor(const sparse::CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x, double omega,
                const SolveOptions& opt) {
    if (!std::isfinite(omega) || omega <= 0.0 || omega > 2.5) {
        throw WorkbenchError(ErrorCode::InvalidParameter, "relaxation factor omega must be in (0, 2.5]");
    }
    const std::vector<double> diag = A.diagonal();
    const std::size_t n = A.rows();
    return iterate(A, b, x, opt, n * sizeof(double), [&]() {
        for (std::size_t i = 0; i < n; ++i) {
            double s = b[i];
            for (std::size_t p = A.rowPtr[i]; p < A.rowPtr[i + 1]; ++p) s -= A.val[p] * x[A.col[p]];
            x[i] += omega * s / diag[i];
        }
    });
}

SolveReport jacobi(const sparse::CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                   const SolveOptions& opt) {
    const std::vector<double> diag = A.diagonal();
    const std::size_t n = A.rows();
    std::vector<double> next(n);
    return iterate(A, b, x, opt, 2 * n * sizeof(double), [&]() {
        for (std::size_t i = 0; i < n; ++i) {
            double s = b[i];
            for (std::size_t p = A.rowPtr[i]; p < A.rowPtr[i + 1]; ++p) s -= A.val[p] * x[A.col[p]];
            next[i] = x[i] + s / diag[i];
        }
        x.swap(next);
    });
}

double modelProblemOptimalOmega(int cellsPerSide) {
    return 2.0 / (1.0 + std::sin(kPi / cellsPerSide));
}

}  // namespace wb::solvers
