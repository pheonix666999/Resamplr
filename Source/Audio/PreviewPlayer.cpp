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
    return commands_.tryPush({PreviewCommandType::start, epoch, asset});
}

bool PreviewPlayer::requestStop(const std::uint64_t epoch) noexcept {
    return commands_.tryPush({PreviewCommandType::stop, epoch, nullptr});
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
            position_ = 0.0;
            increment_ =
                activeAsset_.sampleRate > 0.0 ? activeAsset_.sampleRate / outputSampleRate_ : 1.0;
            activeMeter_.store(activeAsset_.interleavedData != nullptr &&
                                   activeAsset_.frameCount > 0U,
                               std::memory_order_release);
        } else {
            activeAsset_ = {};
            position_ = 0.0;
            activeMeter_.store(false, std::memory_order_release);
        }
        acknowledgedEpoch_.store(command.epoch, std::memory_order_release);
    }

    if (!activeMeter_.load(std::memory_order_relaxed))
        return;
    const auto gain = volume_.load(std::memory_order_relaxed);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        if (position_ >= static_cast<double>(activeAsset_.frameCount)) {
            activeAsset_ = {};
            activeMeter_.store(false, std::memory_order_release);
            break;
        }
        const auto sourceLeft = interpolate(activeAsset_, 0U, position_);
        const auto sourceRight =
            activeAsset_.channelCount > 1U ? interpolate(activeAsset_, 1U, position_) : sourceLeft;
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

float PreviewPlayer::interpolate(const SampleAssetView& asset, const std::uint32_t channel,
                                 const double position) noexcept {
    if (asset.interleavedData == nullptr || asset.frameCount == 0U || channel >= asset.channelCount)
        return 0.0F;
    const auto base = static_cast<std::int64_t>(position);
    const auto fraction = static_cast<float>(position - static_cast<double>(base));
    const auto sample = [&](const std::int64_t frame) {
        const auto clamped =
            std::clamp<std::int64_t>(frame, 0, static_cast<std::int64_t>(asset.frameCount - 1U));
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
