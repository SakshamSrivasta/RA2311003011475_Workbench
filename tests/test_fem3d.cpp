#include "TestHarness.h"

#include "core/common/Errors.h"
#include "core/fea/ThermalRod1D.h"
#include "core/fem3d/Assembler.h"
#include "core/fem3d/Cases.h"
#include "core/fem3d/HexElement.h"
#include "core/fem3d/Postprocess.h"
#include "core/solvers/ConjugateGradient.h"
#include "core/sparse/CsrMatrix.h"

#include <algorithm>
#include <cmath>

using namespace wb;
using fem3d::Matrix8;

namespace {

constexpr double mm = 1e-3;

struct Solved {
    mesh::HexMesh mesh;
    fem3d::FullSystem full;
    std::vector<double> T;
    solvers::SolveReport report;
};

Solved solveCase(const fem3d::StudyCase& c, double tolerance = 1e-12) {
    Solved s;
    s.mesh = fem3d::buildMesh(c);
    s.full = fem3d::assemble(s.mesh, c.problem);
    const auto reduced = fem3d::eliminateDirichlet(s.full);
    std::vector<double> x;
    solvers::SolveOptions opt;
    opt.relativeTolerance = tolerance;
    s.report = solvers::conjugateGradient(reduced.A, reduced.b, x, opt);
    s.T = fem3d::expandSolution(reduced, x);
    return s;
}

double maxAbs(const Matrix8& a) {
    double m = 0.0;
    for (const auto& row : a)
        for (double v : row) m = std::max(m, std::abs(v));
    return m;
}

double maxAbsDiff(const Matrix8& a, const Matrix8& b) {
    double m = 0.0;
    for (int l = 0; l < 8; ++l)
        for (int q = 0; q < 8; ++q) m = std::max(m, std::abs(a[l][q] - b[l][q]));
    return m;
}

}  // namespace

TEST(element_closed_form_matches_gauss_quadrature) {
    const double cases[][4] = {{1, 1, 1, 1}, {0.002, 0.0117, 0.0005, 200.0}, {3, 0.5, 2, 7}};
    for (const auto& c : cases) {
        const Matrix8 K1 = fem3d::stiffness(c[0], c[1], c[2], c[3]);
        const Matrix8 K2 = fem3d::stiffnessByQuadrature(c[0], c[1], c[2], c[3]);
        CHECK(maxAbsDiff(K1, K2) <= 1e-12 * maxAbs(K1));
    }
}

TEST(element_stiffness_is_symmetric_with_zero_row_sums) {
    const Matrix8 K = fem3d::stiffness(0.3, 0.7, 1.1, 5.0);
    for (int l = 0; l < 8; ++l) {
        double sum = 0.0;
        for (int m = 0; m < 8; ++m) {
            sum += K[l][m];
            CHECK_NEAR(K[l][m], K[m][l], 1e-14);
        }
        CHECK_NEAR(sum, 0.0, 1e-13);  // a uniform temperature conducts nothing
        CHECK(K[l][l] > 0.0);
    }
}

TEST(axis_neighbour_coupling_sign_depends_on_aspect_ratio) {
    // Nodes 0 and 1 differ only in x; 0 and 4 only in z.
    const Matrix8 cube = fem3d::stiffness(1, 1, 1, 1);
    CHECK_NEAR(cube[0][1], 0.0, 1e-15);  // exactly zero for a cube
    CHECK(cube[0][3] < 0.0);             // face diagonal
    CHECK(cube[0][7] < 0.0);             // body diagonal
    const Matrix8 thin = fem3d::stiffness(2, 2, 0.5, 1);  // the pad layer of variant B
    CHECK(thin[0][1] > 0.0);  // positive off-diagonal along a long axis: no longer an M-matrix
    CHECK(thin[0][4] < 0.0);  // across the thin direction it stays negative
    const double a = 2, b = 2, c = 0.5;
    CHECK_NEAR(thin[0][1], -b * c / (9 * a) + a * c / (18 * b) + a * b / (18 * c), 1e-14);
}

TEST(body_load_face_mass_and_shape_functions_are_consistent) {
    const auto f = fem3d::bodyLoad(0.5, 2.0, 3.0, 10.0);
    for (double v : f) CHECK_NEAR(v, 10.0 * 3.0 / 8.0, 1e-14);
    const auto g = fem3d::bodyLoad(0.5, 2.0, 3.0, {1, 2, 3}, [](const mesh::Point&) { return 10.0; });
    for (int l = 0; l < 8; ++l) CHECK_NEAR(g[l], f[l], 1e-13);
    // a linear source integrates exactly with two Gauss points
    const auto lin = fem3d::bodyLoad(1.0, 1.0, 1.0, {0, 0, 0}, [](const mesh::Point& p) { return p[0]; });
    double total = 0.0;
    for (double v : lin) total += v;
    CHECK_NEAR(total, 0.5, 1e-14);  // integral of x over the unit cube
    const auto M = fem3d::faceMass(0.4, 0.9);
    for (int q = 0; q < 4; ++q) {
        double sum = 0.0;
        for (int r = 0; r < 4; ++r) {
            sum += M[q][r];
            CHECK_NEAR(M[q][r], M[r][q], 1e-15);
        }
        CHECK_NEAR(sum, 0.4 * 0.9 / 4, 1e-15);
    }
    const auto N = fem3d::shapeFunctions(0.2, 0.3, 0.4);
    double sum = 0.0;
    for (double v : N) sum += v;
    CHECK_NEAR(sum, 1.0, 1e-15);
    CHECK_NEAR(N[7], 0.2 * 0.3 * 0.4, 1e-15);
}

TEST(assembled_matrix_is_symmetric_and_conserves_heat) {
    const auto c = fem3d::rodCube(4);
    auto m = fem3d::buildMesh(c);
    const auto full = fem3d::assemble(m, c.problem);
    CHECK(full.K.rows() == 125);
    CHECK(full.K.symmetryDefect() < 1e-12);
    double worstRowSum = 0.0;
    for (std::size_t r = 0; r < full.K.rows(); ++r) {
        double s = 0.0;
        for (std::size_t p = full.K.rowPtr[r]; p < full.K.rowPtr[r + 1]; ++p) s += full.K.val[p];
        worstRowSum = std::max(worstRowSum, std::abs(s));
    }
    CHECK(worstRowSum < 1e-12);  // no convection: K times a constant is zero
    double totalLoad = 0.0;
    for (double v : full.b) totalLoad += v;
    CHECK_NEAR(totalLoad, 1e4 * 1.0, 1e-9);  // q V
    CHECK(full.dirichletNodes.size() == 2 * 25);

    // With convection the row sums turn positive; the matrix stays symmetric.
    const auto hs = fem3d::heatSink(2 * mm, false);
    auto hm = fem3d::buildMesh(hs);
    const auto hfull = fem3d::assemble(hm, hs.problem);
    CHECK(hfull.K.symmetryDefect() < 1e-12);
    CHECK(hfull.dirichletNodes.empty());
    double totalRhs = 0.0;
    for (double v : hfull.b) totalRhs += v;
    CHECK_NEAR(totalRhs, 50.0 + 50.0 * 25.0 * 16000e-6, 1e-9);  // chip power + h T_inf A
}

TEST(problem_validation_catches_bad_setups) {
    auto c = fem3d::rodCube(3);
    c.problem.dirichlet.clear();  // nothing fixes the temperature level
    CHECK_THROWS_CODE(fem3d::buildMesh(c), ErrorCode::InvalidParameter);
    c = fem3d::rodCube(3);
    c.problem.convection.push_back({9, 10.0, 20.0});  // a tag no face carries
    CHECK_THROWS_CODE(fem3d::buildMesh(c), ErrorCode::InvalidParameter);
    c = fem3d::rodCube(3);
    c.problem.materials[0].conductivity_W_mK = 0.0;
    CHECK_THROWS_CODE(fem3d::buildMesh(c), ErrorCode::InvalidParameter);
    c = fem3d::rodCube(3);
    c.problem.materials.clear();
    CHECK_THROWS_CODE(fem3d::buildMesh(c), ErrorCode::InvalidParameter);
    c = fem3d::rodCube(3);
    c.problem.convection.push_back({1, -5.0, 20.0});
    CHECK_THROWS_CODE(fem3d::buildMesh(c), ErrorCode::InvalidParameter);
    c = fem3d::rodCube(3);
    c.geometry.boxes[0].material = 3;  // no such material
    CHECK_THROWS_CODE(fem3d::buildMesh(c), ErrorCode::InvalidParameter);
}

TEST(bar_reproduces_the_round1_rod) {
    const auto c = fem3d::rodBar(10);
    const auto s = solveCase(c);
    CHECK(s.report.converged());
    fea::RodProblem rod;
    rod.elements = 10;
    const auto rodSolution = fea::solve(rod);
    for (std::size_t n = 0; n < s.mesh.nodeCount(); ++n) {
        const double x = s.mesh.nodePosition(n)[0];
        const auto i = static_cast<std::size_t>(std::lround(x / 0.1));
        CHECK_NEAR(s.T[n], rodSolution.temperature_C[i], 1e-8);
        CHECK_NEAR(s.T[n], c.exact({x, 0, 0}), 1e-8);
    }
}

TEST(cube_reproduces_the_round1_rod_including_its_midpoint_error) {
    const auto c = fem3d::rodCube(6);
    const auto s = solveCase(c);
    CHECK(s.report.converged());
    const auto err = fem3d::errorNorms(s.mesh, s.T, c.exact);
    CHECK(err.maxNodal_C < 1e-8);  // nodally exact, like the 1D rod
    // Inside an element the FE field is below the parabola by q h^2 / (8k) at
    // the centre, the same number Round 1 reported at the rod midpoints.
    const double h = 1.0 / 6;
    const auto& nodes = s.mesh.elements[0];
    double centre = 0.0;
    for (std::size_t n : nodes) centre += s.T[n] / 8;
    const auto o = s.mesh.elementOrigin(0);
    CHECK_NEAR(centre - c.exact({o[0] + h / 2, o[1] + h / 2, o[2] + h / 2}), -1e4 * h * h / (8 * 10.0), 1e-8);
}

TEST(patch_test_linear_field_on_a_graded_mesh) {
    mesh::BoxGeometry g;
    g.boxes = {mesh::makeBox(0, 0, 0, 1, 0.8, 0.6)};
    // Same material, so this box only adds grid lines: the spacing becomes irregular.
    g.boxes.push_back(mesh::makeBox(0.1, 0.15, 0.05, 0.45, 0.5, 0.33, 0));
    fem3d::StudyCase c;
    c.name = "patch";
    c.geometry = g;
    c.problem.materials = {{"m", 3.0, 0.0}};
    fem3d::DirichletBc bc;
    bc.tag = 0;
    bc.temperature_C = [](const mesh::Point& p) { return 7 + 2 * p[0] - 3 * p[1] + 5 * p[2]; };
    c.problem.dirichlet = {bc};
    c.spacing_m = {0.3, 0.25, 0.2};
    const auto s = solveCase(c);
    CHECK(s.report.converged());
    bool graded = false;
    for (int i = 1; i < s.mesh.grid.cells(0); ++i) {
        if (std::abs(s.mesh.grid.cellSize(0, i) - s.mesh.grid.cellSize(0, 0)) > 1e-12) graded = true;
    }
    CHECK(graded);
    const auto err = fem3d::errorNorms(s.mesh, s.T, bc.temperature_C);
    CHECK(err.maxNodal_C < 1e-9);
    CHECK(err.rms_C < 1e-9);
}

TEST(two_materials_in_series_are_nodally_exact) {
    fem3d::StudyCase c = fem3d::rodBar(10);
    c.problem.materials = {{"a", 10.0, 0.0}, {"b", 2.0, 0.0}};
    c.geometry.boxes.push_back(mesh::makeBox(0.4, 0, 0, 1.0, 0.01, 0.01, 1));
    const auto s = solveCase(c);
    CHECK(s.report.converged());
    // Flux continuity at the interface: (k1/L1)(100 - Ti) = (k2/L2)(Ti - 20)
    const double g1 = 10.0 / 0.4, g2 = 2.0 / 0.6;
    const double Ti = (g1 * 100 + g2 * 20) / (g1 + g2);
    for (std::size_t n = 0; n < s.mesh.nodeCount(); ++n) {
        const double x = s.mesh.nodePosition(n)[0];
        const double expected = x <= 0.4 ? 100 + (Ti - 100) * x / 0.4 : Ti + (20 - Ti) * (x - 0.4) / 0.6;
        CHECK_NEAR(s.T[n], expected, 1e-8);
    }
}

TEST(manufactured_solution_converges_at_second_order_on_the_cube) {
    double rmsPrev = 0.0, maxPrev = 0.0;
    int level = 0;
    for (int N : {8, 16, 32}) {
        const auto c = fem3d::manufacturedCube(N);
        const auto s = solveCase(c, 1e-12);
        CHECK(s.report.converged());
        const auto err = fem3d::errorNorms(s.mesh, s.T, c.exact);
        if (level > 0) {
            const double orderRms = std::log2(rmsPrev / err.rms_C);
            const double orderMax = std::log2(maxPrev / err.maxNodal_C);
            CHECK(orderRms > 1.8 && orderRms < 2.2);
            CHECK(orderMax > 1.8 && orderMax < 2.2);
        }
        rmsPrev = err.rms_C;
        maxPrev = err.maxNodal_C;
        ++level;
    }
}

TEST(manufactured_solution_converges_on_the_heat_sink_domain) {
    const auto geometry = fem3d::heatSink(1 * mm, false).geometry;
    double rmsPrev = 0.0;
    int level = 0;
    for (double h : {2 * mm, 1 * mm}) {
        const auto c = fem3d::manufacturedOn("heatsink-manufactured", geometry, {h, h, h}, 200.0);
        const auto s = solveCase(c, 1e-12);
        CHECK(s.report.converged());
        const auto err = fem3d::errorNorms(s.mesh, s.T, c.exact);
        if (level > 0) {
            // The grading rounds cell counts up, so the refinement ratio is not exactly 2.
            const double order = std::log2(rmsPrev / err.rms_C);
            CHECK(order > 1.6 && order < 2.4);
        }
        rmsPrev = err.rms_C;
        ++level;
    }
}

TEST(heat_balance_closes) {
    {
        const auto c = fem3d::heatSink(2 * mm, false);
        const auto s = solveCase(c, 1e-12);
        CHECK(s.report.converged());
        const auto hb = fem3d::heatBalance(s.mesh, c.problem, s.full, s.T);
        CHECK_NEAR(hb.fluxIn_W, 50.0, 1e-12);
        CHECK(hb.generated_W == 0.0);
        CHECK(hb.reactionIn_W == 0.0);
        CHECK_NEAR(hb.convectedOut_W, 50.0, 1e-6);
        CHECK(std::abs(hb.imbalance_W) < 1e-6);
    }
    {
        const auto c = fem3d::rodBar(20);
        const auto s = solveCase(c, 1e-13);
        const auto hb = fem3d::heatBalance(s.mesh, c.problem, s.full, s.T);
        CHECK_NEAR(hb.generated_W, 1.0, 1e-12);   // q V = 1e4 * 1e-4
        CHECK_NEAR(hb.reactionIn_W, -1.0, 1e-8);  // all of it leaves through the two ends
        CHECK(std::abs(hb.imbalance_W) < 1e-8);
        // Split between the ends: -k A T'(0) = -0.42 W enters at x = 0, k A T'(L) = -0.58 W at x = L.
        const auto r = fem3d::reactions(s.full, s.T);
        double left = 0.0, right = 0.0;
        for (std::size_t i = 0; i < r.size(); ++i) {
            (s.mesh.nodePosition(s.full.dirichletNodes[i])[0] < 0.5 ? left : right) += r[i];
        }
        CHECK_NEAR(left, -0.42, 1e-8);
        CHECK_NEAR(right, -0.58, 1e-8);
    }
}

TEST(single_fin_matches_1d_fin_theory) {
    using namespace fem3d::heatsink;
    const double root = 80.0;
    const auto c = fem3d::singleFin(1 * mm, root);
    const auto s = solveCase(c, 1e-12);
    CHECK(s.report.converged());
    const double P = 2 * (kFinThickness + kBaseY), A = kFinThickness * kBaseY;
    const double m = std::sqrt(kConvection_W_m2K * P / (kAluminium_W_mK * A));
    const double hOverMk = kConvection_W_m2K / (m * kAluminium_W_mK);
    double tipSum = 0.0;
    int tipCount = 0;
    for (std::size_t n = 0; n < s.mesh.nodeCount(); ++n) {
        if (std::abs(s.mesh.nodePosition(n)[2] - kFinHeight) < 1e-12) {
            tipSum += s.T[n];
            ++tipCount;
        }
    }
    CHECK(tipCount > 0);
    const double tipTheory = kAmbient_C + (root - kAmbient_C) * fem3d::finExcessRatio(m, kFinHeight, hOverMk, kFinHeight);
    CHECK_NEAR(tipSum / tipCount, tipTheory, 0.5);  // the 1D model ignores the small variation across the fin
    const auto hb = fem3d::heatBalance(s.mesh, c.problem, s.full, s.T);
    const double heatTheory = fem3d::finRootHeat_W(std::sqrt(kConvection_W_m2K * P * kAluminium_W_mK * A),
                                                   root - kAmbient_C, m, kFinHeight, hOverMk);
    CHECK_NEAR(hb.reactionIn_W, heatTheory, 0.02 * heatTheory);
    CHECK_NEAR(hb.convectedOut_W, hb.reactionIn_W, 1e-6);
}

TEST(heat_sink_field_is_physically_sensible) {
    const auto c = fem3d::heatSink(2 * mm, false);
    const auto s = solveCase(c, 1e-10);
    CHECK(s.report.converged());
    double tMin = 1e300, tMax = -1e300;
    std::size_t hottest = 0;
    for (std::size_t n = 0; n < s.mesh.nodeCount(); ++n) {
        tMin = std::min(tMin, s.T[n]);
        if (s.T[n] > tMax) {
            tMax = s.T[n];
            hottest = n;
        }
    }
    CHECK(tMin >= fem3d::heatsink::kAmbient_C - 1e-9);  // nothing below ambient
    CHECK(tMax > 60.0 && tMax < 140.0);                 // 50 W over ~0.016 m^2 at h = 50: ~60 K plus spreading
    const auto p = s.mesh.nodePosition(hottest);
    CHECK(p[2] < 1e-12);  // the hottest node is on the board side, under the chip
    CHECK(p[0] > 19 * mm && p[0] < 41 * mm && p[1] > 9 * mm && p[1] < 31 * mm);
    double tipMax = -1e300, baseTopMax = -1e300;
    for (std::size_t n = 0; n < s.mesh.nodeCount(); ++n) {
        const auto q = s.mesh.nodePosition(n);
        if (std::abs(q[2] - 30 * mm) < 1e-12) tipMax = std::max(tipMax, s.T[n]);
        if (std::abs(q[2] - 5 * mm) < 1e-12) baseTopMax = std::max(baseTopMax, s.T[n]);
    }
    CHECK(tipMax < baseTopMax);
    CHECK(tipMax > fem3d::heatsink::kAmbient_C);
}

TEST(assembly_and_solve_are_deterministic) {
    const auto c = fem3d::heatSink(2 * mm, true);
    const auto a = solveCase(c, 1e-10), b = solveCase(c, 1e-10);
    CHECK(a.full.K.val == b.full.K.val);
    CHECK(a.full.b == b.full.b);
    CHECK(a.T == b.T);
    CHECK(a.report.iterations == b.report.iterations);
}

TEST(assembly_can_be_cancelled) {
    const auto c = fem3d::rodCube(4);
    auto m = fem3d::buildMesh(c);
    CancelToken token;
    token.requestCancel();
    CHECK_THROWS_CODE(fem3d::assemble(m, c.problem, &token), ErrorCode::Cancelled);
}

TEST(dirichlet_elimination_keeps_the_free_system_consistent) {
    const auto c = fem3d::rodCube(4);
    auto m = fem3d::buildMesh(c);
    const auto full = fem3d::assemble(m, c.problem);
    const auto red = fem3d::eliminateDirichlet(full);
    CHECK(red.A.rows() == 125 - 50);
    CHECK(red.A.symmetryDefect() < 1e-12);
    CHECK(red.freeNodes.size() == red.A.rows());
    for (std::size_t r = 0; r < red.A.rows(); ++r) {
        for (std::size_t p = red.A.rowPtr[r] + 1; p < red.A.rowPtr[r + 1]; ++p) CHECK(red.A.col[p - 1] < red.A.col[p]);
    }
    std::vector<double> x;
    solvers::SolveOptions opt;
    opt.relativeTolerance = 1e-13;
    solvers::conjugateGradient(red.A, red.b, x, opt);
    const auto T = fem3d::expandSolution(red, x);
    for (std::size_t i = 0; i < full.dirichletNodes.size(); ++i) {
        CHECK(T[full.dirichletNodes[i]] == full.dirichletValues[i]);
    }
    const auto r = sparse::residual(full.K, T, full.b);
    for (std::size_t n : red.freeNodes) CHECK(std::abs(r[n]) < 1e-8);

    fem3d::FullSystem allFixed = full;
    allFixed.dirichletNodes.clear();
    allFixed.dirichletValues.clear();
    for (std::size_t i = 0; i < full.K.rows(); ++i) {
        allFixed.dirichletNodes.push_back(i);
        allFixed.dirichletValues.push_back(1.0);
    }
    CHECK_THROWS_CODE(fem3d::eliminateDirichlet(allFixed), ErrorCode::InvalidParameter);
}
