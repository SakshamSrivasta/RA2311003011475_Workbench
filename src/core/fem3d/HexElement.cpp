#include "core/fem3d/HexElement.h"

#include <cmath>

namespace wb::fem3d {

namespace {

struct OneD {
    double S[2][2];  // stiffness
    double M[2][2];  // mass
};

OneD oneD(double L) {
    OneD o;
    o.S[0][0] = o.S[1][1] = 1.0 / L;
    o.S[0][1] = o.S[1][0] = -1.0 / L;
    o.M[0][0] = o.M[1][1] = L / 3.0;
    o.M[0][1] = o.M[1][0] = L / 6.0;
    return o;
}

// Two-point Gauss rule mapped to [0,1]: points and weights.
constexpr double kGaussOffset = 0.28867513459481287;  // 0.5 / sqrt(3)
const double kGauss[2] = {0.5 - kGaussOffset, 0.5 + kGaussOffset};

}  // namespace

Vector8 shapeFunctions(double xi, double eta, double zeta) {
    const double nx[2] = {1.0 - xi, xi}, ny[2] = {1.0 - eta, eta}, nz[2] = {1.0 - zeta, zeta};
    Vector8 N{};
    for (int l = 0; l < 8; ++l) N[l] = nx[l & 1] * ny[(l >> 1) & 1] * nz[(l >> 2) & 1];
    return N;
}

Matrix8 stiffness(double a, double b, double c, double conductivity) {
    const OneD x = oneD(a), y = oneD(b), z = oneD(c);
    Matrix8 K{};
    for (int l = 0; l < 8; ++l) {
        const int ix = l & 1, iy = (l >> 1) & 1, iz = (l >> 2) & 1;
        for (int m = 0; m < 8; ++m) {
            const int jx = m & 1, jy = (m >> 1) & 1, jz = (m >> 2) & 1;
            K[l][m] = conductivity * (x.S[ix][jx] * y.M[iy][jy] * z.M[iz][jz] +
                                      x.M[ix][jx] * y.S[iy][jy] * z.M[iz][jz] +
                                      x.M[ix][jx] * y.M[iy][jy] * z.S[iz][jz]);
        }
    }
    return K;
}

Matrix8 stiffnessByQuadrature(double a, double b, double c, double conductivity) {
    Matrix8 K{};
    const double weight = 0.125 * a * b * c;  // (1/2)^3 per point times the volume
    for (double gz : kGauss) {
        for (double gy : kGauss) {
            for (double gx : kGauss) {
                const double nx[2] = {1.0 - gx, gx}, ny[2] = {1.0 - gy, gy}, nz[2] = {1.0 - gz, gz};
                const double dn[2] = {-1.0, 1.0};  // derivative of the 1D hat functions on [0,1]
                double grad[8][3];
                for (int l = 0; l < 8; ++l) {
                    const int ix = l & 1, iy = (l >> 1) & 1, iz = (l >> 2) & 1;
                    grad[l][0] = dn[ix] / a * ny[iy] * nz[iz];
                    grad[l][1] = nx[ix] * dn[iy] / b * nz[iz];
                    grad[l][2] = nx[ix] * ny[iy] * dn[iz] / c;
                }
                for (int l = 0; l < 8; ++l) {
                    for (int m = 0; m < 8; ++m) {
                        K[l][m] += conductivity * weight *
                                   (grad[l][0] * grad[m][0] + grad[l][1] * grad[m][1] + grad[l][2] * grad[m][2]);
                    }
                }
            }
        }
    }
    return K;
}

Vector8 bodyLoad(double a, double b, double c, double q) {
    Vector8 f{};
    f.fill(q * a * b * c / 8.0);
    return f;
}

Vector8 bodyLoad(double a, double b, double c, const mesh::Point& origin,
                 const std::function<double(const mesh::Point&)>& source) {
    Vector8 f{};
    const double weight = 0.125 * a * b * c;
    for (double gz : kGauss) {
        for (double gy : kGauss) {
            for (double gx : kGauss) {
                const double q = source({origin[0] + a * gx, origin[1] + b * gy, origin[2] + c * gz});
                const Vector8 N = shapeFunctions(gx, gy, gz);
                for (int l = 0; l < 8; ++l) f[l] += weight * q * N[l];
            }
        }
    }
    return f;
}

Matrix4 faceMass(double lu, double lv) {
    const OneD u = oneD(lu), v = oneD(lv);
    Matrix4 M{};
    for (int f = 0; f < 4; ++f) {
        const int iu = f & 1, iv = f >> 1;
        for (int g = 0; g < 4; ++g) {
            const int ju = g & 1, jv = g >> 1;
            M[f][g] = u.M[iu][ju] * v.M[iv][jv];
        }
    }
    return M;
}

}  // namespace wb::fem3d
