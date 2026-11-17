// Node reordering for the banded (skyline) direct solver.
//
// The profile of a symmetric matrix, sum over rows of (i - first column in
// row i), is exactly how many entries the skyline factor stores below the
// diagonal: no fill appears outside it, and in practice almost all of it
// fills in. Reverse Cuthill-McKee renumbers the graph breadth-first from a
// far-away node, which keeps every node's neighbours close in number and so
// keeps the profile small.
#pragma once

#include "core/sparse/CsrMatrix.h"

#include <cstddef>
#include <vector>

namespace wb::sparse {

// perm[newIndex] = oldIndex.
using Permutation = std::vector<std::size_t>;

Permutation identityPermutation(std::size_t n);

// Reverse Cuthill-McKee on the graph of A (A must have a symmetric pattern).
// Starts from a pseudo-peripheral node found by repeated BFS; neighbours are
// visited in increasing degree, ties by index, so the result is deterministic.
// Handles disconnected graphs component by component.
Permutation reverseCuthillMcKee(const CsrMatrix& A);

// B = P A P^T, i.e. B(i, j) = A(perm[i], perm[j]), with sorted columns.
CsrMatrix permuteSymmetric(const CsrMatrix& A, const Permutation& perm);

std::vector<double> permuteVector(const std::vector<double>& v, const Permutation& perm);    // out[i] = v[perm[i]]
std::vector<double> unpermuteVector(const std::vector<double>& v, const Permutation& perm);  // out[perm[i]] = v[i]

// Sum over rows of (i - first stored column <= i). Entries above the diagonal are ignored.
std::size_t profile(const CsrMatrix& A);

}  // namespace wb::sparse
