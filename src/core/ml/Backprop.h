// One-hidden-unit regression network with hand-derived gradients.
//
//   z     = w x + b
//   h     = tanh(z)
//   y_hat = v h + c
//   J     = 0.5 (y_hat - y)^2
//
// Chain rule, with e = y_hat - y = dJ/dy_hat:
//   dJ/dc = e
//   dJ/dv = e h
//   dJ/db = e v (1 - h^2)       since dh/dz = 1 - tanh(z)^2
//   dJ/dw = e v (1 - h^2) x
//
// No automatic differentiation is used; numericalGradients() checks these
// formulas with central finite differences.
#pragma once

#include <vector>

namespace wb::ml {

struct Params {
    double w = 0.5;
    double b = 0.0;
    double v = 1.0;
    double c = 0.0;
};

// Same four fields, holding dJ/dw, dJ/db, dJ/dv, dJ/dc.
using Gradients = Params;

struct Sample {
    double x = 1.0;
    double y = 2.0;
};

struct ForwardPass {
    double z = 0.0;
    double h = 0.0;
    double yHat = 0.0;
    double loss = 0.0;
};

ForwardPass forward(const Params& p, const Sample& s);

double loss(const Params& p, const Sample& s);

Gradients analyticGradients(const Params& p, const Sample& s);

// [J(theta + eps) - J(theta - eps)] / (2 eps) for each parameter in turn,
// holding the other three fixed.
Gradients numericalGradients(const Params& p, const Sample& s, double eps = 1e-6);

// Simultaneous update: every parameter uses gradients computed at `p`.
Params gradientStep(const Params& p, const Gradients& g, double learningRate);

struct GradientCheckRow {
    const char* name;
    double analytic;
    double numerical;
    double absError;
    bool passed;
};

std::vector<GradientCheckRow> gradientCheck(const Params& p, const Sample& s, double eps = 1e-6,
                                            double tolerance = 1e-5);

// Loss before any update, then after each of `steps` updates (steps + 1 values).
std::vector<double> trainLossHistory(Params p, const Sample& s, double learningRate, int steps);

}  // namespace wb::ml
