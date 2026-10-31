// Timing study for the batch solver: serial baseline plus the worker pool at
// 1, 2, ... up to 4 workers (capped at the logical CPU count).
//
// Only the compute call is timed. Case generation happens once before any
// timing, and nothing is read from disk or drawn on screen inside the timed
// region. Each configuration gets `warmupRuns` untimed runs, then
// `measuredRuns` timed runs; the median is reported.
#pragma once

#include "core/batch/BatchGenerator.h"
#include "core/common/Cancellation.h"

#include <vector>

namespace wb::batch {

struct BenchmarkConfig {
    BatchConfig batch;
    int warmupRuns = 1;
    int measuredRuns = 5;
    unsigned maxWorkers = 4;
    // 0 = detect with std::thread::hardware_concurrency(). Tests may override.
    unsigned logicalCpusOverride = 0;
};

struct TimingRow {
    unsigned workers = 0;              // 0 marks the plain serial loop
    std::vector<double> runs_s;        // each measured run, in seconds
    double median_s = 0.0;
    double speedup = 0.0;              // T1 / Tp, T1 = pool with one worker
    double efficiency = 0.0;           // speedup / p
    double maxAbsDiffVsSerial_C = 0.0; // agreement with the serial result
};

struct BenchmarkReport {
    unsigned logicalCpus = 1;
    bool speedupAssessable = true;     // false on a single-logical-CPU machine
    std::vector<unsigned> workerCounts;
    TimingRow serial;
    std::vector<TimingRow> parallel;   // one row per worker count, ascending
    double agreementTolerance_C = 1e-8;
    bool allAgree = true;
};

// Returns the worker counts to test: 1, 2, ..., min(maxWorkers, logicalCpus).
std::vector<unsigned> workerCountsFor(unsigned maxWorkers, unsigned logicalCpus);

double median(std::vector<double> values);

// Runs the whole study. Throws WorkbenchError(Cancelled) if cancelled, so a
// half-finished report is never returned. `progress` reports finished runs
// out of the total number of runs (warm-up and measured).
BenchmarkReport runBenchmark(const BenchmarkConfig& config,
                             const CancelToken* cancel = nullptr,
                             const ProgressFn& progress = nullptr);

}  // namespace wb::batch
