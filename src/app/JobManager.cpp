#include "app/JobManager.h"

#include "core/common/Errors.h"

#include <cstdio>
#include <vector>

namespace wb::app {

JobManager::~JobManager() { shutdown(); }

std::optional<std::string> JobManager::start(const std::string& kind, Work work) {
    std::lock_guard<std::mutex> lock(mutex_);
    reapFinished();
    for (const auto& [id, job] : jobs_) {
        std::lock_guard<std::mutex> jl(job->mutex);
        if (job->state == "running") return std::nullopt;
    }

    auto job = std::make_shared<Job>();
    // Zero-padded so the std::map keeps jobs in start order ("job-10" would sort before "job-2").
    char id[16];
    std::snprintf(id, sizeof(id), "job-%04d", nextId_++);
    job->id = id;
    job->kind = kind;

    job->thread = std::thread([job, work = std::move(work)]() {
        ProgressFn progress = [job](std::size_t done, std::size_t total) {
            job->total = total;
            job->done = done;
        };
        std::string state = "done", error;
        nlohmann::json result;
        try {
            result = work(job->cancel, progress);
        } catch (const WorkbenchError& e) {
            state = (e.code() == ErrorCode::Cancelled) ? "cancelled" : "failed";
            error = e.what();
        } catch (const std::exception& e) {
            state = "failed";
            error = std::string("Unexpected error: ") + e.what();
        }
        // Results are published in one step under the lock, so a poll never
        // sees a half-filled result.
        std::lock_guard<std::mutex> jl(job->mutex);
        job->state = state;
        job->error = error;
        job->result = std::move(result);
        job->elapsed_s = job->clock.elapsedSeconds();
    });

    jobs_[job->id] = job;
    return job->id;
}

std::optional<nlohmann::json> JobManager::status(const std::string& id) const {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = jobs_.find(id);
        if (it == jobs_.end()) return std::nullopt;
        job = it->second;
    }
    std::lock_guard<std::mutex> jl(job->mutex);
    nlohmann::json s = {{"id", job->id},
                        {"kind", job->kind},
                        {"state", job->state},
                        {"done", job->done.load()},
                        {"total", job->total.load()},
                        {"elapsed_s", job->state == "running" ? job->clock.elapsedSeconds() : job->elapsed_s}};
    if (!job->error.empty()) s["error"] = job->error;
    if (job->state == "done") s["result"] = job->result;
    return s;
}

nlohmann::json JobManager::resultOf(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = jobs_.find(id);
    if (it == jobs_.end()) throw WorkbenchError(ErrorCode::InvalidParameter, "Unknown job " + id + ".");
    std::lock_guard<std::mutex> jl(it->second->mutex);
    if (it->second->state != "done") {
        throw WorkbenchError(ErrorCode::InvalidParameter, "Job " + id + " has no results to export (state: " +
                                                              it->second->state + ").");
    }
    return it->second->result;
}

bool JobManager::cancel(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = jobs_.find(id);
    if (it == jobs_.end()) return false;
    it->second->cancel.requestCancel();
    return true;
}

void JobManager::shutdown() {
    std::vector<std::shared_ptr<Job>> all;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, job] : jobs_) {
            job->cancel.requestCancel();
            all.push_back(job);
        }
    }
    for (auto& job : all) {
        if (job->thread.joinable()) job->thread.join();
    }
}

void JobManager::reapFinished() {
    // Keep the last few finished jobs around for export; join their threads.
    std::size_t finished = 0;
    for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it) {
        auto& job = it->second;
        std::lock_guard<std::mutex> jl(job->mutex);
        if (job->state != "running" && job->thread.joinable()) job->thread.join();
        if (job->state != "running") ++finished;
    }
    while (finished > 20) {
        jobs_.erase(jobs_.begin());
        --finished;
    }
}

}  // namespace wb::app
