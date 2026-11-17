#include "core/fem3d/Assembler.h"

#include "core/common/Errors.h"
#include "core/fem3d/HexElement.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace wb::fem3d {

namespace {

// Every node that shares a solid cell with `node`, sorted, itself included.
// This is the row pattern of the stiffness matrix: at most 27 entries.
void neighbourNodes(const mesh::HexMesh& m, std::size_t node, std::vector<std::uint32_t>& out) {
    out.clear();
    const auto& ijk = m.nodeIjk[node];
    const int cells[3] = {m.grid.cells(0), m.grid.cells(1), m.grid.cells(2)};
    for (int dz = -1; dz <= 0; ++dz) {
        for (int dy = -1; dy <= 0; ++dy) {
            for (int dx = -1; dx <= 0; ++dx) {
                const int ci = ijk[0] + dx, cj = ijk[1] + dy, ck = ijk[2] + dz;
                if (ci < 0 || cj < 0 || ck < 0 || ci >= cells[0] || cj >= cells[1] || ck >= cells[2]) continue;
                if (m.cellMaterial[m.gridCellIndex(ci, cj, ck)] == mesh::kVoid) continue;
                for (int l = 0; l < 8; ++l) {
                    const std::size_t id =
                        m.gridNodeToNode[m.gridNodeIndex(ci + (l & 1), cj + ((l >> 1) & 1), ck + ((l >> 2) & 1))];
                    out.push_back(static_cast<std::uint32_t>(id));
                }
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

sparse::CsrMatrix emptyPattern(const mesh::HexMesh& m) {
    const std::size_t n = m.nodeCount();
    if (n >= std::numeric_limits<std::uint32_t>::max()) {
        throw WorkbenchError(ErrorCode::InvalidParameter, "too many nodes for 32-bit column indices");
    }
    sparse::CsrMatrix A;
    A.rowPtr.assign(n + 1, 0);
    std::vector<std::uint32_t> buffer;
    buffer.reserve(27);
    for (std::size_t r = 0; r < n; ++r) {
        neighbourNodes(m, r, buffer);
        A.rowPtr[r + 1] = A.rowPtr[r] + buffer.size();
    }
    A.col.resize(A.rowPtr[n]);
    A.val.assign(A.rowPtr[n], 0.0);
    for (std::size_t r = 0; r < n; ++r) {
        neighbourNodes(m, r, buffer);
        std::copy(buffer.begin(), buffer.end(), A.col.begin() + static_cast<std::ptrdiff_t>(A.rowPtr[r]));
    }
    return A;
}

}  // namespace

void applyRegions(mesh::HexMesh& mesh, const ThermalProblem3D& problem) {
    const auto matched = mesh::tagBoundaryFaces(mesh, problem.regions);
    for (std::size_t i = 0; i < matched.size(); ++i) {
        if (matched[i] == 0) {
            const auto& r = problem.regions[i];
            throw WorkbenchError(ErrorCode::InvalidParameter,
                                 "boundary region " + std::to_string(i) + " (tag " + std::to_string(r.tag) +
                                     ") matches no face: check its plane coordinate " + std::to_string(r.plane) +
                                     " m and its bounds. Left as is, that surface would be silently insulated");
        }
    }
    validate(mesh, problem);
}

FullSystem assemble(const mesh::HexMesh& m, const ThermalProblem3D& problem, const CancelToken* cancel) {
    validate(m, problem);
    const std::size_t n = m.nodeCount();
    FullSystem sys;
    sys.K = emptyPattern(m);
    sys.b.assign(n, 0.0);

    // Conduction and volumetric heat, element by element. The element matrix
    // is cheap enough (a few hundred flops) that caching it per box size is
    // not worth the bookkeeping.
    for (std::size_t e = 0; e < m.elementCount(); ++e) {
        if ((e & 4095) == 0 && isCancelled(cancel)) {
            throw WorkbenchError(ErrorCode::Cancelled, "assembly cancelled");
        }
        const mesh::Point s = m.elementSize(e);
        const Material& mat = problem.materials[static_cast<std::size_t>(m.elementMaterial[e])];
        const Matrix8 K = stiffness(s[0], s[1], s[2], mat.conductivity_W_mK);
        const auto& nodes = m.elements[e];
        for (int l = 0; l < 8; ++l) {
            for (int q = 0; q < 8; ++q) sys.K.add(nodes[l], static_cast<std::uint32_t>(nodes[q]), K[l][q]);
        }
        if (mat.heatGeneration_W_m3 != 0.0) {
            const Vector8 f = bodyLoad(s[0], s[1], s[2], mat.heatGeneration_W_m3);
            for (int l = 0; l < 8; ++l) sys.b[nodes[l]] += f[l];
        }
        if (problem.source_W_m3) {
            const Vector8 f = bodyLoad(s[0], s[1], s[2], m.elementOrigin(e), problem.source_W_m3);
            for (int l = 0; l < 8; ++l) sys.b[nodes[l]] += f[l];
        }
    }

    // Prescribed flux and convection on tagged faces.
    for (const mesh::BoundaryFace& face : m.faces) {
        for (const FluxBc& bc : problem.flux) {
            if (bc.tag != face.tag) continue;
            const double w = bc.flux_W_m2 * face.area() / 4.0;
            for (std::size_t node : face.nodes) sys.b[node] += w;
        }
        for (const ConvectionBc& bc : problem.convection) {
            if (bc.tag != face.tag || bc.h_W_m2K == 0.0) continue;
            const Matrix4 M = faceMass(face.size_u, face.size_v);
            for (int q = 0; q < 4; ++q) {
                for (int r = 0; r < 4; ++r) {
                    sys.K.add(face.nodes[q], static_cast<std::uint32_t>(face.nodes[r]), bc.h_W_m2K * M[q][r]);
                }
            }
            const double w = bc.h_W_m2K * bc.ambient_C * face.area() / 4.0;
            for (std::size_t node : face.nodes) sys.b[node] += w;
        }
    }

    // Prescribed temperatures. On an edge or corner shared by two tagged
    // surfaces the later condition in the list wins.
    const double unset = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> fixed(n, unset);
    for (const DirichletBc& bc : problem.dirichlet) {
        for (const mesh::BoundaryFace& face : m.faces) {
            if (face.tag != bc.tag) continue;
            for (std::size_t node : face.nodes) {
                const double value = bc.temperature_C(m.nodePosition(node));
                requireFinite(value, "prescribed temperature", "C");
                fixed[node] = value;
            }
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isnan(fixed[i])) {
            sys.dirichletNodes.push_back(i);
            sys.dirichletValues.push_back(fixed[i]);
        }
    }
    return sys;
}

ReducedSystem eliminateDirichlet(const FullSystem& full) {
    const std::size_t n = full.K.rows();
    ReducedSystem r;
    r.reducedIndex.assign(n, kFixed);
    r.fixedTemperature_C.assign(n, 0.0);
    std::vector<unsigned char> isFixed(n, 0);
    for (std::size_t i = 0; i < full.dirichletNodes.size(); ++i) {
        isFixed[full.dirichletNodes[i]] = 1;
        r.fixedTemperature_C[full.dirichletNodes[i]] = full.dirichletValues[i];
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (isFixed[i]) continue;
        r.reducedIndex[i] = r.freeNodes.size();
        r.freeNodes.push_back(i);
    }
    const std::size_t nf = r.freeNodes.size();
    if (nf == 0) {
        throw WorkbenchError(ErrorCode::InvalidParameter,
                             "every node has a prescribed temperature; there is nothing to solve");
    }

    r.A.rowPtr.assign(nf + 1, 0);
    for (std::size_t fr = 0; fr < nf; ++fr) {
        const std::size_t i = r.freeNodes[fr];
        std::size_t count = 0;
        for (std::size_t p = full.K.rowPtr[i]; p < full.K.rowPtr[i + 1]; ++p) count += isFixed[full.K.col[p]] ? 0 : 1;
        r.A.rowPtr[fr + 1] = r.A.rowPtr[fr] + count;
    }
    r.A.col.resize(r.A.rowPtr[nf]);
    r.A.val.resize(r.A.rowPtr[nf]);
    r.b.assign(nf, 0.0);
    for (std::size_t fr = 0; fr < nf; ++fr) {
        const std::size_t i = r.freeNodes[fr];
        std::size_t pos = r.A.rowPtr[fr];
        double rhs = full.b[i];
        for (std::size_t p = full.K.rowPtr[i]; p < full.K.rowPtr[i + 1]; ++p) {
            const std::uint32_t c = full.K.col[p];
            if (isFixed[c]) {
                rhs -= full.K.val[p] * r.fixedTemperature_C[c];
            } else {
                r.A.col[pos] = static_cast<std::uint32_t>(r.reducedIndex[c]);  // stays sorted: the map is monotone
                r.A.val[pos] = full.K.val[p];
                ++pos;
            }
        }
        r.b[fr] = rhs;
    }
    return r;
}

std::vector<double> expandSolution(const ReducedSystem& reduced, const std::vector<double>& x) {
    std::vector<double> T(reduced.reducedIndex.size());
    for (std::size_t i = 0; i < T.size(); ++i) {
        const std::size_t ri = reduced.reducedIndex[i];
        T[i] = ri == kFixed ? reduced.fixedTemperature_C[i] : x[ri];
    }
    return T;
}

}  // namespace wb::fem3d
