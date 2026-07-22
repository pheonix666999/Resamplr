#include "BackgroundJobSystem.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace padflow {
void CancellationToken::cancel() noexcept {
    cancelled_.store(true, std::memory_order_release);
}

bool CancellationToken::isCancellationRequested() const noexcept {
    return cancelled_.load(std::memory_order_acquire);
}

void JobProgress::set(const float zeroToOne) noexcept {
    value_.store(std::clamp(zeroToOne, 0.0F, 1.0F), std::memory_order_release);
}

float JobProgress::snapshot() const noexcept {
    return value_.load(std::memory_order_acquire);
}

void JobHandle::cancel() const noexcept {
    if (cancellation != nullptr)
        cancellation->cancel();
}

std::size_t BackgroundJobSystem::defaultWorkerCount() noexcept {
    const auto hardwareThreads = static_cast<std::size_t>(std::thread::hardware_concurrency());
    const auto available = hardwareThreads > 2U ? hardwareThreads - 2U : 2U;
    return std::clamp(available, std::size_t{2U}, std::size_t{8U});
}

BackgroundJobSystem::BackgroundJobSystem(const std::size_t queueCapacity,
                                         const std::size_t workerCount)
    : queueCapacity_(std::max(std::size_t{1U}, queueCapacity)) {
    const auto actualWorkers = std::max(std::size_t{1U}, workerCount);
    workers_.reserve(actualWorkers);
    for (std::size_t index = 0; index < actualWorkers; ++index)
        workers_.emplace_back([this] { workerLoop(); });
}

BackgroundJobSystem::~BackgroundJobSystem() {
    shutdown();
}

std::optional<JobHandle> BackgroundJobSystem::submit(JobSpec spec, JobFunction function) {
    std::lock_guard lock{mutex_};
    if (stopping_ || outstanding_ >= queueCapacity_ || !function)
        return std::nullopt;

    auto cancellation = std::make_shared<CancellationToken>();
    auto progress = std::make_shared<JobProgress>();
    const auto id = nextId_.fetch_add(1U, std::memory_order_relaxed);
    PendingJob job{id, std::move(spec), std::move(function), cancellation, progress};

    const auto position = std::find_if(pending_.begin(), pending_.end(), [&](const auto& queued) {
        return queued.spec.priority < job.spec.priority;
    });
    pending_.insert(position, std::move(job));
    ++outstanding_;
    condition_.notify_one();
    return JobHandle{id, std::move(cancellation), std::move(progress)};
}

std::shared_ptr<const JobResult> BackgroundJobSystem::tryPopCompleted() {
    std::lock_guard lock{mutex_};
    if (completed_.empty())
        return {};

    auto result = std::move(completed_.front());
    completed_.pop_front();
    --outstanding_;
    return result;
}

void BackgroundJobSystem::cancelOwner(const juce::String& ownerUuid) {
    std::lock_guard lock{mutex_};
    for (auto& job : pending_)
        if (job.spec.ownerUuid == ownerUuid)
            job.cancellation->cancel();
    for (auto& [owner, cancellation] : active_)
        if (owner == ownerUuid)
            cancellation->cancel();
}

void BackgroundJobSystem::shutdown() {
    {
        std::lock_guard lock{mutex_};
        if (stopping_)
            return;

        stopping_ = true;
        for (auto& job : pending_)
            job.cancellation->cancel();
        for (auto& [owner, cancellation] : active_) {
            juce::ignoreUnused(owner);
            cancellation->cancel();
        }
    }

    condition_.notify_all();
    for (auto& worker : workers_)
        if (worker.joinable())
            worker.join();
    workers_.clear();
}

void BackgroundJobSystem::workerLoop() {
    for (;;) {
        PendingJob job;
        {
            std::unique_lock lock{mutex_};
            condition_.wait(lock, [&] { return stopping_ || !pending_.empty(); });
            if (stopping_ && pending_.empty())
                return;

            job = std::move(pending_.front());
            pending_.pop_front();
            active_.emplace_back(job.spec.ownerUuid, job.cancellation);
        }

        auto result = job.function(*job.cancellation, *job.progress);
        if (result == nullptr)
            result = std::make_shared<JobResult>(
                JobResult{job.spec, false, "Job returned no immutable result", {}});

        std::lock_guard lock{mutex_};
        const auto active = std::find_if(active_.begin(), active_.end(), [&](const auto& entry) {
            return entry.second == job.cancellation;
        });
        if (active != active_.end())
            active_.erase(active);
        completed_.push_back(std::move(result));
    }
}
} // namespace padflow
