#include "PreviewPlayer.h"

#include <algorithm>
#include <cmath>

namespace padflow {
void PreviewPlayer::prepare(const double outputSampleRate) noexcept {
    outputSampleRate_ =
        std::isfinite(outputSampleRate) && outputSampleRate > 0.0 ? outputSampleRate : 48000.0;
    panicWhenQuiescent();
    commands_.resetWhenQuiescent();
}

bool PreviewPlayer::publishAndStart(const SampleAsset* const asset,
                                    const std::uint64_t epoch) noexcept {
    const auto endFrame = asset != nullptr ? asset->metadata().frameCount : 0U;
    return publishAndStartSlice(asset, 0U, endFrame, false, epoch);
}

bool PreviewPlayer::publishAndStartSlice(const SampleAsset* const asset,
                                         const std::uint64_t startFrame,
                                         const std::uint64_t endFrame, const bool reverse,
                                         const std::uint64_t epoch) noexcept {
    return commands_.tryPush(
        {PreviewCommandType::start, epoch, asset, startFrame, endFrame, reverse});
}

bool PreviewPlayer::requestStop(const std::uint64_t epoch) noexcept {
    return commands_.tryPush({PreviewCommandType::stop, epoch, nullptr, 0U, 0U, false});
}

void PreviewPlayer::setVolume(const float volume) noexcept {
    volume_.store(std::clamp(volume, 0.0F, 1.0F), std::memory_order_release);
}

void PreviewPlayer::processAdd(float* const left, float* const right,
                               const std::size_t frameCount) noexcept {
    if (left == nullptr || right == nullptr)
        return;

    PreviewCommand command;
    while (commands_.tryPop(command)) {
        if (command.type == PreviewCommandType::start) {
            activeAsset_ = command.asset != nullptr ? command.asset->view() : SampleAssetView{};
            startFrame_ = std::min(command.startFrame, activeAsset_.frameCount);
            endFrame_ = std::min(command.endFrame, activeAsset_.frameCount);
            reverse_ = command.reverse;
            position_ = reverse_ && endFrame_ > startFrame_ ? static_cast<double>(endFrame_ - 1U)
                                                            : static_cast<double>(startFrame_);
            increment_ =
                activeAsset_.sampleRate > 0.0 ? activeAsset_.sampleRate / outputSampleRate_ : 1.0;
            if (reverse_)
                increment_ = -increment_;
            sourceFramePosition_.store(reverse_ && endFrame_ > startFrame_ ? endFrame_ - 1U
                                                                           : startFrame_,
                                       std::memory_order_release);
            activeMeter_.store(activeAsset_.interleavedData != nullptr && endFrame_ > startFrame_,
                               std::memory_order_release);
        } else {
            activeAsset_ = {};
            position_ = 0.0;
            startFrame_ = 0U;
            endFrame_ = 0U;
            reverse_ = false;
            sourceFramePosition_.store(0U, std::memory_order_release);
            activeMeter_.store(false, std::memory_order_release);
        }
        acknowledgedEpoch_.store(command.epoch, std::memory_order_release);
    }

    if (!activeMeter_.load(std::memory_order_relaxed))
        return;
    const auto gain = volume_.load(std::memory_order_relaxed);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        if (position_ < static_cast<double>(startFrame_) ||
            position_ >= static_cast<double>(endFrame_)) {
            activeAsset_ = {};
            activeMeter_.store(false, std::memory_order_release);
            break;
        }
        sourceFramePosition_.store(static_cast<std::uint64_t>(position_),
                                   std::memory_order_relaxed);
        const auto sourceLeft = interpolate(activeAsset_, 0U, position_, startFrame_, endFrame_);
        const auto sourceRight =
            activeAsset_.channelCount > 1U
                ? interpolate(activeAsset_, 1U, position_, startFrame_, endFrame_)
                : sourceLeft;
        const auto renderedLeft = sourceLeft * gain;
        const auto renderedRight = sourceRight * gain;
        left[frame] += std::isfinite(renderedLeft) ? renderedLeft : 0.0F;
        right[frame] += std::isfinite(renderedRight) ? renderedRight : 0.0F;
        position_ += increment_;
    }
}

void PreviewPlayer::panicWhenQuiescent() noexcept {
    activeAsset_ = {};
    position_ = 0.0;
    startFrame_ = 0U;
    endFrame_ = 0U;
    reverse_ = false;
    sourceFramePosition_.store(0U, std::memory_order_release);
    activeMeter_.store(false, std::memory_order_release);
}

bool PreviewPlayer::isActive() const noexcept {
    return activeMeter_.load(std::memory_order_acquire);
}

float PreviewPlayer::volume() const noexcept {
    return volume_.load(std::memory_order_acquire);
}

std::uint64_t PreviewPlayer::acknowledgedEpoch() const noexcept {
    return acknowledgedEpoch_.load(std::memory_order_acquire);
}

std::uint64_t PreviewPlayer::sourceFramePosition() const noexcept {
    return sourceFramePosition_.load(std::memory_order_acquire);
}

float PreviewPlayer::interpolate(const SampleAssetView& asset, const std::uint32_t channel,
                                 const double position, const std::uint64_t startFrame,
                                 const std::uint64_t endFrame) noexcept {
    if (asset.interleavedData == nullptr || asset.frameCount == 0U || channel >= asset.channelCount)
        return 0.0F;
    const auto base = static_cast<std::int64_t>(position);
    const auto fraction = static_cast<float>(position - static_cast<double>(base));
    const auto sample = [&](const std::int64_t frame) {
        const auto clamped = std::clamp<std::int64_t>(frame, static_cast<std::int64_t>(startFrame),
                                                      static_cast<std::int64_t>(endFrame - 1U));
        return asset
            .interleavedData[static_cast<std::size_t>(clamped) * asset.channelCount + channel];
    };
    const auto y0 = sample(base - 1);
    const auto y1 = sample(base);
    const auto y2 = sample(base + 1);
    const auto y3 = sample(base + 2);
    const auto c0 = y1;
    const auto c1 = 0.5F * (y2 - y0);
    const auto c2 = y0 - 2.5F * y1 + 2.0F * y2 - 0.5F * y3;
    const auto c3 = 0.5F * (y3 - y0) + 1.5F * (y1 - y2);
    return ((c3 * fraction + c2) * fraction + c1) * fraction + c0;
}
} // namespace padflow
