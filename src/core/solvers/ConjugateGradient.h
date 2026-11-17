// Conjugate gradient for symmetric positive definite systems, with optional
// preconditioning (none, Jacobi, or zero-fill incomplete Cholesky).
//
// The residual is updated recursively (r -= alpha A p), which is what makes
// CG one matrix-vector product per iteration. In floating point that
// recursive residual drifts away from the true b - A x, so every
// `trueResidualEvery` iterations, and before declaring convergence, the true
// residual is computed explicitly. The report keeps both so the drift can be
// plotted.
//
// The CG coefficients are the Lanczos tridiagonal matrix of M^{-1} A in
// disguise; lanczosExtremeEigenvalues() turns them into an estimate of the
// condition number at no extra cost.
#pragma once

#include "core/solvers/LinearSolver.h"
#include "core/sparse/CsrMatrix.h"

#include <memory>
#include <string>
#include <vector>

namespace wb::solvers {

class Preconditioner {
public:
    virtual ~Preconditioner() = default;
    // z = M^{-1} r
    virtual void apply(const std::vector<double>& r, std::vector<double>& z) const = 0;
    virtual std::size_t bytes() const = 0;
    virtual std::string name() const = 0;
};

class JacobiPreconditioner : public Preconditioner {
public:
    explicit JacobiPreconditioner(const sparse::CsrMatrix& A);
    void apply(const std::vector<double>& r, std::vector<double>& z) const override;
    std::size_t bytes() const override { return inverseDiagonal_.size() * sizeof(double); }
    std::string name() const override { return "jacobi"; }

private:
    std::vector<double> inverseDiagonal_;
};

// Incomplete Cholesky with zero fill: L has exactly the pattern of the lower
// triangle of A, and L L^T matches A on that pattern. For an M-matrix it
// always exists; trilinear hex matrices on stretched cells have positive
// off-diagonals and are not M-matrices, so a pivot can turn non-positive.
// With `allowShift`, the factorisation then restarts on A + alpha diag(A)
// with alpha = 1e-3, 2e-3, 4e-3, ... (Manteuffel's shift); without it, the
// failure is reported, for the breakdown experiment.
struct IcOptions {
    bool allowShift = true;
    int maxShiftAttempts = 20;
};

class IncompleteCholesky : public Preconditioner {
public:
    IncompleteCholesky(const sparse::CsrMatrix& A, const IcOptions& options);
    bool ok() const { return ok_; }
    double shift() const { return shift_; }        // alpha used, 0 if none was needed
    int attempts() const { return attempts_; }     // factorisations tried
    const std::string& failure() const { return failure_; }
    // min over rows of L_ii^2 / a_ii: 1 means nothing was subtracted, 0 is
    // breakdown. How close the factorisation came to failing.
    double minPivotRatio() const { return minPivotRatio_; }
    void apply(const std::vector<double>& r, std::vector<double>& z) const override;
    std::size_t bytes() const override;
    std::string name() const override { return "ic0"; }

private:
    bool factorise(const sparse::CsrMatrix& A, double alpha);
    // Lower triangle of A in CSR, diagonal last in each row.
    std::vector<std::size_t> rowPtr_;
    std::vector<std::uint32_t> col_;
    std::vector<double> val_;
    bool ok_ = false;
    double shift_ = 0.0;
    int attempts_ = 0;
    double minPivotRatio_ = 1.0;
    std::string failure_;
};

// Solves A x = b starting from x (zeros if x is empty or the wrong size).
// `preconditioner` may be null (plain CG). Throws InvalidParameter if the
// sizes disagree.
SolveReport conjugateGradient(const sparse::CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                              const SolveOptions& options, const Preconditioner* preconditioner = nullptr);

struct EigenEstimate {
    double lambdaMin = 0.0, lambdaMax = 0.0;
    double condition() const { return lambdaMax / lambdaMin; }
};

// Extreme eigenvalues of the Lanczos tridiagonal built from CG's alphas and
// betas (Sturm-sequence bisection). They converge to the extreme
// eigenvalues of M^{-1} A from inside, so the condition number is a lower
// bound that becomes accurate once CG has run long enough.
EigenEstimate lanczosExtremeEigenvalues(const std::vector<double>& alphas, const std::vector<double>& betas);

}  // namespace wb::solvers
