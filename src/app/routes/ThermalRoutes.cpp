#include "app/HttpHelpers.h"
#include "app/Routes.h"
#include "core/common/Stopwatch.h"
#include "core/fea/ThermalRod1D.h"
#include "core/report/Exports.h"

namespace wb::app {

namespace {

// Interactive requests return every node to the browser, so keep meshes to a
// size that plots quickly. The solver itself accepts far more (fea::kMaxElements).
constexpr int kMaxInteractiveElements = 100'000;

void validateInteractive(const fea::RodProblem& p) {
    fea::validate(p);
    if (p.elements > kMaxInteractiveElements) {
        throw WorkbenchError(ErrorCode::InvalidParameter,
                             "For interactive plots use N <= " + std::to_string(kMaxInteractiveElements) +
                                 ". Larger meshes can be run from the Performance view.");
    }
}

fea::RodProblem problemFrom(const json& body) {
    fea::RodProblem p;
    p.length_m = numberField(body, "L", p.length_m);
    p.area_m2 = numberField(body, "A", p.area_m2);
    p.conductivity_W_mK = numberField(body, "k", p.conductivity_W_mK);
    p.heatGeneration_W_m3 = numberField(body, "q", p.heatGeneration_W_m3);
    p.leftTemperature_C = numberField(body, "T0", p.leftTemperature_C);
    p.rightTemperature_C = numberField(body, "TL", p.rightTemperature_C);
    p.elements = intField(body, "N", p.elements);
    validateInteractive(p);
    return p;
}

std::vector<int> countsFrom(const json& body, const fea::RodProblem& base) {
    std::vector<int> counts{10, 20, 40, 80};
    if (body.contains("counts")) {
        if (!body["counts"].is_array() || body["counts"].empty() || body["counts"].size() > 8) {
            throw WorkbenchError(ErrorCode::InvalidParameter, "'counts' must be a list of 1 to 8 element counts.");
        }
        counts.clear();
        for (const auto& c : body["counts"]) {
            fea::RodProblem p = base;
            p.elements = intField(json{{"N", c}}, "N", 0);
            validateInteractive(p);
            counts.push_back(p.elements);
        }
    }
    return counts;
}

json exactCurve(const fea::RodProblem& p, int samples = 400) {
    std::vector<double> x(samples + 1), t(samples + 1);
    for (int i = 0; i <= samples; ++i) {
        x[i] = p.length_m * i / samples;
        t[i] = fea::exactTemperature(p, x[i]);
    }
    return {{"x", x}, {"T", t}};
}

json problemJson(const fea::RodProblem& p) {
    return {{"L", p.length_m}, {"A", p.area_m2}, {"k", p.conductivity_W_mK}, {"q", p.heatGeneration_W_m3},
            {"T0", p.leftTemperature_C}, {"TL", p.rightTemperature_C}, {"N", p.elements}};
}

}  // namespace

void registerThermalRoutes(httplib::Server& server, AppContext&) {
    server.Post("/api/thermal/solve", [](const httplib::Request& req, httplib::Response& res) {
        respondJson(res, [&] {
            const fea::RodProblem p = problemFrom(parseBody(req));
            Stopwatch sw;
            const fea::RodSolution s = fea::solve(p);
            const double solveMs = sw.elapsedSeconds() * 1000.0;

            std::vector<double> xm, tFe, tEx;
            for (std::size_t e = 0; e + 1 < s.x_m.size(); ++e) {
                xm.push_back(0.5 * (s.x_m[e] + s.x_m[e + 1]));
                tFe.push_back(0.5 * (s.temperature_C[e] + s.temperature_C[e + 1]));
                tEx.push_back(fea::exactTemperature(p, xm.back()));
            }
            const auto mid = fea::maxMidpointError(p, s);
            const auto nodal = fea::maxNodalError(p, s);
            return json{{"problem", problemJson(p)},
                        {"solveMs", solveMs},
                        {"x", s.x_m},
                        {"T", s.temperature_C},
                        {"exact", exactCurve(p)},
                        {"midpoints", {{"x", xm}, {"T_fe", tFe}, {"T_exact", tEx}}},
                        {"maxMidpointError_C", mid.maxAbsError_C},
                        {"maxMidpointErrorAt_m", mid.atX_m},
                        {"maxNodalError_C", nodal.maxAbsError_C}};
        });
    });

    server.Post("/api/thermal/convergence", [](const httplib::Request& req, httplib::Response& res) {
        respondJson(res, [&] {
            const json body = parseBody(req);
            const fea::RodProblem base = problemFrom(body);
            const std::vector<int> counts = countsFrom(body, base);

            json solutions = json::array();
            for (int n : counts) {
                fea::RodProblem p = base;
                p.elements = n;
                const auto s = fea::solve(p);
                solutions.push_back({{"N", n}, {"x", s.x_m}, {"T", s.temperature_C}});
            }
            json rows = json::array();
            for (const auto& r : fea::convergenceStudy(base, counts)) {
                rows.push_back({{"N", r.elements},
                                {"h_m", r.elementSize_m},
                                {"maxMidpointError_C", r.maxMidpointError_C},
                                {"maxNodalError_C", r.maxNodalError_C},
                                {"observedOrder", r.observedOrder}});
            }
            return json{{"problem", problemJson(base)}, {"rows", rows}, {"solutions", solutions},
                        {"exact", exactCurve(base)}};
        });
    });

    // ?what=solutions|midpoints|convergence ; body has the same fields as /convergence.
    server.Post("/api/thermal/export", [](const httplib::Request& req, httplib::Response& res) {
        const std::string what = req.has_param("what") ? req.get_param_value("what") : "solutions";
        respondCsv(res, "fea_" + what + ".csv", [&]() -> std::string {
            const json body = parseBody(req);
            const fea::RodProblem base = problemFrom(body);
            const std::vector<int> counts = countsFrom(body, base);
            if (what == "solutions") return report::feaSolutionsCsv(base, counts);
            if (what == "midpoints") return report::feaMidpointsCsv(base, counts);
            if (what == "convergence") return report::feaConvergenceCsv(base, counts);
            throw WorkbenchError(ErrorCode::InvalidParameter, "Unknown export '" + what + "'.");
        });
    });
}

}  // namespace wb::app
