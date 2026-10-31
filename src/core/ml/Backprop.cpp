#include "core/ml/Backprop.h"

#include "core/common/Errors.h"

#include <cmath>

namespace wb::ml {

ForwardPass forward(const Params& p, const Sample& s) {
    ForwardPass f;
    f.z = p.w * s.x + p.b;
    f.h = std::tanh(f.z);
    f.yHat = p.v * f.h + p.c;
    const double e = f.yHat - s.y;
    f.loss = 0.5 * e * e;
    return f;
}

double loss(const Params& p, const Sample& s) { return forward(p, s).loss; }

Gradients analyticGradients(const Params& p, const Sample& s) {
    const ForwardPass f = forward(p, s);
    const double dJ_dyHat = f.yHat - s.y;
    const double dJ_dz = dJ_dyHat * p.v * (1.0 - f.h * f.h);

    Gradients g;
    g.w = dJ_dz * s.x;
    g.b = dJ_dz;
    g.v = dJ_dyHat * f.h;
    g.c = dJ_dyHat;
    return g;
}

Gradients numericalGradients(const Params& p, const Sample& s, double eps) {
    requirePositiveFinite(eps, "Finite-difference step epsilon", "(dimensionless)");
    auto central = [&](double Params::*field) {
        Params plus = p, minus = p;
        plus.*field += eps;
        minus.*field -= eps;
        return (loss(plus, s) - loss(minus, s)) / (2.0 * eps);
    };
    Gradients g;
    g.w = central(&Params::w);
    g.b = central(&Params::b);
    g.v = central(&Params::v);
    g.c = central(&Params::c);
    return g;
}

Params gradientStep(const Params& p, const Gradients& g, double lr) {
    return {p.w - lr * g.w, p.b - lr * g.b, p.v - lr * g.v, p.c - lr * g.c};
}

std::vector<GradientCheckRow> gradientCheck(const Params& p, const Sample& s, double eps, double tolerance) {
    const Gradients a = analyticGradients(p, s);
    const Gradients n = numericalGradients(p, s, eps);
    auto row = [&](const char* name, double av, double nv) {
        const double err = std::abs(av - nv);
        return GradientCheckRow{name, av, nv, err, err <= tolerance};
    };
    return {row("w", a.w, n.w), row("b", a.b, n.b), row("v", a.v, n.v), row("c", a.c, n.c)};
}

std::vector<double> trainLossHistory(Params p, const Sample& s, double lr, int steps) {
    requirePositiveFinite(lr, "Learning rate", "(dimensionless)");
    if (steps < 0) {
        throw WorkbenchError(ErrorCode::InvalidParameter, "Training step count must be zero or more.");
    }
    std::vector<double> history;
    history.reserve(static_cast<std::size_t>(steps) + 1);
    history.push_back(loss(p, s));
    for (int i = 0; i < steps; ++i) {
        p = gradientStep(p, analyticGradients(p, s), lr);
        history.push_back(loss(p, s));
    }
    return history;
}

}  // namespace wb::ml
