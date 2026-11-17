#include "core/sparse/Ordering.h"

#include <algorithm>
#include <numeric>

namespace wb::sparse {

Permutation identityPermutation(std::size_t n) {
    Permutation p(n);
    std::iota(p.begin(), p.end(), std::size_t{0});
    return p;
}

namespace {

std::size_t degree(const CsrMatrix& A, std::size_t i) { return A.rowPtr[i + 1] - A.rowPtr[i]; }

// Breadth-first search from `start` over unvisited nodes (mark[i] == 0).
// Returns the level structure; the nodes are not marked afterwards.
std::vector<std::vector<std::size_t>> levels(const CsrMatrix& A, std::size_t start, std::vector<int>& mark, int stamp) {
    std::vector<std::vector<std::size_t>> out{{start}};
    mark[start] = stamp;
    while (true) {
        std::vector<std::size_t> next;
        for (std::size_t u : out.back()) {
            for (std::size_t p = A.rowPtr[u]; p < A.rowPtr[u + 1]; ++p) {
                const std::size_t v = A.col[p];
                if (mark[v] == 0) {
                    mark[v] = stamp;
                    next.push_back(v);
                }
            }
        }
        if (next.empty()) break;
        out.push_back(std::move(next));
    }
    for (const auto& level : out)
        for (std::size_t u : level) mark[u] = 0;
    return out;
}

}  // namespace

Permutation reverseCuthillMcKee(const CsrMatrix& A) {
    const std::size_t n = A.rows();
    Permutation order;
    order.reserve(n);
    std::vector<int> done(n, 0), mark(n, 0);

    for (std::size_t seed = 0; seed < n; ++seed) {
        if (done[seed]) continue;
        // Pseudo-peripheral node (George-Liu): jump to a lowest-degree node of
        // the deepest level until the depth stops growing.
        for (std::size_t i = 0; i < n; ++i) mark[i] = done[i] ? -1 : 0;
        std::size_t start = seed;
        auto lv = levels(A, start, mark, 1);
        while (true) {
            const auto& last = lv.back();
            const std::size_t candidate = *std::min_element(last.begin(), last.end(), [&](std::size_t a, std::size_t b) {
                return degree(A, a) != degree(A, b) ? degree(A, a) < degree(A, b) : a < b;
            });
            auto lv2 = levels(A, candidate, mark, 1);
            if (lv2.size() <= lv.size()) break;
            start = candidate;
            lv = std::move(lv2);
        }

        // Cuthill-McKee from `start`.
        const std::size_t first = order.size();
        order.push_back(start);
        done[start] = 1;
        std::vector<std::size_t> nbrs;
        for (std::size_t head = first; head < order.size(); ++head) {
            const std::size_t u = order[head];
            nbrs.clear();
            for (std::size_t p = A.rowPtr[u]; p < A.rowPtr[u + 1]; ++p) {
                const std::size_t v = A.col[p];
                if (!done[v]) {
                    done[v] = 1;
                    nbrs.push_back(v);
                }
            }
            std::sort(nbrs.begin(), nbrs.end(), [&](std::size_t a, std::size_t b) {
                return degree(A, a) != degree(A, b) ? degree(A, a) < degree(A, b) : a < b;
            });
            order.insert(order.end(), nbrs.begin(), nbrs.end());
        }
        std::reverse(order.begin() + static_cast<std::ptrdiff_t>(first), order.end());
    }
    return order;  // each component was reversed in place above
}

CsrMatrix permuteSymmetric(const CsrMatrix& A, const Permutation& perm) {
    const std::size_t n = A.rows();
    std::vector<std::size_t> inverse(n);
    for (std::size_t i = 0; i < n; ++i) inverse[perm[i]] = i;
    CsrMatrix B;
    B.rowPtr.assign(n + 1, 0);
    for (std::size_t i = 0; i < n; ++i) B.rowPtr[i + 1] = B.rowPtr[i] + (A.rowPtr[perm[i] + 1] - A.rowPtr[perm[i]]);
    B.col.resize(A.nnz());
    B.val.resize(A.nnz());
    std::vector<std::pair<std::uint32_t, double>> row;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t old = perm[i];
        row.clear();
        for (std::size_t p = A.rowPtr[old]; p < A.rowPtr[old + 1]; ++p) {
            row.emplace_back(static_cast<std::uint32_t>(inverse[A.col[p]]), A.val[p]);
        }
        std::sort(row.begin(), row.end());
        std::size_t q = B.rowPtr[i];
        for (const auto& [c, v] : row) {
            B.col[q] = c;
            B.val[q] = v;
            ++q;
        }
    }
    return B;
}

std::vector<double> permuteVector(const std::vector<double>& v, const Permutation& perm) {
    std::vector<double> out(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) out[i] = v[perm[i]];
    return out;
}

std::vector<double> unpermuteVector(const std::vector<double>& v, const Permutation& perm) {
    std::vector<double> out(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) out[perm[i]] = v[i];
    return out;
}

std::size_t profile(const CsrMatrix& A) {
    std::size_t total = 0;
    for (std::size_t i = 0; i < A.rows(); ++i) {
        if (A.rowPtr[i] == A.rowPtr[i + 1]) continue;
        const std::size_t first = A.col[A.rowPtr[i]];  // columns are sorted
        if (first < i) total += i - first;
    }
    return total;
}

}  // namespace wb::sparse
