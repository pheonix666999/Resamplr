#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace padflow {
struct SampleAssetView;

struct SampleAssetMetadata final {
    juce::String assetUuid;
    juce::String displayName;
    double sampleRate{0.0};
    std::uint32_t channelCount{0};
    std::uint64_t frameCount{0};
    juce::String sourceAssetUuid;
    juce::String provenance;
};

class SampleAsset final {
  public:
    static std::shared_ptr<const SampleAsset> create(SampleAssetMetadata metadata,
                                                     std::vector<float> interleavedPcm);

    [[nodiscard]] const SampleAssetMetadata& metadata() const noexcept;
    [[nodiscard]] std::span<const float> interleavedPcm() const noexcept;
    [[nodiscard]] std::size_t decodedBytes() const noexcept;
    [[nodiscard]] SampleAssetView view() const noexcept;

  private:
    SampleAsset(SampleAssetMetadata metadata, std::vector<float> interleavedPcm);

    const SampleAssetMetadata metadata_;
    const std::vector<float> interleavedPcm_;
};

struct SampleAssetView final {
    const float* interleavedData{nullptr};
    std::uint64_t frameCount{0};
    std::uint32_t channelCount{0};
    double sampleRate{0.0};
};

class DeferredSampleAssetReclaimer final {
  public:
    void retireAfterEpoch(std::uint64_t retirementEpoch, std::shared_ptr<const SampleAsset> asset);

    void acknowledgeAudioEpoch(std::uint64_t epoch) noexcept;
    [[nodiscard]] std::size_t collectAcknowledged();
    [[nodiscard]] std::size_t pendingCount() const;

  private:
    std::atomic<std::uint64_t> acknowledgedEpoch_{0U};
    mutable std::mutex mutex_;
    std::vector<std::pair<std::uint64_t, std::shared_ptr<const SampleAsset>>> retired_;
};

[[nodiscard]] std::uint64_t estimateDecodedBytes(std::uint64_t frames,
                                                 std::uint32_t channels) noexcept;

inline constexpr std::uint64_t defaultDecodedSampleBudgetBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
} // namespace padflow
