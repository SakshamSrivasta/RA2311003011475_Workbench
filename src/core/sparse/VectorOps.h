// Dense vector helpers for the iterative solvers. Plain sequential loops: the
// summation order is fixed, so every solver run is bitwise reproducible.
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace wb::sparse {

inline double dot(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
    return s;
}

inline double norm2(const std::vector<double>& a) { return std::sqrt(dot(a, a)); }

inline double normInf(const std::vector<double>& a) {
    double m = 0.0;
    for (double v : a) m = std::max(m, std::abs(v));
    return m;
}

// y += alpha x
inline void axpy(double alpha, const std::vector<double>& x, std::vector<double>& y) {
    for (std::size_t i = 0; i < y.size(); ++i) y[i] += alpha * x[i];
}

}  // namespace wb::sparse
