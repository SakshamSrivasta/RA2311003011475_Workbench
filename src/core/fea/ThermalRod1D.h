// Steady-state 1D heat conduction in a uniform rod, solved with two-node
// linear finite elements.
//
// Governing equation (insulated lateral surface, constant properties):
//     -d/dx( k A dT/dx ) = q A,      0 < x < L
//     T(0) = T_left,  T(L) = T_right   (prescribed temperatures)
//
// Units (SI, temperatures in degrees Celsius):
//     x, L [m]   A [m^2]   k [W/(m K)]   q [W/m^3]   T [C]
//
// Because the problem is linear in T, a temperature difference in K equals one
// in C, so using Celsius throughout is consistent.
#pragma once

#include <vector>

namespace wb::fea {

struct RodProblem {
    double length_m = 1.0;
    double area_m2 = 1e-4;
    double conductivity_W_mK = 10.0;
    double heatGeneration_W_m3 = 10000.0;
    double leftTemperature_C = 100.0;
    double rightTemperature_C = 20.0;
    int elements = 10;
};

// Largest mesh accepted by validate(). Keeps memory to roughly 40 bytes per
// node (~400 MB worst case) so a typo cannot exhaust RAM.
constexpr int kMaxElements = 10'000'000;

// Throws WorkbenchError(InvalidParameter) with an actionable message if any
// input is non-finite, non-positive where it must be positive, or if the
// element count is outside [1, kMaxElements].
void validate(const RodProblem& problem);

struct RodSolution {
    std::vector<double> x_m;            // node positions, size N+1
    std::vector<double> temperature_C;  // nodal temperatures, size N+1

    // Temperature at any x in [0, L] using the linear shape functions of the
    // element that contains x.
    double interpolate(double x) const;
};

// Assembles the global system element by element, imposes both prescribed
// temperatures, and solves it. Throws on invalid input or numerical failure.
RodSolution solve(const RodProblem& problem);

// Analytical solution for the same problem:
//     T(x) = T_left + (T_right - T_left) x / L + q/(2k) x (L - x)
// With the assignment values this is 100 - 80x/L + [q/(2k)] x (L - x).
double exactTemperature(const RodProblem& problem, double x);

struct ErrorAtPoint {
    double maxAbsError_C = 0.0;
    double atX_m = 0.0;
};

// Maximum |T_FE - T_exact| over the element midpoints, where T_FE is the
// linear interpolation (T_i + T_{i+1}) / 2.
ErrorAtPoint maxMidpointError(const RodProblem& problem, const RodSolution& solution);

// Maximum |T_FE - T_exact| over the nodes.
ErrorAtPoint maxNodalError(const RodProblem& problem, const RodSolution& solution);

struct ConvergenceRow {
    int elements = 0;
    double elementSize_m = 0.0;
    double maxMidpointError_C = 0.0;
    double maxNodalError_C = 0.0;
    // log(e_prev / e) / log(h_prev / h); 0 for the first row.
    double observedOrder = 0.0;
};

// Solves `base` once for each element count and reports the errors.
std::vector<ConvergenceRow> convergenceStudy(RodProblem base, const std::vector<int>& elementCounts);

}  // namespace wb::fea
