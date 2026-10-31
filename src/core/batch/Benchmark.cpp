#include "core/batch/Benchmark.h"

#include "core/batch/BatchRunner.h"
#include "core/common/Errors.h"
#include "core/common/Stopwatch.h"

#include <algorithm>
#include <functional>
#include <thread>

namespace wb::batch {

std::vector<unsigned> workerCountsFor(unsigned maxWorkers, unsigned logicalCpus) {
    const unsigned cap = std::max(1u, std::min(maxWorkers, logicalCpus));
    std::vector<unsigned> counts;
    for (unsigned p = 1; p <= cap; ++p) counts.push_back(p);
    return counts;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t mid = v.size() / 2;
    return (v.size() % 2 == 1) ? v[mid] : 0.5 * (v[mid - 1] + v[mid]);
}

BenchmarkReport runBenchmark(const BenchmarkConfig& config, const CancelToken* cancel,
                             const ProgressFn& progress) {
    if (config.warmupRuns < 0 || config.measuredRuns < 1) {
        throw WorkbenchError(ErrorCode::InvalidParameter,
                             "Benchmark needs at least one measured run and a non-negative warm-up count.");
    }

    // Generated once, outside every timed region; all runs use these exact cases.
    const std::vector<fea::RodProblem> cases = generateBatch(config.batch);

    BenchmarkReport report;
    const unsigned detected = std::thread::hardware_concurrency();  // may be 0 if unknown
    report.logicalCpus = config.logicalCpusOverride ? config.logicalCpusOverride : std::max(1u, detected);
    report.speedupAssessable = report.logicalCpus > 1;
    report.workerCounts = workerCountsFor(config.maxWorkers, report.logicalCpus);

    const std::size_t runsPerConfig = static_cast<std::size_t>(config.warmupRuns + config.measuredRuns);
    const std::size_t totalRuns = runsPerConfig * (1 + report.workerCounts.size());
    std::size_t finishedRuns = 0;

    // Times one configuration; returns the last measured result for the agreement check.
    auto timeConfig = [&](TimingRow& row, const std::function<BatchResult()>& run) {
        BatchResult last;
        for (int r = 0; r < config.warmupRuns + config.measuredRuns; ++r) {
            Stopwatch sw;
            last = run();
            const double seconds = sw.elapsedSeconds();
            if (!last.complete()) throw WorkbenchError(ErrorCode::Cancelled, "Benchmark cancelled by user.");
            if (r >= config.warmupRuns) row.runs_s.push_back(seconds);
            if (progress) progress(++finishedRuns, totalRuns);
        }
        row.median_s = median(row.runs_s);
        return last;
    };

    report.serial.workers = 0;
    const BatchResult serialResult = timeConfig(report.serial, [&] { return runSerial(cases, cancel); });

    for (unsigned p : report.workerCounts) {
        TimingRow row;
        row.workers = p;
        const BatchResult parallelResult = timeConfig(row, [&] { return runParallel(cases, p, cancel); });
        row.maxAbsDiffVsSerial_C = maxAbsDifference(serialResult, parallelResult);
        if (row.maxAbsDiffVsSerial_C > report.agreementTolerance_C) report.allAgree = false;
        report.parallel.push_back(std::move(row));
    }

    const double t1 = report.parallel.front().median_s;
    for (TimingRow& row : report.parallel) {
        row.speedup = row.median_s > 0.0 ? t1 / row.median_s : 0.0;
        row.efficiency = row.speedup / row.workers;
    }
    report.serial.speedup = report.serial.median_s > 0.0 ? t1 / report.serial.median_s : 0.0;
    report.serial.efficiency = report.serial.speedup;
    return report;
}

}  // namespace wb::batch
