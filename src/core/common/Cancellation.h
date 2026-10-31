// Cooperative cancellation and progress reporting.
//
// Long-running core functions take an optional `const CancelToken*`. They
// check it between independent units of work (one rod solve, one training
// step) and stop early without touching results that were already written.
// Partially finished work is reported as cancelled, never returned as if it
// were complete.
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>

namespace wb {

class CancelToken {
public:
    void requestCancel() noexcept { cancelled_.store(true, std::memory_order_relaxed); }
    void reset() noexcept { cancelled_.store(false, std::memory_order_relaxed); }
    bool isCancelled() const noexcept { return cancelled_.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> cancelled_{false};
};

inline bool isCancelled(const CancelToken* token) noexcept {
    return token != nullptr && token->isCancelled();
}

// Called with (completed, total). May be called from worker threads, so the
// callback must be thread-safe (e.g. store into atomics).
using ProgressFn = std::function<void(std::size_t completed, std::size_t total)>;

}  // namespace wb
