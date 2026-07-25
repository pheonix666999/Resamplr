#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace padflow {
enum class JobKind : std::uint8_t {
    generic,
    sampleImport,
    sampleResolve,
    samplePreview,
    waveformCache,
    derivedAsset,
    recordedAsset
};

class CancellationToken final {
  public:
    void cancel() noexcept;
    [[nodiscard]] bool isCancellationRequested() const noexcept;

  private:
    std::atomic<bool> cancelled_{false};
};

class JobProgress final {
  public:
    void set(float zeroToOne) noexcept;
    [[nodiscard]] float snapshot() const noexcept;

  private:
    std::atomic<float> value_{0.0F};
};

struct JobSpec final {
    juce::String ownerUuid;
    juce::String targetUuid;
    std::uint64_t targetRevision{0};
    int priority{0};
    JobKind kind{JobKind::generic};
};

struct JobResult final {
    JobSpec target;
    bool succeeded{false};
    juce::String message;
    std::shared_ptr<const void> immutablePayload;
};

struct JobHandle final {
    std::uint64_t id{0};
    std::shared_ptr<CancellationToken> cancellation;
    std::shared_ptr<JobProgress> progress;

    void cancel() const noexcept;
};

class BackgroundJobSystem final {
  public:
    using JobFunction =
        std::function<std::shared_ptr<const JobResult>(const CancellationToken&, JobProgress&)>;

    explicit BackgroundJobSystem(std::size_t queueCapacity = 256U,
                                 std::size_t workerCount = defaultWorkerCount());
    ~BackgroundJobSystem();

    BackgroundJobSystem(const BackgroundJobSystem&) = delete;
    BackgroundJobSystem& operator=(const BackgroundJobSystem&) = delete;

    [[nodiscard]] std::optional<JobHandle> submit(JobSpec spec, JobFunction function);
    [[nodiscard]] std::shared_ptr<const JobResult> tryPopCompleted();
    void cancelOwner(const juce::String& ownerUuid);
    void shutdown();

    [[nodiscard]] static std::size_t defaultWorkerCount() noexcept;

  private:
    struct PendingJob final {
        std::uint64_t id{0};
        JobSpec spec;
        JobFunction function;
        std::shared_ptr<CancellationToken> cancellation;
        std::shared_ptr<JobProgress> progress;
    };

    void workerLoop();

    const std::size_t queueCapacity_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<PendingJob> pending_;
    std::vector<std::pair<juce::String, std::shared_ptr<CancellationToken>>> active_;
    std::deque<std::shared_ptr<const JobResult>> completed_;
    std::vector<std::thread> workers_;
    std::atomic<std::uint64_t> nextId_{1};
    std::size_t outstanding_{0U};
    bool stopping_{false};
};
} // namespace padflow
