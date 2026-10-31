// Runs long computations (benchmarks, big batches) off the HTTP threads so
// the browser keeps getting quick answers while work is in progress. The UI
// polls status() and can cancel at any time.
//
// Only one job runs at a time on purpose: two benchmarks fighting over the
// same cores would make both sets of timings meaningless.
#pragma once

#include "core/common/Cancellation.h"
#include "core/common/Stopwatch.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace wb::app {

class JobManager {
public:
    // The work function returns the job's result. It should poll `cancel`
    // and report progress; throwing marks the job failed (or cancelled for
    // WorkbenchError(Cancelled)).
    using Work = std::function<nlohmann::json(const CancelToken& cancel, const ProgressFn& progress)>;

    ~JobManager();

    // Returns the new job id, or nullopt if another job is still running.
    std::optional<std::string> start(const std::string& kind, Work work);

    // nullopt if the id is unknown.
    std::optional<nlohmann::json> status(const std::string& id) const;
    nlohmann::json resultOf(const std::string& id) const;  // throws if not finished

    bool cancel(const std::string& id);
    void shutdown();  // cancel everything and join worker threads

private:
    struct Job {
        std::string id, kind;
        CancelToken cancel;
        std::atomic<std::size_t> done{0}, total{0};
        Stopwatch clock;
        std::thread thread;

        mutable std::mutex mutex;  // guards the fields below
        std::string state = "running";  // running | done | failed | cancelled
        std::string error;
        nlohmann::json result;
        double elapsed_s = 0.0;
    };

    void reapFinished();  // caller holds mutex_

    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Job>> jobs_;
    int nextId_ = 1;
};

}  // namespace wb::app
