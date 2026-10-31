#include "TestHarness.h"

#include "core/common/Errors.h"
#include "core/fea/ThermalRod1D.h"

#include <limits>

using namespace wb;
using fea::RodProblem;

TEST(fea_boundary_temperatures_are_imposed_exactly) {
    const auto s = fea::solve(RodProblem{});
    CHECK(s.temperature_C.front() == 100.0);
    CHECK(s.temperature_C.back() == 20.0);
    CHECK(s.x_m.size() == 11);
    CHECK(s.x_m.back() == 1.0);
}

TEST(fea_exact_solution_matches_assignment_formula) {
    // T_exact(x) = 100 - 80x/L + q/(2k) x (L - x)
    const RodProblem p;
    for (double x : {0.0, 0.25, 0.5, 0.8, 1.0}) {
        const double expected = 100.0 - 80.0 * x + 10000.0 / 20.0 * x * (1.0 - x);
        CHECK_NEAR(fea::exactTemperature(p, x), expected, 1e-12);
    }
    CHECK_NEAR(fea::exactTemperature(p, 0.5), 185.0, 1e-12);
}

TEST(fea_nodal_values_are_exact_for_linear_elements) {
    // In 1D with consistent loads, linear FE is nodally exact; only round-off remains.
    for (int n : {1, 3, 10, 80}) {
        RodProblem p;
        p.elements = n;
        const auto s = fea::solve(p);
        CHECK(fea::maxNodalError(p, s).maxAbsError_C < 1e-9);
    }
}

TEST(fea_midpoint_error_matches_theory) {
    // Exact minus linear interpolant of a parabola at a midpoint = q h^2 / (8k).
    for (int n : {10, 20, 40, 80}) {
        RodProblem p;
        p.elements = n;
        const auto s = fea::solve(p);
        const double h = 1.0 / n;
        CHECK_NEAR(fea::maxMidpointError(p, s).maxAbsError_C, 10000.0 * h * h / (8.0 * 10.0), 1e-9);
    }
}

TEST(fea_midpoint_error_uses_interpolation_not_exact_nodes) {
    RodProblem p;
    p.elements = 10;
    const auto s = fea::solve(p);
    const double xm = 0.05;
    CHECK_NEAR(s.interpolate(xm), 0.5 * (s.temperature_C[0] + s.temperature_C[1]), 1e-12);
    CHECK_NEAR(s.interpolate(xm) - fea::exactTemperature(p, xm), -1.25, 1e-9);
}

TEST(fea_convergence_is_second_order) {
    const auto rows = fea::convergenceStudy(RodProblem{}, {10, 20, 40, 80});
    CHECK(rows.size() == 4);
    CHECK_NEAR(rows[0].maxMidpointError_C, 1.25, 1e-9);
    CHECK_NEAR(rows[3].maxMidpointError_C, 0.01953125, 1e-9);
    for (std::size_t i = 1; i < rows.size(); ++i) {
        CHECK_NEAR(rows[i].observedOrder, 2.0, 1e-6);
        CHECK(rows[i].maxMidpointError_C < rows[i - 1].maxMidpointError_C);
    }
}

TEST(fea_zero_heat_generation_gives_linear_profile) {
    RodProblem p;
    p.heatGeneration_W_m3 = 0.0;
    p.elements = 7;
    const auto s = fea::solve(p);
    for (std::size_t i = 0; i < s.x_m.size(); ++i) CHECK_NEAR(s.temperature_C[i], 100.0 - 80.0 * s.x_m[i], 1e-10);
}

TEST(fea_single_element_still_works) {
    RodProblem p;
    p.elements = 1;
    const auto s = fea::solve(p);
    CHECK(s.temperature_C.size() == 2);
    CHECK_NEAR(fea::maxMidpointError(p, s).maxAbsError_C, 125.0, 1e-9);
}

TEST(fea_rejects_invalid_parameters) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    auto withN = [](int n) { RodProblem p; p.elements = n; return p; };
    CHECK_THROWS_CODE(fea::solve(withN(0)), ErrorCode::InvalidParameter);
    CHECK_THROWS_CODE(fea::solve(withN(-5)), ErrorCode::InvalidParameter);
    CHECK_THROWS_CODE(fea::solve(withN(fea::kMaxElements + 1)), ErrorCode::InvalidParameter);

    RodProblem p;
    p.conductivity_W_mK = 0.0;
    CHECK_THROWS_CODE(fea::solve(p), ErrorCode::InvalidParameter);
    p = RodProblem{};
    p.length_m = -1.0;
    CHECK_THROWS_CODE(fea::solve(p), ErrorCode::InvalidParameter);
    p = RodProblem{};
    p.area_m2 = nan;
    CHECK_THROWS_CODE(fea::solve(p), ErrorCode::InvalidParameter);
    p = RodProblem{};
    p.heatGeneration_W_m3 = inf;
    CHECK_THROWS_CODE(fea::solve(p), ErrorCode::InvalidParameter);
    p = RodProblem{};
    p.leftTemperature_C = nan;
    CHECK_THROWS_CODE(fea::solve(p), ErrorCode::InvalidParameter);
}
