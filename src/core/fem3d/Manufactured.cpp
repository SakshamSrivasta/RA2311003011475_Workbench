#include "core/fem3d/Manufactured.h"

#include <cmath>

namespace wb::fem3d {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

double ManufacturedSolution::temperature(const mesh::Point& p) const {
    double product = 1.0;
    for (int a = 0; a < 3; ++a) product *= std::sin(kPi * (p[a] - lo[a]) / (hi[a] - lo[a]));
    return base_C + amplitude_C * product + slope_C_per_m * (p[0] - lo[0]);
}

double ManufacturedSolution::source_W_m3(const mesh::Point& p) const {
    // laplacian of the sine product = -pi^2 (1/Lx^2 + 1/Ly^2 + 1/Lz^2) times itself;
    // the linear ramp has zero laplacian.
    double product = 1.0, invL2 = 0.0;
    for (int a = 0; a < 3; ++a) {
        const double L = hi[a] - lo[a];
        product *= std::sin(kPi * (p[a] - lo[a]) / L);
        invL2 += 1.0 / (L * L);
    }
    return conductivity_W_mK * amplitude_C * kPi * kPi * invL2 * product;
}

}  // namespace wb::fem3d
