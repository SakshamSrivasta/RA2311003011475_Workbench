#include "app/HttpHelpers.h"
#include "app/Routes.h"
#include "core/batch/BatchRunner.h"
#include "core/batch/Benchmark.h"
#include "core/common/Stopwatch.h"
#include "core/report/Exports.h"

#include <iostream>

namespace wb::app {

namespace {

batch::BatchConfig batchFrom(const json& body) {
    batch::BatchConfig c;
    c.caseCount = intField(body, "cases", c.caseCount);
    c.elementsPerCase = intField(body, "elements", c.elementsPerCase);
    const double seed = numberField(body, "seed", static_cast<double>(c.seed));
    if (seed < 0 || seed > 9.0e15 || seed != static_cast<double>(static_cast<long long>(seed))) {
        throw WorkbenchError(ErrorCode::InvalidParameter, "Seed must be a whole number between 0 and 9e15.");
    }
    c.seed = static_cast<std::uint64_t>(seed);
    batch::validate(c);
    return c;
}

json timingJson(const batch::TimingRow& r) {
    return {{"workers", r.workers},  {"runs_s", r.runs_s},         {"median_s", r.median_s},
            {"speedup", r.speedup},  {"efficiency", r.efficiency}, {"maxAbsDiffVsSerial_C", r.maxAbsDiffVsSerial_C}};
}

void startJob(httplib::Response& res, AppContext& ctx, const std::string& kind, JobManager::Work work) {
    const auto id = ctx.jobs.start(kind, std::move(work));
    if (!id) {
        res.status = 409;
        res.set_content(json{{"error", "Another job is still running. Wait for it or press Cancel first."},
                             {"code", "Busy"}}.dump(),
                        "application/json");
        return;
    }
    std::cout << "[job] " << *id << " started (" << kind << ")" << std::endl;
    res.set_content(json{{"jobId", *id}}.dump(), "application/json");
}

}  // namespace

void registerPerformanceRoutes(httplib::Server& server, AppContext& ctx) {
    server.Post("/api/jobs/benchmark", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            const json body = parseBody(req);
            batch::BenchmarkConfig cfg;
            cfg.batch = batchFrom(body);
            cfg.warmupRuns = intField(body, "warmup", cfg.warmupRuns);
            cfg.measuredRuns = intField(body, "repetitions", cfg.measuredRuns);
            const int maxWorkers = intField(body, "maxWorkers", 4);
            if (cfg.warmupRuns < 0 || cfg.warmupRuns > 5 || cfg.measuredRuns < 1 || cfg.measuredRuns > 15) {
                throw WorkbenchError(ErrorCode::InvalidParameter, "Use 0-5 warm-up runs and 1-15 measured runs.");
            }
            if (maxWorkers < 1 || maxWorkers > 16) {
                throw WorkbenchError(ErrorCode::InvalidParameter, "Max workers must be between 1 and 16.");
            }
            cfg.maxWorkers = static_cast<unsigned>(maxWorkers);

            startJob(res, ctx, "benchmark", [cfg](const CancelToken& cancel, const ProgressFn& progress) {
                const auto rep = batch::runBenchmark(cfg, &cancel, progress);
                const auto sys = platform::querySystemInfo();
                json rows = json::array();
                for (const auto& r : rep.parallel) rows.push_back(timingJson(r));
                return json{{"config", {{"cases", cfg.batch.caseCount},
                                        {"elements", cfg.batch.elementsPerCase},
                                        {"seed", cfg.batch.seed},
                                        {"warmup", cfg.warmupRuns},
                                        {"repetitions", cfg.measuredRuns}}},
                            {"logicalCpus", rep.logicalCpus},
                            {"speedupAssessable", rep.speedupAssessable},
                            {"allAgree", rep.allAgree},
                            {"tolerance_C", rep.agreementTolerance_C},
                            {"serial", timingJson(rep.serial)},
                            {"parallel", rows},
                            {"csv", report::benchmarkCsv(cfg, rep, sys)}};
            });
        } catch (...) {
            writeCurrentError(res);
        }
    });

    // One batch run in a chosen mode - mostly to show progress and cancel on a long job.
    server.Post("/api/jobs/batch", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            const json body = parseBody(req);
            const batch::BatchConfig cfg = batchFrom(body);
            const int workers = intField(body, "workers", 0);  // 0 = plain serial loop
            if (workers < 0 || workers > static_cast<int>(batch::kMaxWorkers)) {
                throw WorkbenchError(ErrorCode::InvalidParameter, "Workers must be 0 (serial) to 64.");
            }
            startJob(res, ctx, "batch", [cfg, workers](const CancelToken& cancel, const ProgressFn& progress) {
                const auto cases = batch::generateBatch(cfg);
                Stopwatch sw;
                const auto r = workers == 0 ? batch::runSerial(cases, &cancel, progress)
                                            : batch::runParallel(cases, static_cast<unsigned>(workers), &cancel, progress);
                const double elapsed = sw.elapsedSeconds();
                if (r.cancelled) {
                    throw WorkbenchError(ErrorCode::Cancelled,
                                         "Cancelled after " + std::to_string(r.completedCount) + " of " +
                                             std::to_string(cases.size()) +
                                             " cases. Partial results were discarded; nothing was exported.");
                }
                // A short summary is enough for the UI; the full arrays can be large.
                const auto& first = r.temperatures.front();
                return json{{"mode", workers == 0 ? "serial" : "worker_pool"},
                            {"workers", workers},
                            {"cases", cases.size()},
                            {"elements", cfg.elementsPerCase},
                            {"compute_s", elapsed},
                            {"case0", {{"k", cases[0].conductivity_W_mK},
                                       {"q", cases[0].heatGeneration_W_m3},
                                       {"T_mid_C", first[first.size() / 2]}}}};
            });
        } catch (...) {
            writeCurrentError(res);
        }
    });

    server.Get(R"(/api/jobs/([\w-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        respondJson(res, [&] {
            const auto s = ctx.jobs.status(req.matches[1]);
            if (!s) throw WorkbenchError(ErrorCode::InvalidParameter, "Unknown job id.");
            json out = *s;
            if (out.contains("result")) out["result"].erase("csv");  // fetched separately
            return out;
        });
    });

    server.Post(R"(/api/jobs/([\w-]+)/cancel)", [&](const httplib::Request& req, httplib::Response& res) {
        respondJson(res, [&] {
            const bool found = ctx.jobs.cancel(req.matches[1]);
            if (found) std::cout << "[job] " << req.matches[1] << " cancel requested" << std::endl;
            return json{{"cancelRequested", found}};
        });
    });

    server.Get(R"(/api/jobs/([\w-]+)/csv)", [&](const httplib::Request& req, httplib::Response& res) {
        respondCsv(res, "benchmark.csv", [&]() -> std::string {
            const json result = ctx.jobs.resultOf(req.matches[1]);
            if (!result.contains("csv")) throw WorkbenchError(ErrorCode::InvalidParameter, "This job has no CSV export.");
            return result["csv"].get<std::string>();
        });
    });
}

}  // namespace wb::app
