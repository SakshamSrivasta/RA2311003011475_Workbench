// Compressed sparse row matrix. Both triangles of a symmetric matrix are
// stored, so y = A x is one pass over the rows and a Gauss-Seidel sweep needs
// no transpose. Column indices are sorted inside each row. Entries are added
// into a fixed pattern, which is how the finite-element assembly uses it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wb::sparse {

struct CsrMatrix {
    std::vector<std::size_t> rowPtr;  // size rows + 1
    std::vector<std::uint32_t> col;   // size nnz
    std::vector<double> val;          // size nnz

    std::size_t rows() const { return rowPtr.empty() ? 0 : rowPtr.size() - 1; }
    std::size_t nnz() const { return col.size(); }
    std::size_t bytes() const;

    // y = A x. x and y must not alias.
    void multiply(const double* x, double* y) const;
    std::vector<double> multiply(const std::vector<double>& x) const;

    // Adds v to the (row, column) entry, which must be in the pattern.
    void add(std::size_t row, std::uint32_t column, double v);
    // The (row, column) entry, 0 if it is not in the pattern.
    double at(std::size_t row, std::uint32_t column) const;
    std::vector<double> diagonal() const;

    // Diagnostics used by tests and the solver study.
    double symmetryDefect() const;      // max |a_ij - a_ji|
    std::size_t halfBandwidth() const;  // max |i - j| over stored entries
};

// b - A x
std::vector<double> residual(const CsrMatrix& A, const std::vector<double>& x, const std::vector<double>& b);

}  // namespace wb::sparse
