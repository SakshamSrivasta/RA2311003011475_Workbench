// Direct solver: Cholesky factorisation A = L L^T stored as a skyline
// (variable-band, "envelope") matrix.
//
// Row i of L is stored from its first non-zero column f_i up to the diagonal.
// Fill-in can only happen inside that envelope, so the storage is known
// before factorising: profile + n doubles. That lets the solver refuse a
// problem it cannot fit instead of running out of memory half way, and the
// harness reports the refusal with its predicted size.
//
// Factorisation (row-oriented, "bordering"): for each row i, for each column
// j in [f_i, i):   L_ij = (a_ij - sum_k L_ik L_jk) / L_jj,  k over the
// overlap of the two rows' envelopes; then L_ii = sqrt(a_ii - sum_k L_ik^2).
// Work is about sum_i (i - f_i)^2 / 2 multiply-adds, i.e. n b^2 / 2 for a
// band of width b. A non-positive pivot means A is not SPD: the solver stops
// with Breakdown and the row number instead of producing NaNs.
#pragma once

#include "core/solvers/LinearSolver.h"
#include "core/sparse/CsrMatrix.h"
#include "core/sparse/Ordering.h"

#include <cstddef>
#include <vector>

namespace wb::solvers {

enum class Ordering { Natural, ReverseCuthillMcKee };

struct SkylineOptions {
    Ordering ordering = Ordering::ReverseCuthillMcKee;
    std::size_t memoryCapBytes = std::size_t{3} << 30;  // refuse a factor larger than this
    const CancelToken* cancel = nullptr;
};

struct SkylinePlan {
    std::size_t n = 0;
    std::size_t profile = 0;     // stored entries below the diagonal
    std::size_t halfBandwidth = 0;
    double factorBytes = 0.0;    // (profile + n) * 8 plus the row pointers
    double factorFlops = 0.0;    // multiply-adds, sum over rows of (i - f_i)^2 / 2 plus divisions
};

// The profile and cost of factorising A under an ordering, without factorising.
SkylinePlan planSkyline(const sparse::CsrMatrix& A, const sparse::Permutation& perm);

class SkylineCholesky {
public:
    // Orders and factorises A (setup), or records why it could not.
    SkylineCholesky(const sparse::CsrMatrix& A, const SkylineOptions& options);

    bool ok() const { return status_ == SolveStatus::Converged; }
    SolveStatus status() const { return status_; }
    const std::string& detail() const { return detail_; }
    const SkylinePlan& plan() const { return plan_; }
    double setupSeconds() const { return setupSeconds_; }
    std::size_t bytes() const;

    // x = A^{-1} b by forward and backward substitution. Requires ok().
    std::vector<double> solve(const std::vector<double>& b) const;

    // Row i of L as (first column, values up to and including the diagonal).
    std::size_t firstColumn(std::size_t i) const { return first_[i]; }
    const sparse::Permutation& permutation() const { return perm_; }

private:
    sparse::Permutation perm_;
    std::vector<std::size_t> first_;   // f_i
    std::vector<std::size_t> start_;   // offset of L(i, f_i) in values_
    std::vector<double> values_;
    SkylinePlan plan_;
    SolveStatus status_ = SolveStatus::Breakdown;
    std::string detail_;
    double setupSeconds_ = 0.0;
};

// Order, factorise and solve in one call; fills a SolveReport like the iterative solvers.
SolveReport skylineSolve(const sparse::CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                         const SkylineOptions& options);

}  // namespace wb::solvers
