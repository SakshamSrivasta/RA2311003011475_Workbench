#include "core/solvers/SkylineCholesky.h"

#include "core/common/Errors.h"
#include "core/common/Stopwatch.h"
#include "core/sparse/VectorOps.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace wb::solvers {

namespace {

// First stored column of each row of P A P^T, without building it.
std::vector<std::size_t> firstColumns(const sparse::CsrMatrix& A, const sparse::Permutation& perm) {
    const std::size_t n = A.rows();
    std::vector<std::size_t> inverse(n);
    for (std::size_t i = 0; i < n; ++i) inverse[perm[i]] = i;
    std::vector<std::size_t> first(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t f = i;
        const std::size_t old = perm[i];
        for (std::size_t p = A.rowPtr[old]; p < A.rowPtr[old + 1]; ++p) f = std::min(f, inverse[A.col[p]]);
        first[i] = f;
    }
    return first;
}

}  // namespace

SkylinePlan planSkyline(const sparse::CsrMatrix& A, const sparse::Permutation& perm) {
    SkylinePlan plan;
    plan.n = A.rows();
    const auto first = firstColumns(A, perm);
    for (std::size_t i = 0; i < plan.n; ++i) {
        const double w = static_cast<double>(i - first[i]);
        plan.profile += i - first[i];
        plan.halfBandwidth = std::max(plan.halfBandwidth, i - first[i]);
        plan.factorFlops += 0.5 * w * w + w;
    }
    plan.factorBytes = static_cast<double>(plan.profile + plan.n) * sizeof(double) +
                       2.0 * static_cast<double>(plan.n) * sizeof(std::size_t);
    return plan;
}

std::size_t SkylineCholesky::bytes() const {
    return values_.size() * sizeof(double) + (first_.size() + start_.size() + perm_.size()) * sizeof(std::size_t);
}

SkylineCholesky::SkylineCholesky(const sparse::CsrMatrix& A, const SkylineOptions& options) {
    const Stopwatch timer;
    const std::size_t n = A.rows();
    perm_ = options.ordering == Ordering::ReverseCuthillMcKee ? sparse::reverseCuthillMcKee(A)
                                                               : sparse::identityPermutation(n);
    first_ = firstColumns(A, perm_);
    plan_ = planSkyline(A, perm_);
    if (plan_.factorBytes > static_cast<double>(options.memoryCapBytes)) {
        status_ = SolveStatus::Refused;
        detail_ = "factor would need " + std::to_string(plan_.factorBytes / 1e9) + " GB, above the cap of " +
                  std::to_string(static_cast<double>(options.memoryCapBytes) / 1e9) + " GB";
        setupSeconds_ = timer.elapsedSeconds();
        return;
    }

    // Scatter the lower triangle of P A P^T into the envelope.
    start_.resize(n + 1);
    start_[0] = 0;
    for (std::size_t i = 0; i < n; ++i) start_[i + 1] = start_[i] + (i - first_[i] + 1);
    values_.assign(start_[n], 0.0);
    std::vector<std::size_t> inverse(n);
    for (std::size_t i = 0; i < n; ++i) inverse[perm_[i]] = i;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t old = perm_[i];
        for (std::size_t p = A.rowPtr[old]; p < A.rowPtr[old + 1]; ++p) {
            const std::size_t j = inverse[A.col[p]];
            if (j <= i) values_[start_[i] + (j - first_[i])] = A.val[p];
        }
    }

    // Row-by-row factorisation. L(i, j) lives at values_[start_[i] + j - first_[i]].
    for (std::size_t i = 0; i < n; ++i) {
        if ((i & 255) == 0 && isCancelled(options.cancel)) {
            status_ = SolveStatus::Cancelled;
            detail_ = "factorisation cancelled at row " + std::to_string(i);
            values_.clear();
            setupSeconds_ = timer.elapsedSeconds();
            return;
        }
        double* Li = values_.data() + start_[i];
        const std::size_t fi = first_[i];
        for (std::size_t j = fi; j < i; ++j) {
            const double* Lj = values_.data() + start_[j];
            const std::size_t fj = first_[j];
            const std::size_t k0 = std::max(fi, fj);
            double s = Li[j - fi];
            const double* a = Li + (k0 - fi);
            const double* b = Lj + (k0 - fj);
            for (std::size_t k = k0; k < j; ++k) s -= *a++ * *b++;
            Li[j - fi] = s / Lj[j - fj];
        }
        double d = Li[i - fi];
        for (std::size_t k = fi; k < i; ++k) d -= Li[k - fi] * Li[k - fi];
        if (!(d > 0.0) || !std::isfinite(d)) {
            status_ = SolveStatus::Breakdown;
            detail_ = "non-positive pivot " + std::to_string(d) + " at row " + std::to_string(i) +
                      ": the matrix is not symmetric positive definite";
            values_.clear();
            setupSeconds_ = timer.elapsedSeconds();
            return;
        }
        Li[i - fi] = std::sqrt(d);
    }
    status_ = SolveStatus::Converged;
    setupSeconds_ = timer.elapsedSeconds();
}

std::vector<double> SkylineCholesky::solve(const std::vector<double>& b) const {
    const std::size_t n = first_.size();
    std::vector<double> y = sparse::permuteVector(b, perm_);
    // L y = Pb, row-oriented.
    for (std::size_t i = 0; i < n; ++i) {
        const double* Li = values_.data() + start_[i];
        double s = y[i];
        for (std::size_t k = first_[i]; k < i; ++k) s -= Li[k - first_[i]] * y[k];
        y[i] = s / Li[i - first_[i]];
    }
    // L^T z = y, column-oriented: row i of L is column i of L^T.
    for (std::size_t i = n; i-- > 0;) {
        const double* Li = values_.data() + start_[i];
        y[i] /= Li[i - first_[i]];
        const double zi = y[i];
        for (std::size_t k = first_[i]; k < i; ++k) y[k] -= Li[k - first_[i]] * zi;
    }
    return sparse::unpermuteVector(y, perm_);
}

SolveReport skylineSolve(const sparse::CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                         const SkylineOptions& options) {
    if (b.size() != A.rows()) {
        throw WorkbenchError(ErrorCode::InvalidParameter, "right-hand side size does not match the matrix");
    }
    SolveReport report;
    const SkylineCholesky chol(A, options);
    report.setupSeconds = chol.setupSeconds();
    report.bytesAllocated = static_cast<std::size_t>(chol.plan().factorBytes);
    report.status = chol.status();
    report.detail = chol.detail();
    if (!chol.ok()) return report;
    const Stopwatch timer;
    x = chol.solve(b);
    report.solveSeconds = timer.elapsedSeconds();
    report.bytesAllocated = chol.bytes() + 2 * b.size() * sizeof(double);
    const double bNorm = sparse::norm2(b);
    report.relativeResidual = bNorm == 0.0 ? 0.0 : sparse::norm2(sparse::residual(A, x, b)) / bNorm;
    report.status = SolveStatus::Converged;
    return report;
}

}  // namespace wb::solvers
