#include "core/fem3d/Postprocess.h"

#include "core/fem3d/HexElement.h"

#include <algorithm>
#include <cmath>

namespace wb::fem3d {

std::vector<double> reactions(const FullSystem& full, const std::vector<double>& T) {
    std::vector<double> KT(full.K.rows());
    full.K.multiply(T.data(), KT.data());
    std::vector<double> out;
    out.reserve(full.dirichletNodes.size());
    for (std::size_t node : full.dirichletNodes) out.push_back(KT[node] - full.b[node]);
    return out;
}

HeatBalance heatBalance(const mesh::HexMesh& mesh, const ThermalProblem3D& problem, const FullSystem& full,
                        const std::vector<double>& T) {
    HeatBalance hb;
    for (std::size_t e = 0; e < mesh.elementCount(); ++e) {
        const Material& mat = problem.materials[static_cast<std::size_t>(mesh.elementMaterial[e])];
        hb.generated_W += mat.heatGeneration_W_m3 * mesh.elementVolume(e);
        if (problem.source_W_m3) {
            const mesh::Point s = mesh.elementSize(e);
            const Vector8 f = bodyLoad(s[0], s[1], s[2], mesh.elementOrigin(e), problem.source_W_m3);
            for (double v : f) hb.generated_W += v;
        }
    }
    for (const mesh::BoundaryFace& face : mesh.faces) {
        for (const FluxBc& bc : problem.flux) {
            if (bc.tag == face.tag) hb.fluxIn_W += bc.flux_W_m2 * face.area();
        }
        for (const ConvectionBc& bc : problem.convection) {
            if (bc.tag != face.tag) continue;
            // integral of T_h over a bilinear face = area/4 * sum of its nodal values
            double sum = 0.0;
            for (std::size_t node : face.nodes) sum += T[node];
            hb.convectedOut_W += bc.h_W_m2K * face.area() * (0.25 * sum - bc.ambient_C);
        }
    }
    for (double r : reactions(full, T)) hb.reactionIn_W += r;
    hb.imbalance_W = hb.generated_W + hb.fluxIn_W + hb.reactionIn_W - hb.convectedOut_W;
    return hb;
}

ErrorNorms errorNorms(const mesh::HexMesh& mesh, const std::vector<double>& T,
                      const std::function<double(const mesh::Point&)>& exact) {
    ErrorNorms norms;
    for (std::size_t i = 0; i < mesh.nodeCount(); ++i) {
        norms.maxNodal_C = std::max(norms.maxNodal_C, std::abs(T[i] - exact(mesh.nodePosition(i))));
    }
    const double g[2] = {0.5 - 0.28867513459481287, 0.5 + 0.28867513459481287};
    double sum = 0.0, volume = 0.0;
    for (std::size_t e = 0; e < mesh.elementCount(); ++e) {
        const mesh::Point s = mesh.elementSize(e), o = mesh.elementOrigin(e);
        const auto& nodes = mesh.elements[e];
        const double weight = 0.125 * s[0] * s[1] * s[2];
        volume += s[0] * s[1] * s[2];
        for (double gz : g) {
            for (double gy : g) {
                for (double gx : g) {
                    const Vector8 N = shapeFunctions(gx, gy, gz);
                    double Th = 0.0;
                    for (int l = 0; l < 8; ++l) Th += N[l] * T[nodes[l]];
                    const double d = Th - exact({o[0] + s[0] * gx, o[1] + s[1] * gy, o[2] + s[2] * gz});
                    sum += weight * d * d;
                }
            }
        }
    }
    norms.rms_C = std::sqrt(sum / volume);
    return norms;
}

}  // namespace wb::fem3d
