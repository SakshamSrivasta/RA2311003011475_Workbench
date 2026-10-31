#include "core/batch/BatchRunner.h"

#include "core/common/Errors.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <mutex>
#include <sstream>
#include <thread>

namespace wb::batch {

namespace {

BatchResult makeEmptyResult(std::size_t n) {
    BatchResult r;
    r.temperatures.resize(n);
    r.solved.assign(n, 0);
    return r;
}

}  // namespace

BatchResult runSerial(const std::vector<fea::RodProblem>& cases, const CancelToken* cancel,
                      const ProgressFn& progress) {
    BatchResult result = makeEmptyResult(cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        if (isCancelled(cancel)) {
            result.cancelled = true;
            break;
        }
        result.temperatures[i] = fea::solve(cases[i]).temperature_C;
        result.solved[i] = 1;
        ++result.completedCount;
        if (progress) progress(result.completedCount, cases.size());
    }
    return result;
}

BatchResult runParallel(const std::vector<fea::RodProblem>& cases, unsigned workers,
                        const CancelToken* cancel, const ProgressFn& progress) {
    if (workers < 1 || workers > kMaxWorkers) {
        std::ostringstream msg;
        msg << "Worker count must be between 1 and " << kMaxWorkers << " (got " << workers << ").";
        throw WorkbenchError(ErrorCode::InvalidParameter, msg.str());
    }

    BatchResult result = makeEmptyResult(cases.size());
    const std::size_t total = cases.size();

    std::atomic<std::size_t> nextIndex{0};
    std::atomic<std::size_t> completed{0};
    std::atomic<bool> stop{false};
    std::exception_ptr firstError;
    std::mutex errorMutex;

    auto worker = [&]() {
        while (!stop.load(std::memory_order_relaxed)) {
            if (isCancelled(cancel)) {
                stop.store(true, std::memory_order_relaxed);
                break;
            }
            const std::size_t i = nextIndex.fetch_add(1, std::memory_order_relaxed);
            if (i >= total) break;
            try {
                // Each thread writes only to slot i, which no other thread touches.
                result.temperatures[i] = fea::solve(cases[i]).temperature_C;
                result.solved[i] = 1;
            } catch (...) {
                std::lock_guard<std::mutex> lock(errorMutex);
                if (!firstError) firstError = std::current_exception();
                stop.store(true, std::memory_order_relaxed);
                break;
            }
            const std::size_t done = completed.fetch_add(1, std::memory_order_relaxed) + 1;
            if (progress) progress(done, total);
        }
    };

    const unsigned threadCount = static_cast<unsigned>(std::min<std::size_t>(workers, std::max<std::size_t>(total, 1)));
    std::vector<std::thread> pool;
    pool.reserve(threadCount);
    for (unsigned t = 0; t < threadCount; ++t) pool.emplace_back(worker);
    for (std::thread& th : pool) th.join();  // join() also makes all writes visible here

    if (firstError) std::rethrow_exception(firstError);

    result.completedCount = completed.load();
    result.cancelled = result.completedCount < total;
    return result;
}

double maxAbsDifference(const BatchResult& a, const BatchResult& b) {
    if (!a.complete() || !b.complete()) {
        throw WorkbenchError(ErrorCode::InvalidParameter,
                             "Cannot compare batch results: at least one run was cancelled or incomplete.");
    }
    if (a.temperatures.size() != b.temperatures.size()) {
        throw WorkbenchError(ErrorCode::InvalidParameter, "Cannot compare batch results with different case counts.");
    }
    double worst = 0.0;
    for (std::size_t i = 0; i < a.temperatures.size(); ++i) {
        const auto& ta = a.temperatures[i];
        const auto& tb = b.temperatures[i];
        if (ta.size() != tb.size()) {
            throw WorkbenchError(ErrorCode::InvalidParameter, "Cannot compare batch results with different mesh sizes.");
        }
        for (std::size_t j = 0; j < ta.size(); ++j) {
            worst = std::max(worst, std::abs(ta[j] - tb[j]));
        }
    }
    return worst;
}

}  // namespace wb::batch
