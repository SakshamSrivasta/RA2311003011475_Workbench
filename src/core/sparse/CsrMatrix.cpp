#include "core/sparse/CsrMatrix.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace wb::sparse {

std::size_t CsrMatrix::bytes() const {
    return rowPtr.size() * sizeof(std::size_t) + col.size() * sizeof(std::uint32_t) + val.size() * sizeof(double);
}

void CsrMatrix::multiply(const double* x, double* y) const {
    const std::size_t n = rows();
    for (std::size_t r = 0; r < n; ++r) {
        double s = 0.0;
        for (std::size_t p = rowPtr[r]; p < rowPtr[r + 1]; ++p) s += val[p] * x[col[p]];
        y[r] = s;
    }
}

std::vector<double> CsrMatrix::multiply(const std::vector<double>& x) const {
    std::vector<double> y(rows());
    multiply(x.data(), y.data());
    return y;
}

void CsrMatrix::add(std::size_t row, std::uint32_t column, double v) {
    for (std::size_t p = rowPtr[row]; p < rowPtr[row + 1]; ++p) {
        if (col[p] == column) {
            val[p] += v;
            return;
        }
    }
    // A pattern miss is a programming error in the assembly, not bad user input.
    throw std::logic_error("CsrMatrix::add: entry (" + std::to_string(row) + ", " + std::to_string(column) +
                           ") is not in the sparsity pattern");
}

double CsrMatrix::at(std::size_t row, std::uint32_t column) const {
    for (std::size_t p = rowPtr[row]; p < rowPtr[row + 1]; ++p) {
        if (col[p] == column) return val[p];
    }
    return 0.0;
}

std::vector<double> CsrMatrix::diagonal() const {
    std::vector<double> d(rows());
    for (std::size_t r = 0; r < d.size(); ++r) d[r] = at(r, static_cast<std::uint32_t>(r));
    return d;
}

double CsrMatrix::symmetryDefect() const {
    double worst = 0.0;
    for (std::size_t r = 0; r < rows(); ++r) {
        for (std::size_t p = rowPtr[r]; p < rowPtr[r + 1]; ++p) {
            worst = std::max(worst, std::abs(val[p] - at(col[p], static_cast<std::uint32_t>(r))));
        }
    }
    return worst;
}

std::size_t CsrMatrix::halfBandwidth() const {
    std::size_t band = 0;
    for (std::size_t r = 0; r < rows(); ++r) {
        for (std::size_t p = rowPtr[r]; p < rowPtr[r + 1]; ++p) {
            const std::size_t c = col[p];
            band = std::max(band, c > r ? c - r : r - c);
        }
    }
    return band;
}

std::vector<double> residual(const CsrMatrix& A, const std::vector<double>& x, const std::vector<double>& b) {
    std::vector<double> r(A.rows());
    A.multiply(x.data(), r.data());
    for (std::size_t i = 0; i < r.size(); ++i) r[i] = b[i] - r[i];
    return r;
}

}  // namespace wb::sparse
