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

// ---- Direct, stationary and preconditioned solvers on real FE systems -------

#include "core/fem3d/Assembler.h"
#include "core/fem3d/Cases.h"
#include "core/solvers/SkylineCholesky.h"
#include "core/solvers/Stationary.h"
#include "core/sparse/Ordering.h"

#include <algorithm>
#include <numeric>

namespace {

fem3d::ReducedSystem reducedFor(const fem3d::StudyCase& c) {
    auto m = fem3d::buildMesh(c);
    return fem3d::eliminateDirichlet(fem3d::assemble(m, c.problem));
}

double maxDiff(const std::vector<double>& a, const std::vector<double>& b) {
    double m = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) m = std::max(m, std::abs(a[i] - b[i]));
    return m;
}

}  // namespace

TEST(skyline_matches_a_dense_solve_with_both_orderings) {
    const std::size_t n = 15;
    const Dense D = randomSpd(n, 11);
    std::vector<double> b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = std::cos(0.3 * static_cast<double>(i));
    const auto reference = gaussianElimination(D, b);
    for (auto ordering : {solvers::Ordering::Natural, solvers::Ordering::ReverseCuthillMcKee}) {
        std::vector<double> x;
        solvers::SkylineOptions opt;
        opt.ordering = ordering;
        const auto rep = solvers::skylineSolve(toCsr(D), b, x, opt);
        CHECK(rep.converged());
        for (std::size_t i = 0; i < n; ++i) CHECK_NEAR(x[i], reference[i], 1e-11);
        CHECK(rep.relativeResidual < 1e-14);
    }
}

TEST(skyline_plan_profile_matches_the_permuted_matrix) {
    const auto sys = reducedFor(fem3d::heatSink(2e-3, false));
    for (auto ordering : {solvers::Ordering::Natural, solvers::Ordering::ReverseCuthillMcKee}) {
        const auto perm = ordering == solvers::Ordering::Natural ? sparse::identityPermutation(sys.A.rows())
                                                                 : sparse::reverseCuthillMcKee(sys.A);
        std::vector<std::size_t> sorted = perm;
        std::sort(sorted.begin(), sorted.end());
        for (std::size_t i = 0; i < sorted.size(); ++i) CHECK(sorted[i] == i);  // a true permutation
        const auto plan = solvers::planSkyline(sys.A, perm);
        CHECK(plan.profile == sparse::profile(sparse::permuteSymmetric(sys.A, perm)));
        CHECK(plan.halfBandwidth == sparse::permuteSymmetric(sys.A, perm).halfBandwidth());
    }
}

TEST(rcm_recovers_a_small_profile_from_a_scrambled_numbering) {
    const auto sys = reducedFor(fem3d::rodCube(10));
    sparse::Permutation scramble(sys.A.rows());
    std::iota(scramble.begin(), scramble.end(), std::size_t{0});
    std::mt19937 rng(5);
    std::shuffle(scramble.begin(), scramble.end(), rng);
    const auto scrambled = sparse::permuteSymmetric(sys.A, scramble);
    const auto natural = sparse::profile(sys.A);
    const auto bad = sparse::profile(scrambled);
    const auto rcm = sparse::profile(sparse::permuteSymmetric(scrambled, sparse::reverseCuthillMcKee(scrambled)));
    CHECK(bad > 4 * natural);
    CHECK(rcm < 2 * natural);
}

TEST(skyline_refuses_oversized_factors_and_detects_non_spd) {
    const auto sys = reducedFor(fem3d::rodCube(8));
    std::vector<double> x;
    solvers::SkylineOptions opt;
    opt.memoryCapBytes = 1000;
    auto rep = solvers::skylineSolve(sys.A, sys.b, x, opt);
    CHECK(rep.status == solvers::SolveStatus::Refused);
    CHECK(!rep.detail.empty());

    Dense indefinite = {{1, 2}, {2, 1}};
    rep = solvers::skylineSolve(toCsr(indefinite), {1, 1}, x, {});
    CHECK(rep.status == solvers::SolveStatus::Breakdown);
}

TEST(sor_converges_below_two_and_diverges_above) {
    const auto sys = reducedFor(fem3d::manufacturedCube(8));
    solvers::SolveOptions opt;
    opt.relativeTolerance = 1e-8;
    opt.trueResidualEvery = 1;
    std::size_t sweeps[3] = {0, 0, 0};
    int i = 0;
    for (double omega : {1.0, 1.5}) {
        std::vector<double> x;
        const auto rep = solvers::sor(sys.A, sys.b, x, omega, opt);
        CHECK(rep.converged());
        sweeps[i++] = rep.iterations;
    }
    CHECK(sweeps[1] < sweeps[0]);  // over-relaxation helps
    std::vector<double> xj;
    const auto jac = solvers::jacobi(sys.A, sys.b, xj, opt);
    CHECK(jac.converged());
    CHECK(jac.iterations > static_cast<std::size_t>(1.6 * static_cast<double>(sweeps[0])));  // GS about halves Jacobi

    std::vector<double> x;
    opt.maxIterations = 5000;
    const auto rep = solvers::sor(sys.A, sys.b, x, 2.05, opt);
    CHECK(rep.status == solvers::SolveStatus::Diverged);
    CHECK_THROWS_CODE(solvers::sor(sys.A, sys.b, x, 0.0, opt), ErrorCode::InvalidParameter);
}

TEST(all_solvers_agree_on_the_contrast_heat_sink) {
    const auto sys = reducedFor(fem3d::heatSink(2e-3, true));
    std::vector<double> xd;
    const auto direct = solvers::skylineSolve(sys.A, sys.b, xd, {});
    CHECK(direct.converged());
    CHECK(direct.relativeResidual < 1e-12);

    solvers::SolveOptions opt;
    opt.relativeTolerance = 1e-12;
    std::vector<double> xcg, xj, xic, xsor;
    const auto cg = solvers::conjugateGradient(sys.A, sys.b, xcg, opt);
    const solvers::JacobiPreconditioner jacobi(sys.A);
    const auto pj = solvers::conjugateGradient(sys.A, sys.b, xj, opt, &jacobi);
    const solvers::IncompleteCholesky ic(sys.A, {});
    CHECK(ic.ok());
    const auto pic = solvers::conjugateGradient(sys.A, sys.b, xic, opt, &ic);
    opt.trueResidualEvery = 10;
    const auto s = solvers::sor(sys.A, sys.b, xsor, 1.9, opt);
    for (const auto* r : {&cg, &pj, &pic, &s}) CHECK(r->converged());
    // Temperatures are O(100) C; agreement far below the 1e-8 C used in Round 1.
    CHECK(maxDiff(xcg, xd) < 1e-8);
    CHECK(maxDiff(xj, xd) < 1e-8);
    CHECK(maxDiff(xic, xd) < 1e-8);
    CHECK(maxDiff(xsor, xd) < 1e-7);
    CHECK(pj.iterations < cg.iterations);
    CHECK(pic.iterations < pj.iterations);
}

TEST(ic0_shift_kicks_in_only_when_needed) {
    // FE conduction matrices factorise without a shift (see the solver study).
    const solvers::IncompleteCholesky cube(reducedFor(fem3d::rodCube(6)).A, {});
    CHECK(cube.ok());
    CHECK(cube.shift() == 0.0);
    CHECK(cube.attempts() == 1);

    // A 4-node cycle: SPD (its full Cholesky exists), but eliminating node 0
    // creates fill at (3,1), which IC(0) drops, and the last pivot goes negative.
    const Dense cycle = {{1, -0.6, 0, -0.55}, {-0.6, 1, 0.75, 0}, {0, 0.75, 1, -0.45}, {-0.55, 0, -0.45, 1}};
    std::vector<double> x;
    CHECK(solvers::skylineSolve(toCsr(cycle), {1, 1, 1, 1}, x, {}).converged());  // full Cholesky: SPD
    solvers::IcOptions noShift;
    noShift.allowShift = false;
    const solvers::IncompleteCholesky failed(toCsr(cycle), noShift);
    CHECK(!failed.ok());
    CHECK(failed.attempts() == 1);
    CHECK(failed.failure().find("non-positive pivot") != std::string::npos);
    const solvers::IncompleteCholesky shifted(toCsr(cycle), {});
    CHECK(shifted.ok());
    CHECK(shifted.shift() > 0.0);
    CHECK(shifted.attempts() > 1);
    solvers::SolveOptions opt;
    opt.relativeTolerance = 1e-12;
    CHECK(solvers::conjugateGradient(toCsr(cycle), {1, 1, 1, 1}, x, opt, &shifted).converged());
}

TEST(cg_residual_replacement_rescues_a_drifting_recursive_residual) {
    // Plain CG on the contrast heat sink at 1e-12: the recursive residual
    // reaches the tolerance before the true one does.
    const auto sys = reducedFor(fem3d::heatSink(2e-3, true));
    std::vector<double> x;
    solvers::SolveOptions opt;
    opt.relativeTolerance = 1e-12;
    const auto rep = solvers::conjugateGradient(sys.A, sys.b, x, opt);
    CHECK(rep.converged());
    CHECK(rep.residualReplacements >= 1);
    CHECK(rep.iterations < 2000);
    CHECK(rep.relativeResidual <= 1e-12);
    // Asking for less than round-off allows ends in Stagnated, not a fake success.
    opt.relativeTolerance = 1e-17;
    opt.maxResidualReplacements = 2;
    const auto floor = solvers::conjugateGradient(sys.A, sys.b, x, opt);
    CHECK(floor.status == solvers::SolveStatus::Stagnated);
    CHECK(floor.relativeResidual > 1e-17);
}

TEST(lanczos_estimate_recovers_known_extreme_eigenvalues) {
    const std::size_t n = 60;
    Dense D(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i < n; ++i) D[i][i] = 1.0 + static_cast<double>(i);  // eigenvalues 1..60
    std::vector<double> x;
    solvers::SolveOptions opt;
    opt.relativeTolerance = 1e-13;
    opt.recordCoefficients = true;
    const auto rep = solvers::conjugateGradient(toCsr(D), std::vector<double>(n, 1.0), x, opt);
    CHECK(rep.converged());
    const auto est = solvers::lanczosExtremeEigenvalues(rep.alphas, rep.betas);
    CHECK_NEAR(est.lambdaMin, 1.0, 1e-6);
    CHECK_NEAR(est.lambdaMax, 60.0, 1e-6);
    CHECK_NEAR(est.condition(), 60.0, 1e-4);
}

TEST(solvers_are_bitwise_deterministic) {
    const auto sys = reducedFor(fem3d::heatSink(2e-3, true));
    const solvers::IncompleteCholesky ic(sys.A, {});
    solvers::SolveOptions opt;
    opt.relativeTolerance = 1e-10;
    std::vector<double> a, b, c, d;
    solvers::conjugateGradient(sys.A, sys.b, a, opt, &ic);
    solvers::conjugateGradient(sys.A, sys.b, b, opt, &ic);
    CHECK(a == b);
    solvers::sor(sys.A, sys.b, c, 1.8, opt);
    solvers::sor(sys.A, sys.b, d, 1.8, opt);
    CHECK(c == d);
}
