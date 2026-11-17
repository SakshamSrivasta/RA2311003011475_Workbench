#include "core/mesh/HexMesh.h"

#include "core/common/Errors.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace wb::mesh {

std::size_t HexMesh::gridCellIndex(int i, int j, int k) const {
    return static_cast<std::size_t>(i) +
           static_cast<std::size_t>(grid.cells(0)) * (static_cast<std::size_t>(j) + static_cast<std::size_t>(grid.cells(1)) * k);
}

std::size_t HexMesh::gridNodeIndex(int i, int j, int k) const {
    return static_cast<std::size_t>(i) +
           static_cast<std::size_t>(grid.nodes(0)) * (static_cast<std::size_t>(j) + static_cast<std::size_t>(grid.nodes(1)) * k);
}

Point HexMesh::nodePosition(std::size_t node) const {
    const auto& ijk = nodeIjk[node];
    return {grid.lines[0][ijk[0]], grid.lines[1][ijk[1]], grid.lines[2][ijk[2]]};
}

Point HexMesh::elementSize(std::size_t element) const {
    const auto& c = elementCell[element];
    return {grid.cellSize(0, c[0]), grid.cellSize(1, c[1]), grid.cellSize(2, c[2])};
}

Point HexMesh::elementOrigin(std::size_t element) const {
    const auto& c = elementCell[element];
    return {grid.lines[0][c[0]], grid.lines[1][c[1]], grid.lines[2][c[2]]};
}

double HexMesh::elementVolume(std::size_t element) const {
    const Point s = elementSize(element);
    return s[0] * s[1] * s[2];
}

double HexMesh::solidVolume() const {
    double v = 0.0;
    for (std::size_t e = 0; e < elements.size(); ++e) v += elementVolume(e);
    return v;
}

double HexMesh::boundaryArea() const {
    double a = 0.0;
    for (const BoundaryFace& f : faces) a += f.area();
    return a;
}

HexMesh buildHexMesh(const BoxGeometry& geometry, const Grid3D& grid) {
    validate(geometry);
    HexMesh m;
    m.grid = grid;
    const int cells[3] = {grid.cells(0), grid.cells(1), grid.cells(2)};
    const int nodes[3] = {grid.nodes(0), grid.nodes(1), grid.nodes(2)};
    for (int a = 0; a < 3; ++a) {
        if (cells[a] < 1) throw WorkbenchError(ErrorCode::InvalidParameter, "the grid has no cells along an axis");
    }

    // Classify every cell by the material at its centre.
    m.cellMaterial.assign(grid.cellCount(), kVoid);
    std::size_t solidCells = 0;
    for (int k = 0; k < cells[2]; ++k) {
        for (int j = 0; j < cells[1]; ++j) {
            for (int i = 0; i < cells[0]; ++i) {
                const Point centre{0.5 * (grid.lines[0][i] + grid.lines[0][i + 1]),
                                   0.5 * (grid.lines[1][j] + grid.lines[1][j + 1]),
                                   0.5 * (grid.lines[2][k] + grid.lines[2][k + 1])};
                const int material = geometry.materialAt(centre);
                m.cellMaterial[m.gridCellIndex(i, j, k)] = material;
                if (material != kVoid) ++solidCells;
            }
        }
    }
    if (solidCells == 0) {
        throw WorkbenchError(ErrorCode::InvalidParameter,
                             "no grid cell lies inside the geometry; check that the boxes and the grid use the same units");
    }

    // A grid node is a mesh node if any of the cells around it is solid.
    std::vector<unsigned char> used(grid.gridNodeCount(), 0);
    std::size_t usedCount = 0;
    for (int k = 0; k < cells[2]; ++k) {
        for (int j = 0; j < cells[1]; ++j) {
            for (int i = 0; i < cells[0]; ++i) {
                if (m.cellMaterial[m.gridCellIndex(i, j, k)] == kVoid) continue;
                for (int l = 0; l < 8; ++l) {
                    unsigned char& flag = used[m.gridNodeIndex(i + (l & 1), j + ((l >> 1) & 1), k + ((l >> 2) & 1))];
                    if (!flag) {
                        flag = 1;
                        ++usedCount;
                    }
                }
            }
        }
    }
    if (usedCount > kMaxMeshNodes) {
        throw WorkbenchError(ErrorCode::InvalidParameter,
                             "the mesh would have " + std::to_string(usedCount) + " nodes, above the limit of " +
                                 std::to_string(kMaxMeshNodes) + "; use a coarser spacing");
    }

    // Numbering: fewest grid nodes fastest. Ties keep the x, y, z order.
    m.order = {0, 1, 2};
    std::stable_sort(m.order.begin(), m.order.end(), [&](int a, int b) { return nodes[a] < nodes[b]; });
    const int fast = m.order[0], mid = m.order[1], slow = m.order[2];

    m.gridNodeToNode.assign(grid.gridNodeCount(), kNoNode);
    m.nodeIjk.reserve(usedCount);
    int idx[3];
    for (idx[slow] = 0; idx[slow] < nodes[slow]; ++idx[slow]) {
        for (idx[mid] = 0; idx[mid] < nodes[mid]; ++idx[mid]) {
            for (idx[fast] = 0; idx[fast] < nodes[fast]; ++idx[fast]) {
                const std::size_t g = m.gridNodeIndex(idx[0], idx[1], idx[2]);
                if (!used[g]) continue;
                m.gridNodeToNode[g] = m.nodeIjk.size();
                m.nodeIjk.push_back({idx[0], idx[1], idx[2]});
            }
        }
    }

    // Elements in the same sweep order as the nodes, for locality.
    m.elements.reserve(solidCells);
    m.elementCell.reserve(solidCells);
    m.elementMaterial.reserve(solidCells);
    int c[3];
    for (c[slow] = 0; c[slow] < cells[slow]; ++c[slow]) {
        for (c[mid] = 0; c[mid] < cells[mid]; ++c[mid]) {
            for (c[fast] = 0; c[fast] < cells[fast]; ++c[fast]) {
                const int material = m.cellMaterial[m.gridCellIndex(c[0], c[1], c[2])];
                if (material == kVoid) continue;
                std::array<std::size_t, 8> conn{};
                for (int l = 0; l < 8; ++l) {
                    conn[l] = m.gridNodeToNode[m.gridNodeIndex(c[0] + (l & 1), c[1] + ((l >> 1) & 1), c[2] + ((l >> 2) & 1))];
                }
                m.elements.push_back(conn);
                m.elementCell.push_back({c[0], c[1], c[2]});
                m.elementMaterial.push_back(material);
            }
        }
    }

    // Boundary faces: a solid cell's face whose neighbour is void or outside.
    for (std::size_t e = 0; e < m.elements.size(); ++e) {
        const auto& cc = m.elementCell[e];
        for (int axis = 0; axis < 3; ++axis) {
            for (int side = 0; side < 2; ++side) {
                int nb[3] = {cc[0], cc[1], cc[2]};
                nb[axis] += side ? 1 : -1;
                const bool outside = nb[axis] < 0 || nb[axis] >= cells[axis];
                if (!outside && m.cellMaterial[m.gridCellIndex(nb[0], nb[1], nb[2])] != kVoid) continue;
                BoundaryFace f;
                f.element = e;
                f.axis = axis;
                f.side = side;
                const auto local = faceLocalNodes(axis, side);
                for (int q = 0; q < 4; ++q) f.nodes[q] = m.elements[e][local[q]];
                const int u = axis == 0 ? 1 : 0;
                const int v = axis == 2 ? 1 : 2;
                f.size_u = grid.cellSize(u, cc[u]);
                f.size_v = grid.cellSize(v, cc[v]);
                m.faces.push_back(f);
            }
        }
    }
    return m;
}

std::vector<std::size_t> tagBoundaryFaces(HexMesh& mesh, const std::vector<FaceRegion>& regions) {
    std::vector<std::size_t> matched(regions.size(), 0);
    const double tol = mesh.grid.tolerance();
    for (BoundaryFace& face : mesh.faces) {
        const auto& cc = mesh.elementCell[face.element];
        const double plane = mesh.grid.lines[face.axis][cc[face.axis] + face.side];
        const int u = face.axis == 0 ? 1 : 0;
        const int v = face.axis == 2 ? 1 : 2;
        const double u0 = mesh.grid.lines[u][cc[u]], u1 = mesh.grid.lines[u][cc[u] + 1];
        const double v0 = mesh.grid.lines[v][cc[v]], v1 = mesh.grid.lines[v][cc[v] + 1];
        for (std::size_t r = 0; r < regions.size(); ++r) {
            const FaceRegion& region = regions[r];
            if (region.axis != face.axis) continue;
            if (region.side != -1 && region.side != face.side) continue;
            if (std::abs(plane - region.plane) > tol) continue;
            if (u0 < region.lo[u] - tol || u1 > region.hi[u] + tol) continue;
            if (v0 < region.lo[v] - tol || v1 > region.hi[v] + tol) continue;
            face.tag = region.tag;
            ++matched[r];
        }
    }
    return matched;
}

std::size_t halfBandwidth(const HexMesh& mesh) {
    std::size_t band = 0;
    for (const auto& conn : mesh.elements) {
        const auto [lo, hi] = std::minmax_element(conn.begin(), conn.end());
        band = std::max(band, *hi - *lo);
    }
    return band;
}

}  // namespace wb::mesh
