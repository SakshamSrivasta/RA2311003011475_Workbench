#include "core/fea/ThermalRod1D.h"

#include "core/common/Errors.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <sstream>

namespace wb::fea {

namespace {

// Global stiffness matrix of a 1D linear-element mesh is tridiagonal, so we
// store only its three diagonals:
//     lower[i] = K(i, i-1),  diag[i] = K(i, i),  upper[i] = K(i, i+1)
struct TridiagonalSystem {
    std::vector<double> lower, diag, upper, rhs;

    explicit TridiagonalSystem(std::size_t n) : lower(n, 0.0), diag(n, 0.0), upper(n, 0.0), rhs(n, 0.0) {}
    std::size_t size() const { return diag.size(); }
};

// Adds every element's 2x2 stiffness matrix and 2x1 load vector into the
// global system.
//     k_e = (kA/h) [ 1 -1; -1 1 ]      f_e = (qAh/2) [ 1; 1 ]
// The load vector is the exact integral of q A N_i over the element for a
// uniform q.
void assemble(const RodProblem& p, TridiagonalSystem& sys) {
    const double h = p.length_m / p.elements;
    const double ke = p.conductivity_W_mK * p.area_m2 / h;
    const double fe = p.heatGeneration_W_m3 * p.area_m2 * h / 2.0;

    for (int e = 0; e < p.elements; ++e) {
        const std::size_t i = static_cast<std::size_t>(e);  // left node
        const std::size_t j = i + 1;                        // right node
        sys.diag[i] += ke;
        sys.upper[i] -= ke;
        sys.lower[j] -= ke;
        sys.diag[j] += ke;
        sys.rhs[i] += fe;
        sys.rhs[j] += fe;
    }
}

// Imposes T(node) = value. The known value is moved to the right-hand side of
// the neighbouring row (keeping the matrix symmetric) and the node's own row
// becomes the identity equation 1 * T = value.
void imposeTemperature(TridiagonalSystem& sys, std::size_t node, double value) {
    const std::size_t n = sys.size();
    if (node > 0) {
        sys.rhs[node - 1] -= sys.upper[node - 1] * value;
        sys.upper[node - 1] = 0.0;
    }
    if (node + 1 < n) {
        sys.rhs[node + 1] -= sys.lower[node + 1] * value;
        sys.lower[node + 1] = 0.0;
    }
    sys.lower[node] = 0.0;
    sys.upper[node] = 0.0;
    sys.diag[node] = 1.0;
    sys.rhs[node] = value;
}

// Thomas algorithm (Gaussian elimination without pivoting for tridiagonal
// systems). Stable here because the constrained stiffness matrix is symmetric
// positive definite. O(n) time, deterministic operation order.
std::vector<double> solveTridiagonal(TridiagonalSystem sys) {
    const std::size_t n = sys.size();
    for (std::size_t i = 1; i < n; ++i) {
        const double pivot = sys.diag[i - 1];
        if (pivot == 0.0 || !std::isfinite(pivot)) {
            throw WorkbenchError(ErrorCode::NumericalFailure,
                                 "Zero or non-finite pivot while solving the rod system. "
                                 "Check that conductivity and area are positive.");
        }
        const double m = sys.lower[i] / pivot;
        sys.diag[i] -= m * sys.upper[i - 1];
        sys.rhs[i] -= m * sys.rhs[i - 1];
    }

    std::vector<double> x(n);
    x[n - 1] = sys.rhs[n - 1] / sys.diag[n - 1];
    for (std::size_t i = n - 1; i-- > 0;) {
        x[i] = (sys.rhs[i] - sys.upper[i] * x[i + 1]) / sys.diag[i];
    }
    return x;
}

}  // namespace

void validate(const RodProblem& p) {
    requirePositiveFinite(p.length_m, "Rod length L", "m");
    requirePositiveFinite(p.area_m2, "Cross-section area A", "m^2");
    requirePositiveFinite(p.conductivity_W_mK, "Thermal conductivity k", "W/(m K)");
    requireFinite(p.heatGeneration_W_m3, "Heat generation q", "W/m^3");
    requireFinite(p.leftTemperature_C, "Left temperature T(0)", "C");
    requireFinite(p.rightTemperature_C, "Right temperature T(L)", "C");
    if (p.elements < 1 || p.elements > kMaxElements) {
        std::ostringstream msg;
        msg << "Element count N must be between 1 and " << kMaxElements << " (got " << p.elements
            << "). Enter a whole number in that range.";
        throw WorkbenchError(ErrorCode::InvalidParameter, msg.str());
    }
}

RodSolution solve(const RodProblem& p) {
    validate(p);

    const std::size_t nodes = static_cast<std::size_t>(p.elements) + 1;
    TridiagonalSystem sys(nodes);
    assemble(p, sys);
    imposeTemperature(sys, 0, p.leftTemperature_C);
    imposeTemperature(sys, nodes - 1, p.rightTemperature_C);

    RodSolution out;
    out.temperature_C = solveTridiagonal(std::move(sys));
    out.x_m.resize(nodes);
    const double h = p.length_m / p.elements;
    for (std::size_t i = 0; i < nodes; ++i) {
        out.x_m[i] = h * static_cast<double>(i);
    }
    out.x_m.back() = p.length_m;  // avoid round-off at the last node

    for (double t : out.temperature_C) {
        if (!std::isfinite(t)) {
            throw WorkbenchError(ErrorCode::NumericalFailure,
                                 "Solver produced a non-finite temperature. Reduce extreme input values.");
        }
    }
    return out;
}

double RodSolution::interpolate(double x) const {
    if (x_m.size() < 2) {
        throw WorkbenchError(ErrorCode::InvalidParameter, "Cannot interpolate an empty solution.");
    }
    if (x <= x_m.front()) return temperature_C.front();
    if (x >= x_m.back()) return temperature_C.back();

    // First node strictly greater than x; the element is [i-1, i].
    const auto it = std::upper_bound(x_m.begin(), x_m.end(), x);
    const std::size_t i = static_cast<std::size_t>(it - x_m.begin());
    const double x0 = x_m[i - 1], x1 = x_m[i];
    const double xi = (x - x0) / (x1 - x0);  // local coordinate in [0, 1]
    return (1.0 - xi) * temperature_C[i - 1] + xi * temperature_C[i];
}

double exactTemperature(const RodProblem& p, double x) {
    const double L = p.length_m;
    return p.leftTemperature_C + (p.rightTemperature_C - p.leftTemperature_C) * x / L +
           p.heatGeneration_W_m3 / (2.0 * p.conductivity_W_mK) * x * (L - x);
}

ErrorAtPoint maxMidpointError(const RodProblem& p, const RodSolution& s) {
    ErrorAtPoint worst;
    for (std::size_t i = 0; i + 1 < s.x_m.size(); ++i) {
        const double xm = 0.5 * (s.x_m[i] + s.x_m[i + 1]);
        const double tFe = 0.5 * (s.temperature_C[i] + s.temperature_C[i + 1]);
        const double err = std::abs(tFe - exactTemperature(p, xm));
        if (err > worst.maxAbsError_C) worst = {err, xm};
    }
    return worst;
}

ErrorAtPoint maxNodalError(const RodProblem& p, const RodSolution& s) {
    ErrorAtPoint worst;
    for (std::size_t i = 0; i < s.x_m.size(); ++i) {
        const double err = std::abs(s.temperature_C[i] - exactTemperature(p, s.x_m[i]));
        if (err > worst.maxAbsError_C) worst = {err, s.x_m[i]};
    }
    return worst;
}

std::vector<ConvergenceRow> convergenceStudy(RodProblem base, const std::vector<int>& elementCounts) {
    std::vector<ConvergenceRow> rows;
    rows.reserve(elementCounts.size());
    for (int n : elementCounts) {
        base.elements = n;
        const RodSolution s = solve(base);

        ConvergenceRow row;
        row.elements = n;
        row.elementSize_m = base.length_m / n;
        row.maxMidpointError_C = maxMidpointError(base, s).maxAbsError_C;
        row.maxNodalError_C = maxNodalError(base, s).maxAbsError_C;
        if (!rows.empty() && row.maxMidpointError_C > 0.0 && rows.back().maxMidpointError_C > 0.0) {
            const ConvergenceRow& prev = rows.back();
            row.observedOrder = std::log(prev.maxMidpointError_C / row.maxMidpointError_C) /
                                std::log(prev.elementSize_m / row.elementSize_m);
        }
        rows.push_back(row);
    }
    return rows;
}

}  // namespace wb::fea
