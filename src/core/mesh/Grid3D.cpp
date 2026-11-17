#include "core/mesh/Grid3D.h"

#include "core/common/Errors.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace wb::mesh {

std::size_t Grid3D::cellCount() const {
    return static_cast<std::size_t>(cells(0)) * cells(1) * cells(2);
}

std::size_t Grid3D::gridNodeCount() const {
    return static_cast<std::size_t>(nodes(0)) * nodes(1) * nodes(2);
}

double Grid3D::tolerance() const {
    return 1e-9 * std::max({extent(0), extent(1), extent(2)});
}

namespace {

const char* axisName(int axis) { return axis == 0 ? "x" : axis == 1 ? "y" : "z"; }

struct AxisPlan {
    std::vector<double> keys;  // distinct, increasing
    std::vector<int> cells;    // per interval between keys
    std::size_t lineCount() const {
        std::size_t n = 1;
        for (int c : cells) n += static_cast<std::size_t>(c);
        return n;
    }
};

AxisPlan planAxis(int axis, const GridSpec& spec) {
    const std::string spacingName = std::string("grid spacing along ") + axisName(axis);
    requirePositiveFinite(spec.spacing_m[axis], spacingName.c_str(), "m");

    std::vector<double> keys = spec.keys[axis];
    for (const Refinement& r : spec.refinements) {
        if (r.axis != axis) continue;
        requirePositiveFinite(r.spacing_m, "refinement spacing", "m");
        requireFinite(r.lo, "refinement lower bound", "m");
        requireFinite(r.hi, "refinement upper bound", "m");
        if (!(r.lo < r.hi)) {
            throw WorkbenchError(ErrorCode::InvalidParameter,
                                 std::string("refinement interval along ") + axisName(axis) +
                                     " is empty: the lower bound must be below the upper bound");
        }
        keys.push_back(r.lo);
        keys.push_back(r.hi);
    }
    for (double k : keys) requireFinite(k, "grid key coordinate", "m");
    std::sort(keys.begin(), keys.end());
    if (keys.size() < 2 || !(keys.back() > keys.front())) {
        throw WorkbenchError(ErrorCode::InvalidParameter,
                             std::string("the geometry has no extent along ") + axisName(axis) +
                                 ": at least two distinct coordinates are needed on every axis");
    }

    AxisPlan plan;
    const double tol = 1e-9 * (keys.back() - keys.front());
    for (double k : keys) {
        if (plan.keys.empty() || k - plan.keys.back() > tol) plan.keys.push_back(k);
    }
    for (std::size_t i = 0; i + 1 < plan.keys.size(); ++i) {
        const double a = plan.keys[i], b = plan.keys[i + 1];
        double h = spec.spacing_m[axis];
        for (const Refinement& r : spec.refinements) {
            if (r.axis == axis && r.lo <= a + tol && r.hi >= b - tol) h = std::min(h, r.spacing_m);
        }
        // The -1e-9 keeps an interval that is exactly k spacings at k cells
        // instead of k+1 because of round-off in the division.
        const double n = std::ceil((b - a) / h - 1e-9);
        if (n > 1e9) {
            throw WorkbenchError(ErrorCode::InvalidParameter,
                                 std::string("grid spacing along ") + axisName(axis) +
                                     " is far too fine for the geometry size; use a coarser spacing");
        }
        plan.cells.push_back(std::max(1, static_cast<int>(n)));
    }
    return plan;
}

}  // namespace

Grid3D buildGrid(const GridSpec& spec) {
    AxisPlan plans[3];
    double nodeEstimate = 1.0;
    for (int axis = 0; axis < 3; ++axis) {
        plans[axis] = planAxis(axis, spec);
        nodeEstimate *= static_cast<double>(plans[axis].lineCount());
    }
    if (nodeEstimate > static_cast<double>(kMaxGridNodes)) {
        throw WorkbenchError(ErrorCode::InvalidParameter,
                             "the grid would have about " + std::to_string(static_cast<long long>(nodeEstimate)) +
                                 " nodes, above the limit of " + std::to_string(kMaxGridNodes) +
                                 "; use a coarser spacing");
    }

    Grid3D grid;
    for (int axis = 0; axis < 3; ++axis) {
        const AxisPlan& plan = plans[axis];
        std::vector<double>& lines = grid.lines[axis];
        lines.reserve(plan.lineCount());
        lines.push_back(plan.keys.front());
        for (std::size_t i = 0; i + 1 < plan.keys.size(); ++i) {
            const double a = plan.keys[i], b = plan.keys[i + 1];
            const int n = plan.cells[i];
            for (int c = 1; c < n; ++c) lines.push_back(a + (b - a) * c / n);
            lines.push_back(b);  // the key itself, exactly
        }
    }
    return grid;
}

}  // namespace wb::mesh
