// Manufactured solution for verifying the 3D code on any box-union domain:
// pick a smooth T, derive the source q = -k laplacian(T) analytically, impose T
// on every boundary face, and the discretisation error can be measured
// directly. The field is a product of sines over the domain's bounding box
// plus a linear ramp, so it is genuinely three-dimensional.
#pragma once

#include "core/mesh/Grid3D.h"

namespace wb::fem3d {

struct ManufacturedSolution {
    mesh::Point lo{}, hi{};  // the sines span this box
    double conductivity_W_mK = 10.0;
    double base_C = 100.0;
    double amplitude_C = 50.0;
    double slope_C_per_m = 20.0;  // linear ramp along x

    double temperature(const mesh::Point& p) const;
    double source_W_m3(const mesh::Point& p) const;  // -k laplacian(T)
};

}  // namespace wb::fem3d
