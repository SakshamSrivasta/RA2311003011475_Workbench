#include "app/HttpHelpers.h"
#include "app/Routes.h"
#include "core/common/Stopwatch.h"
#include "core/io/NodalCsvReader.h"
#include "core/io/StlReader.h"
#include "core/io/TextFile.h"

#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace wb::app {

namespace {

json toJson(const io::Bounds& b) {
    return {{"min", {b.min.x, b.min.y, b.min.z}}, {"max", {b.max.x, b.max.y, b.max.z}}};
}

// Example files are picked by name from data/. Only plain file names are
// accepted, so a request can never read outside that folder.
fs::path sampleFile(const AppContext& ctx, const std::string& name) {
    const bool plain = !name.empty() && name.size() < 128 && name.find("..") == std::string::npos &&
                       std::all_of(name.begin(), name.end(), [](char c) {
                           return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
                       });
    if (!plain) throw WorkbenchError(ErrorCode::InvalidParameter, "Invalid example file name '" + name + "'.");
    const fs::path p = ctx.dataDir / name;
    if (!fs::is_regular_file(p)) {
        throw WorkbenchError(ErrorCode::MalformedFile,
                             "Example '" + name + "' is missing from the data folder. Re-extract the ZIP.");
    }
    return p;
}

// Text comes either from an uploaded file (request body) or a bundled example.
struct Source {
    std::string name;
    std::string text;
};

Source loadSource(const httplib::Request& req, const AppContext& ctx) {
    if (req.has_param("sample")) {
        const std::string name = req.get_param_value("sample");
        return {name, io::readTextFile(sampleFile(ctx, name).u8string())};
    }
    std::string name = req.has_header("X-File-Name") ? req.get_header_value("X-File-Name") : "upload";
    return {name, req.body};
}

}  // namespace

void registerGeometryRoutes(httplib::Server& server, AppContext& ctx) {
    server.Get("/api/samples", [&](const httplib::Request&, httplib::Response& res) {
        respondJson(res, [&] {
            json list = json::array();
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(ctx.dataDir, ec)) {
                const std::string ext = entry.path().extension().string();
                if (ext != ".stl" && ext != ".csv") continue;
                list.push_back({{"name", entry.path().filename().u8string()},
                                {"kind", ext == ".stl" ? "stl" : "nodal-csv"},
                                {"bytes", entry.file_size(ec)}});
            }
            std::sort(list.begin(), list.end(), [](const json& a, const json& b) { return a["name"] < b["name"]; });
            return list;
        });
    });

    server.Post("/api/geometry/stl", [&](const httplib::Request& req, httplib::Response& res) {
        respondJson(res, [&] {
            const Source src = loadSource(req, ctx);
            Stopwatch sw;
            const io::StlMesh mesh = io::parseAsciiStl(src.text);
            const double parseMs = sw.elapsedSeconds() * 1000.0;

            std::vector<double> positions;
            positions.reserve(mesh.vertices.size() * 3);
            for (const auto& v : mesh.vertices) {
                positions.insert(positions.end(), {v.x, v.y, v.z});
            }
            return json{{"fileName", src.name},
                        {"bytes", src.text.size()},
                        {"parseMs", parseMs},
                        {"solidName", mesh.name},
                        {"triangles", mesh.triangleCount()},
                        {"degenerateTriangles", mesh.degenerateTriangles},
                        {"solids", mesh.solidCount},
                        {"bounds", toJson(mesh.bounds)},
                        {"positions", std::move(positions)}};
        });
    });

    server.Post("/api/results/nodal", [&](const httplib::Request& req, httplib::Response& res) {
        respondJson(res, [&] {
            const Source src = loadSource(req, ctx);
            Stopwatch sw;
            const io::NodalDataset data = io::parseNodalCsv(src.text);
            const double parseMs = sw.elapsedSeconds() * 1000.0;

            // Column arrays are much smaller in JSON than one object per node.
            const std::size_t n = data.nodes.size();
            std::vector<long long> ids(n);
            std::vector<double> x(n), y(n), z(n), t(n);
            for (std::size_t i = 0; i < n; ++i) {
                const auto& r = data.nodes[i];
                ids[i] = r.nodeId;
                x[i] = r.position_m.x;
                y[i] = r.position_m.y;
                z[i] = r.position_m.z;
                t[i] = r.temperature_C;
            }
            return json{{"fileName", src.name},
                        {"bytes", src.text.size()},
                        {"parseMs", parseMs},
                        {"nodes", n},
                        {"comments", data.comments},
                        {"bounds_m", toJson(data.bounds_m)},
                        {"minTemperature_C", data.minTemperature_C},
                        {"maxTemperature_C", data.maxTemperature_C},
                        {"id", std::move(ids)},
                        {"x", std::move(x)},
                        {"y", std::move(y)},
                        {"z", std::move(z)},
                        {"t", std::move(t)}};
        });
    });
}

}  // namespace wb::app
