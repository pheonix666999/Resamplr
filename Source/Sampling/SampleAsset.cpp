#include "SampleAsset.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace padflow {
std::shared_ptr<const SampleAsset> SampleAsset::create(SampleAssetMetadata metadata,
                                                       std::vector<float> interleavedPcm) {
    if (metadata.channelCount == 0U || metadata.channelCount > 2U)
        throw std::invalid_argument("Milestone 0 assets require one or two channels");

    const auto expectedBytes = estimateDecodedBytes(metadata.frameCount, metadata.channelCount);
    if (expectedBytes == std::numeric_limits<std::uint64_t>::max() ||
        expectedBytes / sizeof(float) != static_cast<std::uint64_t>(interleavedPcm.size()))
        throw std::invalid_argument("PCM size does not match the asset metadata");

    return std::shared_ptr<const SampleAsset>(
        new SampleAsset(std::move(metadata), std::move(interleavedPcm)));
}

SampleAsset::SampleAsset(SampleAssetMetadata metadata, std::vector<float> interleavedPcm)
    : metadata_(std::move(metadata)), interleavedPcm_(std::move(interleavedPcm)) {}

const SampleAssetMetadata& SampleAsset::metadata() const noexcept {
    return metadata_;
}

std::span<const float> SampleAsset::interleavedPcm() const noexcept {
    return interleavedPcm_;
}

std::size_t SampleAsset::decodedBytes() const noexcept {
    return interleavedPcm_.size() * sizeof(float);
}

SampleAssetView SampleAsset::view() const noexcept {
    return {interleavedPcm_.data(), metadata_.frameCount, metadata_.channelCount,
            metadata_.sampleRate};
}

void DeferredSampleAssetReclaimer::retireAfterEpoch(const std::uint64_t retirementEpoch,
                                                    std::shared_ptr<const SampleAsset> asset) {
    if (asset == nullptr)
        return;

    std::lock_guard lock{mutex_};
    retired_.emplace_back(retirementEpoch, std::move(asset));
}

void DeferredSampleAssetReclaimer::acknowledgeAudioEpoch(const std::uint64_t epoch) noexcept {
    acknowledgedEpoch_.store(epoch, std::memory_order_release);
}

std::size_t DeferredSampleAssetReclaimer::collectAcknowledged() {
    const auto acknowledged = acknowledgedEpoch_.load(std::memory_order_acquire);
    std::lock_guard lock{mutex_};
    const auto previousSize = retired_.size();
    std::erase_if(retired_,
                  [acknowledged](const auto& entry) { return entry.first <= acknowledged; });
    return previousSize - retired_.size();
}

std::size_t DeferredSampleAssetReclaimer::pendingCount() const {
    std::lock_guard lock{mutex_};
    return retired_.size();
}

std::uint64_t estimateDecodedBytes(const std::uint64_t frames,
                                   const std::uint32_t channels) noexcept {
    constexpr auto bytesPerSample = static_cast<std::uint64_t>(sizeof(float));
    if (channels == 0U)
        return 0;

    const auto channelCount = static_cast<std::uint64_t>(channels);
    if (frames > std::numeric_limits<std::uint64_t>::max() / channelCount / bytesPerSample)
        return std::numeric_limits<std::uint64_t>::max();

    return frames * channelCount * bytesPerSample;
}
} // namespace padflow
