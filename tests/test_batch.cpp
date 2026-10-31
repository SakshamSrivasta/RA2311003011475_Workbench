#include "TestHarness.h"

#include "core/batch/BatchGenerator.h"
#include "core/batch/BatchRunner.h"
#include "core/batch/Benchmark.h"
#include "core/common/Errors.h"

#include <atomic>

using namespace wb;
using namespace wb::batch;

namespace {
BatchConfig smallConfig() {
    BatchConfig c;
    c.caseCount = 37;  // deliberately not a multiple of the worker count
    c.elementsPerCase = 300;
    return c;
}
}  // namespace

TEST(batch_generator_is_reproducible) {
    const auto a = generateBatch(smallConfig());
    const auto b = generateBatch(smallConfig());
    CHECK(a.size() == 37);
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].conductivity_W_mK == b[i].conductivity_W_mK);
        CHECK(a[i].heatGeneration_W_m3 == b[i].heatGeneration_W_m3);
    }
    BatchConfig other = smallConfig();
    other.seed += 1;
    CHECK(generateBatch(other)[0].conductivity_W_mK != a[0].conductivity_W_mK);
}

TEST(batch_generator_values_are_positive_and_in_range) {
    const BatchConfig c = smallConfig();
    for (const auto& p : generateBatch(c)) {
        CHECK(p.conductivity_W_mK >= c.minConductivity_W_mK && p.conductivity_W_mK <= c.maxConductivity_W_mK);
        CHECK(p.heatGeneration_W_m3 >= c.minHeatGeneration_W_m3 && p.heatGeneration_W_m3 <= c.maxHeatGeneration_W_m3);
        CHECK(p.elements == c.elementsPerCase);
    }
}

TEST(batch_generator_first_case_is_pinned) {
    // Guards against someone swapping in std::uniform_real_distribution, which
    // would silently change the batch between compilers.
    const auto cases = generateBatch(BatchConfig{});
    CHECK_NEAR(cases[0].conductivity_W_mK, 74.99282646418, 1e-9);
    CHECK_NEAR(cases[0].heatGeneration_W_m3, 34307.048932283, 1e-6);
}

TEST(batch_serial_and_parallel_agree_to_1e8) {
    const auto cases = generateBatch(smallConfig());
    const BatchResult serial = runSerial(cases);
    CHECK(serial.complete());
    for (unsigned p : {1u, 2u, 3u, 4u, 8u}) {
        const BatchResult par = runParallel(cases, p);
        CHECK(par.complete());
        CHECK(maxAbsDifference(serial, par) <= 1e-8);
    }
}

TEST(batch_parallel_keeps_input_order) {
    const auto cases = generateBatch(smallConfig());
    const BatchResult par = runParallel(cases, 4);
    for (std::size_t i = 0; i < cases.size(); ++i) {
        // Each case's own solve, matched by index.
        const auto expected = fea::solve(cases[i]).temperature_C;
        CHECK(par.temperatures[i] == expected);
    }
}

TEST(batch_more_workers_than_cases) {
    BatchConfig c = smallConfig();
    c.caseCount = 2;
    const auto cases = generateBatch(c);
    CHECK(runParallel(cases, 8).complete());
}

TEST(batch_cancel_before_start_returns_incomplete) {
    const auto cases = generateBatch(smallConfig());
    CancelToken token;
    token.requestCancel();
    const BatchResult s = runSerial(cases, &token);
    const BatchResult p = runParallel(cases, 4, &token);
    CHECK(s.cancelled && s.completedCount == 0);
    CHECK(p.cancelled && p.completedCount == 0);
    CHECK_THROWS_CODE(maxAbsDifference(s, p), ErrorCode::InvalidParameter);
}

TEST(batch_cancel_midway_leaves_finished_cases_intact) {
    BatchConfig c = smallConfig();
    c.caseCount = 400;
    const auto cases = generateBatch(c);
    CancelToken token;
    std::atomic<std::size_t> seen{0};
    const BatchResult r = runParallel(cases, 4, &token, [&](std::size_t done, std::size_t) {
        seen = done;
        if (done >= 50) token.requestCancel();
    });
    CHECK(r.cancelled);
    CHECK(r.completedCount >= 50 && r.completedCount < 400);
    // Whatever was marked solved must be a full, correct solution.
    for (std::size_t i = 0; i < cases.size(); ++i) {
        if (r.solved[i]) CHECK(r.temperatures[i] == fea::solve(cases[i]).temperature_C);
        else CHECK(r.temperatures[i].empty());
    }
}

TEST(batch_rejects_bad_config) {
    BatchConfig c = smallConfig();
    c.caseCount = 0;
    CHECK_THROWS_CODE(generateBatch(c), ErrorCode::InvalidParameter);
    c = smallConfig();
    c.minConductivity_W_mK = -1.0;
    CHECK_THROWS_CODE(generateBatch(c), ErrorCode::InvalidParameter);
    c = smallConfig();
    c.minHeatGeneration_W_m3 = 5e4;
    c.maxHeatGeneration_W_m3 = 1e3;
    CHECK_THROWS_CODE(generateBatch(c), ErrorCode::InvalidParameter);
    CHECK_THROWS_CODE(runParallel(generateBatch(smallConfig()), 0), ErrorCode::InvalidParameter);
}

TEST(benchmark_worker_counts_are_capped) {
    CHECK((workerCountsFor(4, 8) == std::vector<unsigned>{1, 2, 3, 4}));
    CHECK((workerCountsFor(4, 2) == std::vector<unsigned>{1, 2}));
    CHECK((workerCountsFor(4, 1) == std::vector<unsigned>{1}));
}

TEST(benchmark_median) {
    CHECK_NEAR(median({5, 1, 3}), 3.0, 0.0);
    CHECK_NEAR(median({4, 1, 3, 2}), 2.5, 0.0);
}

TEST(benchmark_runs_and_reports_agreement) {
    BenchmarkConfig cfg;
    cfg.batch = smallConfig();
    cfg.measuredRuns = 5;
    const BenchmarkReport r = runBenchmark(cfg);
    CHECK(r.allAgree);
    CHECK(r.serial.runs_s.size() == 5);
    CHECK(!r.parallel.empty());
    CHECK_NEAR(r.parallel.front().speedup, 1.0, 1e-12);
    for (const auto& row : r.parallel) CHECK(row.maxAbsDiffVsSerial_C <= 1e-8);
}

TEST(benchmark_single_cpu_flags_speedup_not_assessable) {
    BenchmarkConfig cfg;
    cfg.batch = smallConfig();
    cfg.logicalCpusOverride = 1;
    const BenchmarkReport r = runBenchmark(cfg);
    CHECK(!r.speedupAssessable);
    CHECK(r.parallel.size() == 1);  // the parallel path still runs, with one worker
    CHECK(r.allAgree);
}

TEST(benchmark_cancel_throws_cancelled) {
    BenchmarkConfig cfg;
    cfg.batch = smallConfig();
    CancelToken token;
    token.requestCancel();
    CHECK_THROWS_CODE(runBenchmark(cfg, &token), ErrorCode::Cancelled);
}
