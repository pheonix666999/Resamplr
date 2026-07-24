#include "WaveformCache.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace padflow {
namespace {
constexpr std::size_t aggregationFactor = 4U;

[[nodiscard]] bool checkedPeakCount(const std::uint64_t blocks, const std::uint32_t channels,
                                    std::size_t& result) noexcept {
    if (blocks == 0U || channels == 0U ||
        blocks > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) / channels)
        return false;
    result = static_cast<std::size_t>(blocks) * static_cast<std::size_t>(channels);
    return result <= std::numeric_limits<std::size_t>::max() / sizeof(WaveformPeak);
}

[[nodiscard]] std::uint64_t divideRoundUp(const std::uint64_t value,
                                          const std::uint64_t divisor) noexcept {
    return value / divisor + static_cast<std::uint64_t>(value % divisor != 0U);
}

[[nodiscard]] float finiteSample(const float sample) noexcept {
    return std::isfinite(sample) ? sample : 0.0F;
}

std::shared_ptr<const JobResult> failedResult(const JobSpec& target, juce::String message) {
    return std::make_shared<const JobResult>(JobResult{target, false, std::move(message), {}});
}

[[nodiscard]] const ExternalAssetReference*
findAssetReference(const ProjectState& state, const juce::String& assetUuid) noexcept {
    const auto iterator = std::find_if(state.assets.begin(), state.assets.end(),
                                       [&](const auto& asset) { return asset.uuid == assetUuid; });
    return iterator == state.assets.end() ? nullptr : std::addressof(*iterator);
}
} // namespace

std::size_t WaveformCacheLevel::peakBlockCount() const noexcept {
    return channelCount == 0U
               ? 0U
               : channelInterleavedPeaks.size() / static_cast<std::size_t>(channelCount);
}

const WaveformPeak* WaveformCacheLevel::peak(const std::size_t blockIndex,
                                             const std::uint32_t channel) const noexcept {
    if (channelCount == 0U || channel >= channelCount || blockIndex >= peakBlockCount())
        return nullptr;
    const auto interleavedIndex = blockIndex * static_cast<std::size_t>(channelCount) + channel;
    return std::addressof(channelInterleavedPeaks[interleavedIndex]);
}

WaveformCache::WaveformCache(WaveformCacheKey key, std::vector<WaveformCacheLevel> levels,
                             const std::size_t memoryBytes)
    : key_(std::move(key)), levels_(std::move(levels)), memoryBytes_(memoryBytes) {}

std::shared_ptr<const WaveformCache> WaveformCache::generate(const SampleAsset& asset,
                                                             const CancellationToken& cancellation,
                                                             JobProgress& progress,
                                                             juce::String& error) {
    const auto& metadata = asset.metadata();
    const auto channels = metadata.channelCount;
    const auto frames = metadata.frameCount;
    const auto pcm = asset.interleavedPcm();
    if (metadata.assetUuid.trim().isEmpty() || metadata.contentFingerprint.trim().isEmpty()) {
        error = "Waveform source identity is incomplete";
        return {};
    }
    if ((channels != 1U && channels != 2U) || frames == 0U) {
        error = "Waveform source must contain one or two non-empty channels";
        return {};
    }
    if (frames > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) / channels ||
        pcm.size() != static_cast<std::size_t>(frames * channels)) {
        error = "Waveform source PCM size is inconsistent";
        return {};
    }

    const auto baseBlockCount = divideRoundUp(frames, waveformBaseFramesPerPeak);
    std::size_t basePeakCount = 0U;
    if (!checkedPeakCount(baseBlockCount, channels, basePeakCount)) {
        error = "Waveform cache size exceeds this platform";
        return {};
    }

    std::vector<WaveformCacheLevel> levels;
    levels.reserve(16U);
    WaveformCacheLevel base;
    base.framesPerPeak = waveformBaseFramesPerPeak;
    base.coveredSourceFrames = frames;
    base.channelCount = channels;
    base.channelInterleavedPeaks.resize(basePeakCount);

    for (std::uint64_t block = 0U; block < baseBlockCount; ++block) {
        if (cancellation.isCancellationRequested()) {
            error = "Waveform generation was cancelled";
            return {};
        }
        const auto firstFrame = block * waveformBaseFramesPerPeak;
        const auto lastFrame = std::min(frames, firstFrame + waveformBaseFramesPerPeak);
        for (std::uint32_t channel = 0U; channel < channels; ++channel) {
            auto minimum = std::numeric_limits<float>::infinity();
            auto maximum = -std::numeric_limits<float>::infinity();
            for (auto frame = firstFrame; frame < lastFrame; ++frame) {
                const auto sample =
                    finiteSample(pcm[static_cast<std::size_t>(frame * channels + channel)]);
                minimum = std::min(minimum, sample);
                maximum = std::max(maximum, sample);
            }
            base.channelInterleavedPeaks[static_cast<std::size_t>(block * channels + channel)] = {
                minimum, maximum};
        }
        progress.set(0.75F * static_cast<float>(static_cast<double>(block + 1U) /
                                                static_cast<double>(baseBlockCount)));
    }
    levels.push_back(std::move(base));

    while (levels.back().peakBlockCount() > 1U) {
        if (cancellation.isCancellationRequested()) {
            error = "Waveform generation was cancelled";
            return {};
        }
        const auto& previous = levels.back();
        const auto previousBlocks = previous.peakBlockCount();
        const auto nextBlocks =
            divideRoundUp(static_cast<std::uint64_t>(previousBlocks), aggregationFactor);
        std::size_t nextPeakCount = 0U;
        if (!checkedPeakCount(nextBlocks, channels, nextPeakCount) ||
            previous.framesPerPeak >
                std::numeric_limits<std::uint64_t>::max() / aggregationFactor) {
            error = "Waveform cache level exceeds this platform";
            return {};
        }

        WaveformCacheLevel next;
        next.framesPerPeak = previous.framesPerPeak * static_cast<std::uint64_t>(aggregationFactor);
        next.coveredSourceFrames = frames;
        next.channelCount = channels;
        next.channelInterleavedPeaks.resize(nextPeakCount);
        for (std::size_t block = 0U; block < static_cast<std::size_t>(nextBlocks); ++block) {
            const auto firstSourceBlock = block * aggregationFactor;
            const auto lastSourceBlock =
                std::min(previousBlocks, firstSourceBlock + aggregationFactor);
            for (std::uint32_t channel = 0U; channel < channels; ++channel) {
                auto minimum = std::numeric_limits<float>::infinity();
                auto maximum = -std::numeric_limits<float>::infinity();
                for (auto sourceBlock = firstSourceBlock; sourceBlock < lastSourceBlock;
                     ++sourceBlock) {
                    const auto& source =
                        previous.channelInterleavedPeaks[sourceBlock * channels + channel];
                    minimum = std::min(minimum, source.minimum);
                    maximum = std::max(maximum, source.maximum);
                }
                next.channelInterleavedPeaks[block * channels + channel] = {minimum, maximum};
            }
        }
        levels.push_back(std::move(next));
    }

    std::size_t memoryBytes = 0U;
    for (const auto& level : levels) {
        const auto bytes = level.channelInterleavedPeaks.size() * sizeof(WaveformPeak);
        if (bytes > std::numeric_limits<std::size_t>::max() - memoryBytes) {
            error = "Waveform cache byte count overflow";
            return {};
        }
        memoryBytes += bytes;
    }

    WaveformCacheKey key;
    key.assetUuid = metadata.assetUuid;
    key.sourceFingerprint = metadata.contentFingerprint;
    key.channelCount = channels;
    key.sourceFrameCount = frames;
    progress.set(1.0F);
    return std::shared_ptr<const WaveformCache>(
        new WaveformCache(std::move(key), std::move(levels), memoryBytes));
}

const WaveformCacheKey& WaveformCache::key() const noexcept {
    return key_;
}

const std::vector<WaveformCacheLevel>& WaveformCache::levels() const noexcept {
    return levels_;
}

std::size_t WaveformCache::memoryBytes() const noexcept {
    return memoryBytes_;
}

WaveformCacheRegistry::WaveformCacheRegistry(const std::uint64_t budgetBytes) noexcept
    : budgetBytes_(budgetBytes) {}

bool WaveformCacheRegistry::publish(std::shared_ptr<const WaveformCache> cache) {
    if (cache == nullptr || cache->key().assetUuid.trim().isEmpty())
        return false;
    const auto bytes = static_cast<std::uint64_t>(cache->memoryBytes());
    if (bytes > budgetBytes_)
        return false;

    const auto key = cache->key().assetUuid.toStdString();
    const std::scoped_lock lock{mutex_};
    const auto existing = caches_.find(key);
    const auto existingBytes = existing == caches_.end()
                                   ? 0U
                                   : static_cast<std::uint64_t>(existing->second->memoryBytes());
    if (usedBytes_ < existingBytes || bytes > budgetBytes_ - (usedBytes_ - existingBytes))
        return false;
    usedBytes_ = usedBytes_ - existingBytes + bytes;
    caches_[key] = std::move(cache);
    return true;
}

std::shared_ptr<const WaveformCache>
WaveformCacheRegistry::find(const WaveformCacheKey& key) const {
    const std::scoped_lock lock{mutex_};
    const auto iterator = caches_.find(key.assetUuid.toStdString());
    if (iterator == caches_.end() || iterator->second->key() != key)
        return {};
    return iterator->second;
}

bool WaveformCacheRegistry::invalidate(const juce::String& assetUuid) {
    const std::scoped_lock lock{mutex_};
    const auto iterator = caches_.find(assetUuid.toStdString());
    if (iterator == caches_.end())
        return false;
    usedBytes_ -= static_cast<std::uint64_t>(iterator->second->memoryBytes());
    caches_.erase(iterator);
    return true;
}

void WaveformCacheRegistry::clear() {
    const std::scoped_lock lock{mutex_};
    caches_.clear();
    usedBytes_ = 0U;
}

std::uint64_t WaveformCacheRegistry::usedBytes() const {
    const std::scoped_lock lock{mutex_};
    return usedBytes_;
}

std::uint64_t WaveformCacheRegistry::budgetBytes() const noexcept {
    return budgetBytes_;
}

std::size_t WaveformCacheRegistry::uniqueCacheCount() const {
    const std::scoped_lock lock{mutex_};
    return caches_.size();
}

std::optional<JobHandle> WaveformCacheGenerator::submit(BackgroundJobSystem& jobs,
                                                        WaveformCacheRequest request) {
    const auto target = request.target;
    return jobs.submit(target, [request = std::move(request)](const CancellationToken& cancellation,
                                                              JobProgress& progress) {
        return build(request, cancellation, progress);
    });
}

std::shared_ptr<const JobResult>
WaveformCacheGenerator::build(const WaveformCacheRequest& request,
                              const CancellationToken& cancellation, JobProgress& progress) {
    if (request.asset == nullptr)
        return failedResult(request.target, "Waveform generation has no source asset");
    if (request.target.targetUuid != request.asset->metadata().assetUuid)
        return failedResult(request.target, "Waveform target does not match its source asset");

    juce::String error;
    auto cache = WaveformCache::generate(*request.asset, cancellation, progress, error);
    if (cache == nullptr)
        return failedResult(request.target, std::move(error));
    auto payload =
        std::make_shared<const WaveformCachePayload>(WaveformCachePayload{std::move(cache)});
    return std::make_shared<const JobResult>(
        JobResult{request.target, true, "Waveform generation complete", std::move(payload)});
}

juce::Result WaveformCacheGenerator::commit(const JobResult& result,
                                            const ApplicationController& controller,
                                            WaveformCacheRegistry& registry) {
    if (!result.succeeded)
        return juce::Result::fail(result.message);
    const auto& project = controller.project();
    if (result.target.ownerUuid != project.uuid() ||
        result.target.targetRevision != project.revision())
        return juce::Result::fail("Waveform completion targets stale project state");
    const auto* reference = findAssetReference(project.state(), result.target.targetUuid);
    if (reference == nullptr)
        return juce::Result::fail("Waveform completion targets an unknown asset");
    const auto* payload = static_cast<const WaveformCachePayload*>(result.immutablePayload.get());
    if (payload == nullptr || payload->cache == nullptr)
        return juce::Result::fail("Waveform generation returned no immutable cache");
    const auto& key = payload->cache->key();
    if (key.assetUuid != reference->uuid ||
        key.sourceFingerprint != reference->contentFingerprint ||
        key.channelCount != reference->channels || key.sourceFrameCount != reference->frameCount)
        return juce::Result::fail("Waveform completion no longer matches its source asset");
    if (!registry.publish(payload->cache))
        return juce::Result::fail("Waveform cache registry budget would be exceeded");
    return juce::Result::ok();
}
} // namespace padflow
