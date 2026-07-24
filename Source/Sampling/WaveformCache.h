#pragma once

#include "App/ApplicationController.h"
#include "Sampling/SampleAsset.h"
#include "Utilities/BackgroundJobSystem.h"

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace padflow {
inline constexpr std::uint32_t waveformCacheAlgorithmVersion = 1U;
inline constexpr std::uint64_t defaultWaveformCacheBudgetBytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t waveformBaseFramesPerPeak = 64U;

struct WaveformCacheKey final {
    juce::String assetUuid;
    juce::String sourceFingerprint;
    std::uint32_t algorithmVersion{waveformCacheAlgorithmVersion};
    std::uint32_t channelCount{0U};
    std::uint64_t sourceFrameCount{0U};

    [[nodiscard]] friend bool operator==(const WaveformCacheKey&,
                                         const WaveformCacheKey&) = default;
};

struct WaveformPeak final {
    float minimum{0.0F};
    float maximum{0.0F};

    [[nodiscard]] friend bool operator==(const WaveformPeak&, const WaveformPeak&) = default;
};

struct WaveformCacheLevel final {
    std::uint64_t framesPerPeak{0U};
    std::uint64_t coveredSourceFrames{0U};
    std::uint32_t channelCount{0U};
    std::vector<WaveformPeak> channelInterleavedPeaks;

    [[nodiscard]] std::size_t peakBlockCount() const noexcept;
    [[nodiscard]] const WaveformPeak* peak(std::size_t blockIndex,
                                           std::uint32_t channel) const noexcept;
};

class WaveformCache final {
  public:
    [[nodiscard]] static std::shared_ptr<const WaveformCache>
    generate(const SampleAsset& asset, const CancellationToken& cancellation, JobProgress& progress,
             juce::String& error);

    [[nodiscard]] const WaveformCacheKey& key() const noexcept;
    [[nodiscard]] const std::vector<WaveformCacheLevel>& levels() const noexcept;
    [[nodiscard]] std::size_t memoryBytes() const noexcept;

  private:
    WaveformCache(WaveformCacheKey key, std::vector<WaveformCacheLevel> levels,
                  std::size_t memoryBytes);

    const WaveformCacheKey key_;
    const std::vector<WaveformCacheLevel> levels_;
    const std::size_t memoryBytes_;
};

class WaveformCacheRegistry final {
  public:
    explicit WaveformCacheRegistry(
        std::uint64_t budgetBytes = defaultWaveformCacheBudgetBytes) noexcept;

    [[nodiscard]] bool publish(std::shared_ptr<const WaveformCache> cache);
    [[nodiscard]] std::shared_ptr<const WaveformCache> find(const WaveformCacheKey& key) const;
    [[nodiscard]] bool invalidate(const juce::String& assetUuid);
    void clear();
    [[nodiscard]] std::uint64_t usedBytes() const;
    [[nodiscard]] std::uint64_t budgetBytes() const noexcept;
    [[nodiscard]] std::size_t uniqueCacheCount() const;

  private:
    const std::uint64_t budgetBytes_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<const WaveformCache>> caches_;
    std::uint64_t usedBytes_{0U};
};

struct WaveformCacheRequest final {
    JobSpec target;
    std::shared_ptr<const SampleAsset> asset;
};

struct WaveformCachePayload final {
    std::shared_ptr<const WaveformCache> cache;
};

class WaveformCacheGenerator final {
  public:
    [[nodiscard]] static std::optional<JobHandle> submit(BackgroundJobSystem& jobs,
                                                         WaveformCacheRequest request);
    [[nodiscard]] static std::shared_ptr<const JobResult>
    build(const WaveformCacheRequest& request, const CancellationToken& cancellation,
          JobProgress& progress);
    [[nodiscard]] static juce::Result commit(const JobResult& result,
                                             const ApplicationController& controller,
                                             WaveformCacheRegistry& registry);
};
} // namespace padflow
