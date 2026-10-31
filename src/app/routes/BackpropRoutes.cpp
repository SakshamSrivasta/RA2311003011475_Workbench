#include "app/HttpHelpers.h"
#include "app/Routes.h"
#include "core/io/TextFile.h"
#include "core/ml/Backprop.h"

namespace wb::app {

namespace {

json paramsJson(const ml::Params& p) { return {{"w", p.w}, {"b", p.b}, {"v", p.v}, {"c", p.c}}; }

}  // namespace

void registerBackpropRoutes(httplib::Server& server, AppContext&) {
    // Fixed to the assignment's sample and starting point; only the step
    // count can change, to see what happens past 100 steps.
    server.Get("/api/backprop", [](const httplib::Request& req, httplib::Response& res) {
        respondJson(res, [&] {
            double requested = 100;
            if (req.has_param("steps") && !io::parseDouble(req.get_param_value("steps"), requested)) {
                throw WorkbenchError(ErrorCode::InvalidParameter, "Steps must be a number.");
            }
            const int steps = intField(json{{"steps", requested}}, "steps", 100);
            if (steps < 1 || steps > 5000) throw WorkbenchError(ErrorCode::InvalidParameter, "Steps must be 1-5000.");

            const ml::Params p0;
            const ml::Sample s;
            const double lr = 0.1;
            const ml::ForwardPass f = ml::forward(p0, s);
            const ml::Gradients g = ml::analyticGradients(p0, s);
            const ml::Params p1 = ml::gradientStep(p0, g, lr);

            json check = json::array();
            for (const auto& r : ml::gradientCheck(p0, s, 1e-6, 1e-5)) {
                check.push_back({{"param", r.name},
                                 {"analytic", r.analytic},
                                 {"numerical", r.numerical},
                                 {"absError", r.absError},
                                 {"passed", r.passed}});
            }
            return json{{"sample", {{"x", s.x}, {"y", s.y}}},
                        {"learningRate", lr},
                        {"initial", paramsJson(p0)},
                        {"forward", {{"z", f.z}, {"h", f.h}, {"yHat", f.yHat}, {"loss", f.loss}}},
                        {"gradients", paramsJson(g)},
                        {"gradientCheck", check},
                        {"afterOneStep", paramsJson(p1)},
                        {"lossAfterOneStep", ml::loss(p1, s)},
                        {"lossHistory", ml::trainLossHistory(p0, s, lr, steps)}};
        });
    });
}

}  // namespace wb::app
