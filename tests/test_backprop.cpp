#include "TestHarness.h"

#include "core/common/Errors.h"
#include "core/ml/Backprop.h"

using namespace wb;
using namespace wb::ml;

// Hand-computed reference values (same numbers as the handwritten appendix).
TEST(backprop_forward_pass_matches_hand_calculation) {
    const ForwardPass f = forward(Params{}, Sample{});
    CHECK_NEAR(f.z, 0.5, 1e-15);
    CHECK_NEAR(f.h, 0.46211715726, 1e-10);
    CHECK_NEAR(f.yHat, 0.46211715726, 1e-10);
    CHECK_NEAR(f.loss, 1.18254182, 1e-8);
}

TEST(backprop_analytic_gradients_match_hand_calculation) {
    const Gradients g = analyticGradients(Params{}, Sample{});
    CHECK_NEAR(g.c, -1.53788284, 1e-8);
    CHECK_NEAR(g.v, -0.71068205, 1e-8);
    CHECK_NEAR(g.b, -1.20946448, 1e-8);
    CHECK_NEAR(g.w, -1.20946448, 1e-8);  // x = 1, so dJ/dw = dJ/db
}

TEST(backprop_gradient_check_central_differences) {
    const auto rows = gradientCheck(Params{}, Sample{}, 1e-6, 1e-5);
    CHECK(rows.size() == 4);
    for (const auto& r : rows) {
        CHECK(r.passed);
        CHECK(r.absError <= 1e-5);
    }
}

TEST(backprop_gradient_check_at_a_non_trivial_point) {
    // x != 1 so dJ/dw and dJ/db differ - catches a missing "* x".
    const Params p{-0.7, 0.3, 1.8, -0.4};
    const Sample s{2.5, -1.0};
    for (const auto& r : gradientCheck(p, s)) CHECK(r.passed);
    const Gradients g = analyticGradients(p, s);
    CHECK_NEAR(g.w, 2.5 * g.b, 1e-12);
}

TEST(backprop_one_simultaneous_update) {
    const Params p0{};
    const Params p1 = gradientStep(p0, analyticGradients(p0, Sample{}), 0.1);
    CHECK_NEAR(p1.w, 0.62094645, 1e-8);
    CHECK_NEAR(p1.b, 0.12094645, 1e-8);
    CHECK_NEAR(p1.v, 1.07106821, 1e-8);
    CHECK_NEAR(p1.c, 0.15378828, 1e-8);
    CHECK(loss(p1, Sample{}) < loss(p0, Sample{}));
}

TEST(backprop_training_reduces_loss) {
    const auto history = trainLossHistory(Params{}, Sample{}, 0.1, 100);
    CHECK(history.size() == 101);
    CHECK_NEAR(history.front(), 1.18254182, 1e-8);
    CHECK(history.back() < 1e-6);
    for (std::size_t i = 1; i < history.size(); ++i) CHECK(history[i] <= history[i - 1]);
}

TEST(backprop_rejects_bad_settings) {
    CHECK_THROWS_CODE(trainLossHistory(Params{}, Sample{}, -0.1, 10), ErrorCode::InvalidParameter);
    CHECK_THROWS_CODE(trainLossHistory(Params{}, Sample{}, 0.1, -1), ErrorCode::InvalidParameter);
    CHECK_THROWS_CODE(numericalGradients(Params{}, Sample{}, 0.0), ErrorCode::InvalidParameter);
}
