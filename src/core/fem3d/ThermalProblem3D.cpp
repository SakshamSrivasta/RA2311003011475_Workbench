#include "core/fem3d/ThermalProblem3D.h"

#include "core/common/Errors.h"

#include <algorithm>
#include <string>

namespace wb::fem3d {

DirichletBc fixedTemperature(int tag, double temperature_C) {
    DirichletBc bc;
    bc.tag = tag;
    bc.temperature_C = [temperature_C](const mesh::Point&) { return temperature_C; };
    return bc;
}

void validate(const mesh::HexMesh& mesh, const ThermalProblem3D& problem) {
    if (problem.materials.empty()) {
        throw WorkbenchError(ErrorCode::InvalidParameter, "the problem has no materials");
    }
    for (const Material& m : problem.materials) {
        const std::string kName = "conductivity of " + m.name;
        requirePositiveFinite(m.conductivity_W_mK, kName.c_str(), "W/(m K)");
        const std::string qName = "heat generation of " + m.name;
        requireFinite(m.heatGeneration_W_m3, qName.c_str(), "W/m^3");
    }
    for (int id : mesh.elementMaterial) {
        if (id < 0 || static_cast<std::size_t>(id) >= problem.materials.size()) {
            throw WorkbenchError(ErrorCode::InvalidParameter,
                                 "the mesh uses material id " + std::to_string(id) + " but only " +
                                     std::to_string(problem.materials.size()) + " materials are defined");
        }
    }

    std::vector<int> tags;
    for (const auto& f : mesh.faces) tags.push_back(f.tag);
    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    const auto requireTag = [&](int tag, const char* what) {
        if (!std::binary_search(tags.begin(), tags.end(), tag)) {
            throw WorkbenchError(ErrorCode::InvalidParameter,
                                 std::string(what) + " refers to face tag " + std::to_string(tag) +
                                     " but no boundary face carries it (were the regions applied?)");
        }
    };

    bool temperatureLevelFixed = false;
    for (const DirichletBc& bc : problem.dirichlet) {
        if (!bc.temperature_C) {
            throw WorkbenchError(ErrorCode::InvalidParameter, "a prescribed-temperature condition has no value");
        }
        requireTag(bc.tag, "a prescribed-temperature condition");
        temperatureLevelFixed = true;
    }
    for (const FluxBc& bc : problem.flux) {
        requireFinite(bc.flux_W_m2, "prescribed heat flux", "W/m^2");
        requireTag(bc.tag, "a heat-flux condition");
    }
    for (const ConvectionBc& bc : problem.convection) {
        requireFinite(bc.h_W_m2K, "convection coefficient", "W/(m^2 K)");
        requireFinite(bc.ambient_C, "ambient temperature", "C");
        if (bc.h_W_m2K < 0.0) {
            throw WorkbenchError(ErrorCode::InvalidParameter, "convection coefficient must not be negative");
        }
        requireTag(bc.tag, "a convection condition");
        if (bc.h_W_m2K > 0.0) temperatureLevelFixed = true;
    }
    if (!temperatureLevelFixed) {
        throw WorkbenchError(ErrorCode::InvalidParameter,
                             "no temperature is prescribed anywhere and there is no convection, so the "
                             "temperature level is undefined; add a prescribed temperature or a convection condition");
    }
}

}  // namespace wb::fem3d
