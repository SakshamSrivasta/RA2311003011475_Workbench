// Serial and multi-threaded execution of a batch of independent rod solves.
//
// Both versions call the same fea::solve() on the same inputs. The parallel
// version uses a bounded pool of `workers` std::threads that pull case indices
// from a shared atomic counter. Each result is written to its own preallocated
// slot, so the output order always matches the input order no matter which
// thread finished first, and no locking is needed on the results.
#pragma once

#include "core/common/Cancellation.h"
#include "core/fea/ThermalRod1D.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wb::batch {

struct BatchResult {
    // temperatures[i] holds the nodal temperatures of case i.
    std::vector<std::vector<double>> temperatures;
    // solved[i] is 1 once case i finished. All 1 unless the run was cancelled.
    std::vector<std::uint8_t> solved;
    std::size_t completedCount = 0;
    bool cancelled = false;

    bool complete() const { return !cancelled && completedCount == temperatures.size(); }
};

// Solves every case on the calling thread, in order.
BatchResult runSerial(const std::vector<fea::RodProblem>& cases,
                      const CancelToken* cancel = nullptr,
                      const ProgressFn& progress = nullptr);

// Solves the cases on `workers` threads (1..kMaxWorkers). With workers == 1 it
// still goes through the pool, which lets the benchmark measure pool overhead.
// If any solve throws, the remaining work stops and the first exception is
// rethrown after all threads have joined.
BatchResult runParallel(const std::vector<fea::RodProblem>& cases,
                        unsigned workers,
                        const CancelToken* cancel = nullptr,
                        const ProgressFn& progress = nullptr);

constexpr unsigned kMaxWorkers = 64;

// Largest |T_a - T_b| over all cases and nodes. Throws if the two results do
// not have the same shape or either one is incomplete.
double maxAbsDifference(const BatchResult& a, const BatchResult& b);

}  // namespace wb::batch
