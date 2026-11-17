// Element matrices for the 8-node trilinear hexahedron on an axis-aligned box.
//
// With local node order l = ix + 2*iy + 4*iz the shape functions are products
// of 1D hat functions, so every element integral separates into 1D factors:
//
//     K = k ( S_a (x) M_b (x) M_c  +  M_a (x) S_b (x) M_c  +  M_a (x) M_b (x) S_c )
//
// with the 1D stiffness S_L = (1/L) [[1,-1],[-1,1]] and mass M_L = (L/6) [[2,1],[1,2]].
// This is exact, not a quadrature approximation; stiffnessByQuadrature() is
// only there to prove it in a test.
#pragma once

#include "core/mesh/Grid3D.h"

#include <array>
#include <functional>

namespace wb::fem3d {

using Matrix8 = std::array<std::array<double, 8>, 8>;
using Matrix4 = std::array<std::array<double, 4>, 4>;
using Vector8 = std::array<double, 8>;

// Conduction stiffness of a box a x b x c with conductivity k.
Matrix8 stiffness(double a, double b, double c, double conductivity);
// Same by 2x2x2 Gauss quadrature, which is exact for this integrand.
Matrix8 stiffnessByQuadrature(double a, double b, double c, double conductivity);

// Consistent load of a uniform source q: q a b c / 8 at every node.
Vector8 bodyLoad(double a, double b, double c, double q);
// Consistent load of a source that varies in space, by 2x2x2 Gauss quadrature.
Vector8 bodyLoad(double a, double b, double c, const mesh::Point& origin,
                 const std::function<double(const mesh::Point&)>& source);

// "Mass" matrix of a bilinear face lu x lv (face-local order f = iu + 2*iv).
// Times h it is the convection contribution to the stiffness.
Matrix4 faceMass(double lu, double lv);

// Shape functions at local coordinates in [0,1]^3.
Vector8 shapeFunctions(double xi, double eta, double zeta);

}  // namespace wb::fem3d
