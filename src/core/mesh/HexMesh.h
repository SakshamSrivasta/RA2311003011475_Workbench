// Hexahedral mesh: the solid cells of a Grid3D with a compact node numbering,
// element connectivity and tagged boundary faces.
//
// Local node order inside an element is l = ix + 2*iy + 4*iz (x fastest), the
// same order as the tensor-product element matrices in fem3d/HexElement.
// Nodes are numbered with the axis that has the fewest grid nodes varying
// fastest, which gives the smallest bandwidth for a banded direct solver.
#pragma once

#include "core/mesh/BoxGeometry.h"
#include "core/mesh/Grid3D.h"

#include <cstddef>
#include <limits>
#include <vector>

namespace wb::mesh {

constexpr std::size_t kNoNode = std::numeric_limits<std::size_t>::max();
// About 330 bytes per node for the matrix; 10 M nodes is ~3.3 GB, enough.
constexpr std::size_t kMaxMeshNodes = 10'000'000;

// Element-local indices of the four nodes on the face with normal `axis` on
// `side` (0 = low coordinate, 1 = high), in face-local order f = iu + 2*iv
// where (u, v) are the other two axes in increasing order.
inline std::array<int, 4> faceLocalNodes(int axis, int side) {
    const int u = axis == 0 ? 1 : 0;
    const int v = axis == 2 ? 1 : 2;
    std::array<int, 4> out{};
    for (int iv = 0; iv < 2; ++iv) {
        for (int iu = 0; iu < 2; ++iu) {
            int ijk[3];
            ijk[axis] = side;
            ijk[u] = iu;
            ijk[v] = iv;
            out[iu + 2 * iv] = ijk[0] + 2 * ijk[1] + 4 * ijk[2];
        }
    }
    return out;
}

struct BoundaryFace {
    std::size_t element = 0;
    int axis = 0;  // normal axis
    int side = 0;  // 0 = face at the element's low coordinate, 1 = high
    std::array<std::size_t, 4> nodes{};  // global ids, order of faceLocalNodes()
    double size_u = 0.0, size_v = 0.0;   // edge lengths along the other two axes, lower axis first
    int tag = 0;                         // 0 until tagBoundaryFaces() assigns one
    double area() const { return size_u * size_v; }
};

// Selects boundary faces by normal axis, side and plane coordinate, limited
// to a rectangle on the other two axes (defaults cover the whole plane).
struct FaceRegion {
    int axis = 0;
    int side = -1;       // -1 = either side
    double plane = 0.0;  // coordinate on `axis`
    Point lo{-1e300, -1e300, -1e300};  // bounds on the other axes; the `axis` entry is ignored
    Point hi{1e300, 1e300, 1e300};
    int tag = 0;
};

struct HexMesh {
    Grid3D grid;
    std::array<int, 3> order{};  // numbering order: order[0] = fastest-varying axis
    std::vector<int> cellMaterial;            // per grid cell; kVoid where empty
    std::vector<std::size_t> gridNodeToNode;  // per grid node; kNoNode if unused
    std::vector<std::array<int, 3>> nodeIjk;  // per mesh node: grid indices
    std::vector<std::array<std::size_t, 8>> elements;
    std::vector<std::array<int, 3>> elementCell;  // grid cell of each element
    std::vector<int> elementMaterial;
    std::vector<BoundaryFace> faces;

    std::size_t nodeCount() const { return nodeIjk.size(); }
    std::size_t elementCount() const { return elements.size(); }
    std::size_t gridCellIndex(int i, int j, int k) const;
    std::size_t gridNodeIndex(int i, int j, int k) const;
    Point nodePosition(std::size_t node) const;
    Point elementSize(std::size_t element) const;
    Point elementOrigin(std::size_t element) const;  // corner with the lowest coordinates
    double elementVolume(std::size_t element) const;
    double solidVolume() const;
    double boundaryArea() const;
};

// Throws InvalidParameter if no cell is solid or the mesh would exceed
// kMaxMeshNodes. The grid should come from geometry.gridSpec() so that every
// box face is a grid line; otherwise cells straddling a face are classified
// by their centre.
HexMesh buildHexMesh(const BoxGeometry& geometry, const Grid3D& grid);

// Applies the regions in order; a later region overrides an earlier tag.
// Returns how many faces each region matched, so a region that matches
// nothing (a mistyped coordinate) can be reported instead of silently
// insulating a surface.
std::vector<std::size_t> tagBoundaryFaces(HexMesh& mesh, const std::vector<FaceRegion>& regions);

// Largest |i - j| over node pairs that share an element, under the current numbering.
std::size_t halfBandwidth(const HexMesh& mesh);

}  // namespace wb::mesh
