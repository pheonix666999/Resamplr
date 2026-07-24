#include "PlaybackEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace padflow {
namespace {
float decibelsToLinear(const float decibels) noexcept {
    return std::pow(10.0F, decibels / 20.0F);
}

float secondsStep(const float seconds, const double sampleRate) noexcept {
    return seconds <= 0.0F ? 1.0F
                           : static_cast<float>(1.0 / (static_cast<double>(seconds) * sampleRate));
}
} // namespace

void PlaybackEngine::prepare(const double outputSampleRate) noexcept {
    outputSampleRate_ =
        std::isfinite(outputSampleRate) && outputSampleRate > 0.0 ? outputSampleRate : 48000.0;
    panic();
    commands_.resetWhenQuiescent();
}

void PlaybackEngine::publishSnapshot(const PlaybackSnapshot* snapshot) noexcept {
    snapshot_.store(snapshot, std::memory_order_release);
}

bool PlaybackEngine::enqueue(const AudioCommand& command) noexcept {
    return commands_.tryPush(command);
}

void PlaybackEngine::panic() noexcept {
    for (auto& voice : voices_)
        voice = {};
    activeVoices_.store(0U, std::memory_order_release);
    lastAllocatedVoice_.store(-1, std::memory_order_release);
}

void PlaybackEngine::handleCommand(const AudioCommand& command) noexcept {
    switch (command.type) {
    case AudioCommandType::triggerPad:
        trigger(command.objectIndex, command.generation, command.value);
        break;
    case AudioCommandType::releaseSource:
        release(command.generation);
        break;
    case AudioCommandType::panic:
    case AudioCommandType::stopAll:
        panic();
        break;
    default:
        break;
    }
}

void PlaybackEngine::processBlock(float* const left, float* const right,
                                  const std::size_t frameCount) noexcept {
    if (left == nullptr || right == nullptr)
        return;
    std::fill_n(left, frameCount, 0.0F);
    std::fill_n(right, frameCount, 0.0F);

    AudioCommand command;
    while (commands_.tryPop(command))
        handleCommand(command);

    float peakLeft = 0.0F;
    float peakRight = 0.0F;
    std::uint32_t active = 0U;
    for (auto& voice : voices_) {
        if (voice.stage == EnvelopeStage::inactive)
            continue;

        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            if (voice.stage == EnvelopeStage::inactive)
                break;
            if (voice.position >= static_cast<double>(voice.asset.frameCount)) {
                voice = {};
                break;
            }

            const auto envelope = advanceEnvelope(voice);
            const auto sampleLeft = interpolate(voice.asset, 0U, voice.position);
            const auto sampleRight = voice.asset.channelCount > 1U
                                         ? interpolate(voice.asset, 1U, voice.position)
                                         : sampleLeft;
            const auto renderedLeft = sampleLeft * voice.leftGain * envelope;
            const auto renderedRight = sampleRight * voice.rightGain * envelope;
            left[frame] += std::isfinite(renderedLeft) ? renderedLeft : 0.0F;
            right[frame] += std::isfinite(renderedRight) ? renderedRight : 0.0F;
            peakLeft = std::max(peakLeft, std::abs(left[frame]));
            peakRight = std::max(peakRight, std::abs(right[frame]));
            voice.position += voice.increment;
        }
        if (voice.stage != EnvelopeStage::inactive)
            ++active;
    }
    activeVoices_.store(active, std::memory_order_release);
    peakLeft_.store(peakLeft, std::memory_order_release);
    peakRight_.store(peakRight, std::memory_order_release);
    renderedBlocks_.fetch_add(1U, std::memory_order_relaxed);
}

void PlaybackEngine::trigger(const std::uint32_t padIndex, const std::uint32_t sourceId,
                             const float velocityInput) noexcept {
    const auto* snapshot = snapshot_.load(std::memory_order_acquire);
    if (snapshot == nullptr || padIndex >= totalPadCount)
        return;
    const auto& pad = snapshot->pads[padIndex];
    const auto velocity = static_cast<std::uint8_t>(std::clamp(velocityInput, 1.0F, 127.0F));

    if (pad.playbackMode == PlaybackMode::toggle) {
        bool stopped = false;
        for (auto& voice : voices_)
            if (voice.stage != EnvelopeStage::inactive && voice.padIndex == padIndex &&
                voice.sourceId == sourceId) {
                releaseVoice(voice);
                stopped = true;
            }
        if (stopped)
            return;
    }

    if (pad.polyphonyMode == PolyphonyMode::mono)
        for (auto& voice : voices_)
            if (voice.stage != EnvelopeStage::inactive && voice.padIndex == padIndex)
                releaseVoice(voice);
    if (pad.chokeGroup != 0U)
        for (auto& voice : voices_)
            if (voice.stage != EnvelopeStage::inactive && voice.chokeGroup == pad.chokeGroup &&
                voice.padIndex != padIndex)
                releaseVoice(voice);

    for (const auto& layer : pad.layers) {
        if (!layer.enabled || layer.asset.interleavedData == nullptr ||
            layer.asset.frameCount == 0U || velocity < layer.velocityMinimum ||
            velocity > layer.velocityMaximum)
            continue;
        const auto index = allocateVoice(padIndex);
        auto& voice = voices_[index];
        voice = {};
        voice.asset = layer.asset;
        voice.padIndex = padIndex;
        voice.sourceId = sourceId;
        voice.chokeGroup = pad.chokeGroup;
        voice.playbackMode = pad.playbackMode;
        voice.stage = EnvelopeStage::attack;
        voice.increment =
            (layer.asset.sampleRate / outputSampleRate_) *
            std::pow(2.0, static_cast<double>(pad.coarseSemitones) / 12.0 +
                              static_cast<double>(pad.fineCents + layer.tuningCents) / 1200.0);
        voice.sustain = pad.envelope.sustainLevel;
        voice.attackStep = secondsStep(pad.envelope.attackSeconds, outputSampleRate_);
        voice.decayStep =
            secondsStep(pad.envelope.decaySeconds, outputSampleRate_) * (1.0F - voice.sustain);
        voice.releaseStep = secondsStep(pad.envelope.releaseSeconds, outputSampleRate_);
        const auto pan = std::clamp(pad.pan + layer.pan, -1.0F, 1.0F);
        constexpr float halfPi = 1.57079632679F;
        const auto angle = (pan + 1.0F) * 0.5F * halfPi;
        const auto gain =
            pad.gainLinear * layer.gainLinear * (static_cast<float>(velocity) / 127.0F);
        voice.leftGain = gain * std::cos(angle);
        voice.rightGain = gain * std::sin(angle);
        voice.triggerAge = nextTriggerAge_++;
        lastAllocatedVoice_.store(static_cast<int>(index), std::memory_order_release);
    }
}

void PlaybackEngine::release(const std::uint32_t sourceId) noexcept {
    for (auto& voice : voices_)
        if (voice.stage != EnvelopeStage::inactive && voice.sourceId == sourceId &&
            voice.playbackMode != PlaybackMode::oneShot)
            releaseVoice(voice);
}

void PlaybackEngine::releaseVoice(Voice& voice) noexcept {
    if (voice.stage == EnvelopeStage::inactive)
        return;
    voice.stage = EnvelopeStage::release;
    voice.releaseStep = std::max(voice.releaseStep, 1.0F / 128.0F);
}

std::size_t PlaybackEngine::allocateVoice(const std::uint32_t padIndex) noexcept {
    const auto* snapshot = snapshot_.load(std::memory_order_relaxed);
    const auto limit = snapshot != nullptr ? snapshot->pads[padIndex].maximumVoices : 1U;
    std::size_t padCount = 0U;
    std::size_t oldestPad = 0U;
    std::size_t firstInactive = voices_.size();
    std::uint64_t oldestPadAge = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t index = 0; index < voices_.size(); ++index) {
        const auto& voice = voices_[index];
        if (voice.stage == EnvelopeStage::inactive) {
            if (firstInactive == voices_.size())
                firstInactive = index;
            continue;
        }
        if (voice.padIndex == padIndex) {
            ++padCount;
            if (voice.triggerAge < oldestPadAge) {
                oldestPadAge = voice.triggerAge;
                oldestPad = index;
            }
        }
    }
    if (padCount >= limit) {
        std::size_t releasedPad = voices_.size();
        for (std::size_t index = 0; index < voices_.size(); ++index)
            if (voices_[index].padIndex == padIndex &&
                voices_[index].stage == EnvelopeStage::release &&
                (releasedPad == voices_.size() ||
                 voices_[index].envelope < voices_[releasedPad].envelope))
                releasedPad = index;
        return releasedPad != voices_.size() ? releasedPad : oldestPad;
    }
    if (firstInactive != voices_.size())
        return firstInactive;

    std::size_t released = voices_.size();
    for (std::size_t index = 0; index < voices_.size(); ++index)
        if (voices_[index].stage == EnvelopeStage::release &&
            (released == voices_.size() || voices_[index].envelope < voices_[released].envelope))
            released = index;
    if (released != voices_.size())
        return released;

    std::size_t oldest = 0U;
    for (std::size_t index = 1U; index < voices_.size(); ++index)
        if (voices_[index].triggerAge < voices_[oldest].triggerAge)
            oldest = index;
    return oldest;
}

float PlaybackEngine::interpolate(const SampleAssetView& asset, const std::uint32_t channel,
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

float PlaybackEngine::advanceEnvelope(Voice& voice) const noexcept {
    switch (voice.stage) {
    case EnvelopeStage::attack:
        voice.envelope = std::min(1.0F, voice.envelope + voice.attackStep);
        if (voice.envelope >= 1.0F)
            voice.stage = EnvelopeStage::decay;
        break;
    case EnvelopeStage::decay:
        voice.envelope = std::max(voice.sustain, voice.envelope - voice.decayStep);
        if (voice.envelope <= voice.sustain)
            voice.stage = EnvelopeStage::sustain;
        break;
    case EnvelopeStage::sustain:
        break;
    case EnvelopeStage::release:
        voice.envelope = std::max(0.0F, voice.envelope - voice.releaseStep);
        if (voice.envelope <= 0.0F)
            voice = {};
        break;
    case EnvelopeStage::inactive:
        break;
    }
    return voice.envelope;
}

PlaybackMetrics PlaybackEngine::metrics() const noexcept {
    return {activeVoices_.load(std::memory_order_acquire),
            peakLeft_.load(std::memory_order_acquire), peakRight_.load(std::memory_order_acquire),
            renderedBlocks_.load(std::memory_order_acquire)};
}

std::size_t PlaybackEngine::activeVoiceCount() const noexcept {
    return activeVoices_.load(std::memory_order_acquire);
}

int PlaybackEngine::lastAllocatedVoiceIndex() const noexcept {
    return lastAllocatedVoice_.load(std::memory_order_acquire);
}

PlaybackSnapshot makePlaybackSnapshot(const ProjectState& project,
                                      const SampleAssetRegistry& assets) {
    PlaybackSnapshot snapshot;
    for (std::size_t bankIndex = 0; bankIndex < padBankCount; ++bankIndex)
        for (std::size_t padIndex = 0; padIndex < padsPerBank; ++padIndex) {
            const auto globalIndex = toGlobalPadIndex(bankIndex, padIndex);
            const auto& source = project.banks[bankIndex].pads[padIndex];
            auto& destination = snapshot.pads[globalIndex];
            destination.playbackMode = source.parameters.playbackMode;
            destination.polyphonyMode = source.parameters.polyphonyMode;
            destination.chokeGroup = source.parameters.chokeGroup;
            destination.maximumVoices = source.parameters.maximumVoices;
            destination.gainLinear = decibelsToLinear(source.parameters.gainDecibels);
            destination.pan = source.parameters.pan;
            destination.coarseSemitones = static_cast<float>(source.parameters.coarseSemitones);
            destination.fineCents = source.parameters.fineCents;
            destination.envelope = source.parameters.envelope;
            for (std::size_t layerIndex = 0; layerIndex < minimumLayersPerPad; ++layerIndex) {
                const auto& sourceLayer = source.layers[layerIndex];
                auto& destinationLayer = destination.layers[layerIndex];
                destinationLayer.enabled = sourceLayer.enabled;
                destinationLayer.velocityMinimum = sourceLayer.velocityMinimum;
                destinationLayer.velocityMaximum = sourceLayer.velocityMaximum;
                destinationLayer.gainLinear = decibelsToLinear(sourceLayer.gainDecibels);
                destinationLayer.pan = sourceLayer.pan;
                destinationLayer.tuningCents = sourceLayer.tuningCents;
                const auto asset = assets.find(sourceLayer.assetUuid);
                if (asset != nullptr)
                    destinationLayer.asset = asset->view();
            }
        }
    return snapshot;
}
} // namespace padflow
