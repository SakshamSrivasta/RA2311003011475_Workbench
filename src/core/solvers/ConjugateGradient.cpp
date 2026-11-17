#include "core/solvers/ConjugateGradient.h"

#include "core/common/Errors.h"
#include "core/sparse/VectorOps.h"

#include <algorithm>
#include <cmath>

namespace wb::solvers {

const char* toString(SolveStatus status) noexcept {
    switch (status) {
        case SolveStatus::Converged: return "converged";
        case SolveStatus::MaxIterations: return "max-iterations";
        case SolveStatus::Cancelled: return "cancelled";
        case SolveStatus::Breakdown: return "breakdown";
    }
    return "unknown";
}

SolveReport conjugateGradient(const sparse::CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                              const SolveOptions& opt) {
    using sparse::axpy;
    using sparse::dot;
    const std::size_t n = A.rows();
    if (b.size() != n) {
        throw WorkbenchError(ErrorCode::InvalidParameter, "right-hand side size does not match the matrix");
    }
    if (x.size() != n) x.assign(n, 0.0);

    SolveReport report;
    report.bytesAllocated = 4 * n * sizeof(double);
    const double bNorm = sparse::norm2(b);
    if (bNorm == 0.0) {
        std::fill(x.begin(), x.end(), 0.0);
        report.status = SolveStatus::Converged;
        return report;
    }

    std::vector<double> r = sparse::residual(A, x, b), p = r, Ap(n), scratch(n);
    const auto trueRelativeResidual = [&]() {
        A.multiply(x.data(), scratch.data());
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double d = b[i] - scratch[i];
            s += d * d;
        }
        return std::sqrt(s) / bNorm;
    };

    double rr = dot(r, r);
    double relative = std::sqrt(rr) / bNorm;
    if (opt.recordHistory) report.history.push_back(relative);
    if (relative <= opt.relativeTolerance) {
        report.status = SolveStatus::Converged;
        report.relativeResidual = relative;
        return report;
    }

    for (std::size_t it = 1; it <= opt.maxIterations; ++it) {
        if (isCancelled(opt.cancel)) {
            report.status = SolveStatus::Cancelled;
            break;
        }
        A.multiply(p.data(), Ap.data());
        const double pAp = dot(p, Ap);
        if (!(pAp > 0.0) || !std::isfinite(pAp)) {
            report.status = SolveStatus::Breakdown;  // p' A p <= 0: A is not positive definite
            break;
        }
        const double alpha = rr / pAp;
        axpy(alpha, p, x);
        axpy(-alpha, Ap, r);
        const double rrNew = dot(r, r);
        relative = std::sqrt(rrNew) / bNorm;
        report.iterations = it;
        if (opt.recordHistory) report.history.push_back(relative);
        if (opt.trueResidualEvery != 0 && it % opt.trueResidualEvery == 0) {
            report.trueResidualHistory.emplace_back(it, trueRelativeResidual());
        }
        if (relative <= opt.relativeTolerance) {
            // Never trust the recursive residual for the final verdict.
            const double trueRelative = trueRelativeResidual();
            if (trueRelative <= opt.relativeTolerance) {
                report.status = SolveStatus::Converged;
                report.relativeResidual = trueRelative;
                return report;
            }
        }
        const double beta = rrNew / rr;
        for (std::size_t i = 0; i < n; ++i) p[i] = r[i] + beta * p[i];
        rr = rrNew;
    }
    report.relativeResidual = trueRelativeResidual();
    return report;
}

}  // namespace wb::solvers
