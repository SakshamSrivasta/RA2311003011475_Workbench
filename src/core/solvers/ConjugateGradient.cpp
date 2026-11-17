#include "core/solvers/ConjugateGradient.h"

#include "core/common/Errors.h"
#include "core/common/Stopwatch.h"
#include "core/sparse/VectorOps.h"

#include <algorithm>
#include <cmath>

namespace wb::solvers {

const char* toString(SolveStatus status) noexcept {
    switch (status) {
        case SolveStatus::Converged: return "converged";
        case SolveStatus::MaxIterations: return "max-iterations";
        case SolveStatus::TimeLimit: return "time-limit";
        case SolveStatus::Refused: return "refused";
        case SolveStatus::Diverged: return "diverged";
        case SolveStatus::Stagnated: return "stagnated";
        case SolveStatus::Cancelled: return "cancelled";
        case SolveStatus::Breakdown: return "breakdown";
    }
    return "unknown";
}

// ---- Jacobi ---------------------------------------------------------------

JacobiPreconditioner::JacobiPreconditioner(const sparse::CsrMatrix& A) {
    inverseDiagonal_ = A.diagonal();
    for (double& d : inverseDiagonal_) {
        if (!(d > 0.0)) throw WorkbenchError(ErrorCode::NumericalFailure, "Jacobi preconditioner: non-positive diagonal");
        d = 1.0 / d;
    }
}

void JacobiPreconditioner::apply(const std::vector<double>& r, std::vector<double>& z) const {
    for (std::size_t i = 0; i < r.size(); ++i) z[i] = r[i] * inverseDiagonal_[i];
}

// ---- IC(0) ----------------------------------------------------------------

IncompleteCholesky::IncompleteCholesky(const sparse::CsrMatrix& A, const IcOptions& options) {
    const int attemptsAllowed = options.allowShift ? std::max(1, options.maxShiftAttempts) : 1;
    double alpha = 0.0;
    for (int a = 0; a < attemptsAllowed; ++a) {
        ++attempts_;
        if (factorise(A, alpha)) {
            ok_ = true;
            shift_ = alpha;
            return;
        }
        alpha = alpha == 0.0 ? 1e-3 : 2.0 * alpha;
    }
    if (options.allowShift) failure_ += " (no shift up to " + std::to_string(alpha / 2) + " helped)";
}

std::size_t IncompleteCholesky::bytes() const {
    return rowPtr_.size() * sizeof(std::size_t) + col_.size() * sizeof(std::uint32_t) + val_.size() * sizeof(double);
}

bool IncompleteCholesky::factorise(const sparse::CsrMatrix& A, double alpha) {
    const std::size_t n = A.rows();
    minPivotRatio_ = 1.0;
    rowPtr_.assign(n + 1, 0);
    col_.clear();
    val_.clear();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t p = A.rowPtr[i]; p < A.rowPtr[i + 1]; ++p) {
            if (A.col[p] > i) break;  // sorted: the rest is upper triangle
            col_.push_back(A.col[p]);
            val_.push_back(A.col[p] == i ? A.val[p] * (1.0 + alpha) : A.val[p]);
        }
        rowPtr_[i + 1] = col_.size();
        if (col_.empty() || col_.back() != i) {
            failure_ = "IC(0): row " + std::to_string(i) + " has no diagonal entry";
            return false;
        }
    }
    // Row-wise: L_ik = (a_ik - sum_j L_ij L_kj) / L_kk over j < k in both rows,
    // then L_ii = sqrt(a_ii - sum_k L_ik^2). Row k (< i) is already final.
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t ri = rowPtr_[i], re = rowPtr_[i + 1] - 1;  // re = diagonal position
        for (std::size_t p = ri; p < re; ++p) {
            const std::size_t k = col_[p];
            const std::size_t rk = rowPtr_[k], rkDiag = rowPtr_[k + 1] - 1;
            double s = val_[p];
            // Merge the sorted column lists of rows i and k, columns < k.
            std::size_t a = ri, b = rk;
            while (a < p && b < rkDiag) {
                if (col_[a] == col_[b]) {
                    s -= val_[a] * val_[b];
                    ++a;
                    ++b;
                } else if (col_[a] < col_[b]) {
                    ++a;
                } else {
                    ++b;
                }
            }
            val_[p] = s / val_[rkDiag];
        }
        double d = val_[re];
        for (std::size_t p = ri; p < re; ++p) d -= val_[p] * val_[p];
        if (!(d > 0.0) || !std::isfinite(d)) {
            failure_ = "IC(0): non-positive pivot " + std::to_string(d) + " at row " + std::to_string(i) +
                       (alpha > 0.0 ? " with shift " + std::to_string(alpha) : std::string(" without a shift"));
            return false;
        }
        minPivotRatio_ = std::min(minPivotRatio_, d / val_[re]);
        val_[re] = std::sqrt(d);
    }
    return true;
}

void IncompleteCholesky::apply(const std::vector<double>& r, std::vector<double>& z) const {
    const std::size_t n = r.size();
    // L y = r
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t re = rowPtr_[i + 1] - 1;
        double s = r[i];
        for (std::size_t p = rowPtr_[i]; p < re; ++p) s -= val_[p] * z[col_[p]];
        z[i] = s / val_[re];
    }
    // L^T z = y, in place, column-oriented
    for (std::size_t i = n; i-- > 0;) {
        const std::size_t re = rowPtr_[i + 1] - 1;
        z[i] /= val_[re];
        const double zi = z[i];
        for (std::size_t p = rowPtr_[i]; p < re; ++p) z[col_[p]] -= val_[p] * zi;
    }
}

// ---- CG / PCG -------------------------------------------------------------

SolveReport conjugateGradient(const sparse::CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                              const SolveOptions& opt, const Preconditioner* M) {
    using sparse::axpy;
    using sparse::dot;
    const std::size_t n = A.rows();
    if (b.size() != n) {
        throw WorkbenchError(ErrorCode::InvalidParameter, "right-hand side size does not match the matrix");
    }
    if (x.size() != n) x.assign(n, 0.0);

    SolveReport report;
    report.bytesAllocated = (M ? 5 : 4) * n * sizeof(double) + (M ? M->bytes() : 0);
    const Stopwatch timer;
    const double bNorm = sparse::norm2(b);
    if (bNorm == 0.0) {
        std::fill(x.begin(), x.end(), 0.0);
        report.status = SolveStatus::Converged;
        return report;
    }

    std::vector<double> r = sparse::residual(A, x, b), Ap(n), scratch(n), z;
    if (M) {
        z.assign(n, 0.0);
        M->apply(r, z);
    }
    const std::vector<double>& zr = M ? z : r;  // preconditioned residual
    std::vector<double> p = zr;
    const auto trueRelativeResidual = [&]() {
        A.multiply(x.data(), scratch.data());
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double d = b[i] - scratch[i];
            s += d * d;
        }
        return std::sqrt(s) / bNorm;
    };

    double rz = dot(r, zr);
    double relative = sparse::norm2(r) / bNorm;
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
        if (opt.maxSeconds > 0.0 && (it & 15) == 0 && timer.elapsedSeconds() > opt.maxSeconds) {
            report.status = SolveStatus::TimeLimit;
            break;
        }
        A.multiply(p.data(), Ap.data());
        const double pAp = dot(p, Ap);
        if (!(pAp > 0.0) || !std::isfinite(pAp)) {
            report.status = SolveStatus::Breakdown;  // p' A p <= 0: A is not positive definite
            report.detail = "p'Ap = " + std::to_string(pAp) + " at iteration " + std::to_string(it);
            break;
        }
        const double alpha = rz / pAp;
        axpy(alpha, p, x);
        axpy(-alpha, Ap, r);
        if (M) M->apply(r, z);
        const double rzNew = dot(r, zr);
        relative = sparse::norm2(r) / bNorm;
        report.iterations = it;
        if (opt.recordHistory) report.history.push_back(relative);
        if (opt.trueResidualEvery != 0 && it % opt.trueResidualEvery == 0) {
            report.trueResidualHistory.emplace_back(it, trueRelativeResidual());
        }
        const double beta = rzNew / rz;
        // A restart begins a new Lanczos sequence, so only the first run of
        // coefficients describes M^{-1} A.
        if (opt.recordCoefficients && report.residualReplacements == 0) {
            report.alphas.push_back(alpha);
            report.betas.push_back(beta);
        }
        if (relative <= opt.relativeTolerance) {
            // Never trust the recursive residual for the final verdict.
            const double trueRelative = trueRelativeResidual();
            if (trueRelative <= opt.relativeTolerance) {
                report.status = SolveStatus::Converged;
                report.relativeResidual = trueRelative;
                report.solveSeconds = timer.elapsedSeconds();
                return report;
            }
            // The recursive residual has drifted below the true one. Replace
            // it with b - A x and restart the search direction; carrying on
            // would chase a residual that no longer describes x.
            ++report.residualReplacements;
            if (report.residualReplacements > opt.maxResidualReplacements) {
                report.status = SolveStatus::Stagnated;
                report.detail = "true residual stuck at " + std::to_string(trueRelative) +
                                " after " + std::to_string(report.residualReplacements - 1) + " residual replacements";
                break;
            }
            r = sparse::residual(A, x, b);
            if (M) M->apply(r, z);
            p = zr;
            rz = dot(r, zr);
            continue;
        }
        for (std::size_t i = 0; i < n; ++i) p[i] = zr[i] + beta * p[i];
        rz = rzNew;
    }
    report.solveSeconds = timer.elapsedSeconds();
    report.relativeResidual = trueRelativeResidual();
    return report;
}

// ---- Lanczos eigenvalue estimate -------------------------------------------

namespace {

// Number of eigenvalues of the symmetric tridiagonal (d, e) below x.
std::size_t sturmCount(const std::vector<double>& d, const std::vector<double>& e, double x) {
    std::size_t count = 0;
    double q = 1.0;
    for (std::size_t i = 0; i < d.size(); ++i) {
        const double off = i == 0 ? 0.0 : e[i - 1] * e[i - 1];
        q = d[i] - x - (i == 0 ? 0.0 : off / q);
        if (q == 0.0) q = -1e-300;
        if (q < 0.0) ++count;
    }
    return count;
}

double kthEigenvalue(const std::vector<double>& d, const std::vector<double>& e, std::size_t k, double lo, double hi) {
    for (int it = 0; it < 200 && hi - lo > 1e-15 * std::max(std::abs(lo), std::abs(hi)); ++it) {
        const double mid = 0.5 * (lo + hi);
        if (sturmCount(d, e, mid) > k) hi = mid;
        else lo = mid;
    }
    return 0.5 * (lo + hi);
}

}  // namespace

EigenEstimate lanczosExtremeEigenvalues(const std::vector<double>& alphas, const std::vector<double>& betas) {
    EigenEstimate est;
    const std::size_t m = alphas.size();
    if (m == 0) return est;
    // T(k,k) = 1/alpha_k + beta_{k-1}/alpha_{k-1};  T(k,k+1) = sqrt(beta_k)/alpha_k
    std::vector<double> d(m), e(m > 0 ? m - 1 : 0);
    for (std::size_t k = 0; k < m; ++k) {
        d[k] = 1.0 / alphas[k] + (k == 0 ? 0.0 : betas[k - 1] / alphas[k - 1]);
        if (k + 1 < m) e[k] = std::sqrt(betas[k]) / alphas[k];
    }
    // Gershgorin bounds bracket every eigenvalue.
    double lo = 1e300, hi = -1e300;
    for (std::size_t k = 0; k < m; ++k) {
        const double radius = (k > 0 ? std::abs(e[k - 1]) : 0.0) + (k + 1 < m ? std::abs(e[k]) : 0.0);
        lo = std::min(lo, d[k] - radius);
        hi = std::max(hi, d[k] + radius);
    }
    est.lambdaMin = kthEigenvalue(d, e, 0, lo, hi);
    est.lambdaMax = kthEigenvalue(d, e, m - 1, lo, hi);
    return est;
}

}  // namespace wb::solvers
