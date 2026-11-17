#include "TestHarness.h"

#include "core/common/Errors.h"
#include "core/solvers/ConjugateGradient.h"
#include "core/sparse/CsrMatrix.h"
#include "core/sparse/VectorOps.h"

#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>

using namespace wb;

namespace {

using Dense = std::vector<std::vector<double>>;

sparse::CsrMatrix toCsr(const Dense& d) {
    sparse::CsrMatrix A;
    A.rowPtr.push_back(0);
    for (const auto& row : d) {
        for (std::size_t c = 0; c < row.size(); ++c) {
            if (row[c] != 0.0) {
                A.col.push_back(static_cast<std::uint32_t>(c));
                A.val.push_back(row[c]);
            }
        }
        A.rowPtr.push_back(A.col.size());
    }
    return A;
}

// B^T B + n I: symmetric positive definite, reproducible.
Dense randomSpd(std::size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    Dense B(n, std::vector<double>(n));
    for (auto& row : B)
        for (double& v : row) v = u(rng);
    Dense A(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
            for (std::size_t k = 0; k < n; ++k) A[i][j] += B[k][i] * B[k][j];
    for (std::size_t i = 0; i < n; ++i) A[i][i] += static_cast<double>(n);
    return A;
}

std::vector<double> gaussianElimination(Dense A, std::vector<double> b) {
    const std::size_t n = b.size();
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t pivot = k;
        for (std::size_t i = k + 1; i < n; ++i)
            if (std::abs(A[i][k]) > std::abs(A[pivot][k])) pivot = i;
        std::swap(A[k], A[pivot]);
        std::swap(b[k], b[pivot]);
        for (std::size_t i = k + 1; i < n; ++i) {
            const double f = A[i][k] / A[k][k];
            for (std::size_t j = k; j < n; ++j) A[i][j] -= f * A[k][j];
            b[i] -= f * b[k];
        }
    }
    std::vector<double> x(n);
    for (std::size_t i = n; i-- > 0;) {
        double s = b[i];
        for (std::size_t j = i + 1; j < n; ++j) s -= A[i][j] * x[j];
        x[i] = s / A[i][i];
    }
    return x;
}

}  // namespace

TEST(cg_matches_a_dense_solve_on_a_random_spd_matrix) {
    const std::size_t n = 12;
    const Dense D = randomSpd(n, 42);
    std::vector<double> b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = std::sin(1.0 + static_cast<double>(i));
    const auto reference = gaussianElimination(D, b);
    const auto A = toCsr(D);
    std::vector<double> x;
    solvers::SolveOptions opt;
    opt.relativeTolerance = 1e-14;
    opt.recordHistory = true;
    const auto rep = solvers::conjugateGradient(A, b, x, opt);
    CHECK(rep.converged());
    CHECK(rep.iterations <= n + 2);  // finite termination, give or take round-off
    for (std::size_t i = 0; i < n; ++i) CHECK_NEAR(x[i], reference[i], 1e-10);
    CHECK(rep.history.size() == rep.iterations + 1);
    CHECK(rep.relativeResidual <= 1e-14);
    CHECK(rep.bytesAllocated == 4 * n * sizeof(double));
}

TEST(cg_reports_zero_rhs_breakdown_cancellation_and_bad_sizes) {
    const auto A = toCsr(randomSpd(5, 7));
    std::vector<double> x;
    const std::vector<double> zero(5, 0.0);
    auto rep = solvers::conjugateGradient(A, zero, x, {});
    CHECK(rep.converged());
    CHECK(rep.iterations == 0);
    for (double v : x) CHECK(v == 0.0);

    Dense negative(3, std::vector<double>(3, 0.0));
    for (int i = 0; i < 3; ++i) negative[i][i] = -1.0;
    const std::vector<double> b3{1, 2, 3};
    rep = solvers::conjugateGradient(toCsr(negative), b3, x, {});
    CHECK(rep.status == solvers::SolveStatus::Breakdown);

    CancelToken token;
    token.requestCancel();
    solvers::SolveOptions opt;
    opt.cancel = &token;
    rep = solvers::conjugateGradient(A, std::vector<double>(5, 1.0), x, opt);
    CHECK(rep.status == solvers::SolveStatus::Cancelled);
    CHECK(rep.iterations == 0);

    CHECK_THROWS_CODE(solvers::conjugateGradient(A, b3, x, {}), ErrorCode::InvalidParameter);
}

TEST(cg_checks_the_true_residual_and_honours_the_iteration_cap) {
    const std::size_t n = 40;
    const auto A = toCsr(randomSpd(n, 3));
    const std::vector<double> b(n, 1.0);
    std::vector<double> x;
    solvers::SolveOptions opt;
    opt.relativeTolerance = 1e-12;
    opt.trueResidualEvery = 5;
    const auto rep = solvers::conjugateGradient(A, b, x, opt);
    CHECK(rep.converged());
    CHECK(!rep.trueResidualHistory.empty());
    for (const auto& [it, res] : rep.trueResidualHistory) {
        CHECK(it % 5 == 0);
        CHECK(res >= 0.0);
    }
    const double explicitResidual = sparse::norm2(sparse::residual(A, x, b)) / sparse::norm2(b);
    CHECK_NEAR(rep.relativeResidual, explicitResidual, 1e-16);

    solvers::SolveOptions capped;
    capped.relativeTolerance = 1e-14;
    capped.maxIterations = 2;
    std::vector<double> y;
    const auto rep2 = solvers::conjugateGradient(A, b, y, capped);
    CHECK(rep2.status == solvers::SolveStatus::MaxIterations);
    CHECK(rep2.iterations == 2);
    CHECK(rep2.relativeResidual > 1e-14);
}

TEST(csr_helpers_behave) {
    const Dense d = {{4, 1, 0}, {1, 3, 2}, {0, 2, 5}};
    auto A = toCsr(d);
    CHECK(A.rows() == 3);
    CHECK(A.nnz() == 7);
    CHECK(A.at(1, 2) == 2.0);
    CHECK(A.at(0, 2) == 0.0);
    A.add(1, 2, 0.5);
    CHECK(A.at(1, 2) == 2.5);
    CHECK(A.symmetryDefect() == 0.5);
    CHECK(A.halfBandwidth() == 1);
    const auto y = A.multiply({1, 1, 1});
    CHECK_NEAR(y[1], 6.5, 1e-15);
    CHECK(A.diagonal()[2] == 5.0);
    bool threw = false;
    try {
        A.add(0, 2, 1.0);
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(A.bytes() == 4 * sizeof(std::size_t) + 7 * (sizeof(std::uint32_t) + sizeof(double)));
}
