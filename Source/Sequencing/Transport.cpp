#include "Transport.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace padflow {
namespace {
constexpr double twoPi = 6.28318530717958647692;

std::int64_t countInMultiplier(const CountInBars bars) noexcept {
    switch (bars) {
    case CountInBars::off:
        return 0;
    case CountInBars::one:
        return 1;
    case CountInBars::two:
        return 2;
    }
    return 0;
}
} // namespace

juce::Result makeTransportConfiguration(const TempoMap& tempoMap, const TimeSignature timeSignature,
                                        const MusicalTime patternLength,
                                        const std::uint32_t sampleRate,
                                        const std::uint64_t generation, const float metronomeVolume,
                                        TransportConfiguration& output) {
    if (!isSupportedTimeSignature(timeSignature) || patternLength <= MusicalTime{})
        return juce::Result::fail("Transport pattern length or time signature is invalid");
    if (!std::isfinite(metronomeVolume) || metronomeVolume < 0.0F || metronomeVolume > 1.0F)
        return juce::Result::fail("Metronome volume must be between zero and one");

    TransportConfiguration candidate;
    candidate.generation = generation;
    candidate.sampleRate = sampleRate;
    candidate.timeSignature = timeSignature;
    candidate.metronomeVolume = metronomeVolume;
    if (const auto result =
            tempoMap.absoluteFrameAt(patternLength, sampleRate, candidate.patternLengthFrames);
        result.failed())
        return result;
    if (candidate.patternLengthFrames <= 0)
        return juce::Result::fail("Transport pattern must resolve to at least one frame");

    const auto beatTicks = ticksPerBeat(timeSignature);
    const auto barTicks = ticksPerBar(timeSignature);
    if (const auto result =
            tempoMap.absoluteFrameAt({barTicks, 0U}, sampleRate, candidate.firstBarLengthFrames);
        result.failed())
        return result;

    for (std::int64_t tick = 0; MusicalTime{tick, 0U} < patternLength; tick += beatTicks) {
        if (candidate.beatCount >= candidate.maximumBeatCount)
            return juce::Result::fail("Transport beat table exceeds the 128-bar limit");
        auto& beatFrame = candidate.beatFrames[candidate.beatCount];
        if (const auto result = tempoMap.absoluteFrameAt({tick, 0U}, sampleRate, beatFrame);
            result.failed())
            return result;
        candidate.accentedBeats[candidate.beatCount] = tick % barTicks == 0;
        ++candidate.beatCount;
        if (tick > std::numeric_limits<std::int64_t>::max() - beatTicks)
            return juce::Result::fail("Transport beat position overflowed");
    }
    output = candidate;
    return juce::Result::ok();
}

std::int64_t TapTempoEstimator::tap(const std::int64_t timestampMilliseconds) noexcept {
    if (timestampMilliseconds < 0)
        return defaultTempoMicroBpm;
    if (count_ > 0U) {
        const auto previous = timestamps_[(count_ - 1U) % timestamps_.size()];
        if (timestampMilliseconds <= previous || timestampMilliseconds - previous > 2'000)
            reset();
    }
    timestamps_[count_ % timestamps_.size()] = timestampMilliseconds;
    ++count_;
    const auto available = std::min(count_, timestamps_.size());
    if (available < 2U)
        return defaultTempoMicroBpm;
    const auto firstIndex = (count_ - available) % timestamps_.size();
    const auto lastIndex = (count_ - 1U) % timestamps_.size();
    const auto elapsed = timestamps_[lastIndex] - timestamps_[firstIndex];
    if (elapsed <= 0)
        return defaultTempoMicroBpm;
    const auto intervalCount = static_cast<std::int64_t>(available - 1U);
    const auto microBpm = (60'000'000'000LL * intervalCount + elapsed / 2) / elapsed;
    return std::clamp(microBpm, minimumTempoMicroBpm, maximumTempoMicroBpm);
}

void TapTempoEstimator::reset() noexcept {
    timestamps_.fill(0);
    count_ = 0U;
}

bool TransportEngine::publishConfiguration(
    const TransportConfiguration* const configuration) noexcept {
    if (configuration == nullptr || configuration->patternLengthFrames <= 0 ||
        configuration->beatCount > configuration->maximumBeatCount)
        return false;
    configuration_.store(configuration, std::memory_order_release);
    return true;
}

bool TransportEngine::enqueue(const TransportCommand& command) noexcept {
    if (commands_.tryPush(command))
        return true;
    commandOverflows_.fetch_add(1U, std::memory_order_relaxed);
    return false;
}

bool TransportEngine::play() noexcept {
    return enqueue({TransportCommandType::play, 0});
}

bool TransportEngine::stop() noexcept {
    return enqueue({TransportCommandType::stop, 0});
}

bool TransportEngine::record() noexcept {
    return enqueue({TransportCommandType::record, 0});
}

bool TransportEngine::returnToStart() noexcept {
    return enqueue({TransportCommandType::returnToStart, 0});
}

bool TransportEngine::setPositionFrames(const std::int64_t position) noexcept {
    return enqueue({TransportCommandType::setPositionFrames, position});
}

bool TransportEngine::setLoopEnabled(const bool enabled) noexcept {
    return enqueue({TransportCommandType::setLoopEnabled, enabled ? 1 : 0});
}

bool TransportEngine::setMetronomeEnabled(const bool enabled) noexcept {
    return enqueue({TransportCommandType::setMetronomeEnabled, enabled ? 1 : 0});
}

bool TransportEngine::setCountInBars(const CountInBars bars) noexcept {
    return enqueue({TransportCommandType::setCountInBars, static_cast<std::int64_t>(bars)});
}

bool TransportEngine::panic() noexcept {
    return enqueue({TransportCommandType::panic, 0});
}

void TransportEngine::beginPlayback(const bool recording) noexcept {
    if (activeConfiguration_ == nullptr)
        return;
    loopIteration_ = 0U;
    if (recording && countInBars_ != CountInBars::off) {
        state_ = TransportState::countIn;
        framePosition_ = 0;
        countInFramesRemaining_ =
            activeConfiguration_->firstBarLengthFrames * countInMultiplier(countInBars_);
    } else {
        state_ = recording ? TransportState::recording : TransportState::playing;
    }
    resetBeatCursor();
}

void TransportEngine::applyCommand(const TransportCommand& command) noexcept {
    switch (command.type) {
    case TransportCommandType::play:
        beginPlayback(false);
        break;
    case TransportCommandType::record:
        beginPlayback(true);
        break;
    case TransportCommandType::stop:
        state_ = TransportState::stopped;
        panicRequested_ = true;
        clickFramesRemaining_ = 0U;
        break;
    case TransportCommandType::returnToStart:
        framePosition_ = 0;
        loopIteration_ = 0U;
        resetBeatCursor();
        break;
    case TransportCommandType::setPositionFrames:
        if (activeConfiguration_ != nullptr)
            framePosition_ =
                std::clamp(command.value, std::int64_t{0},
                           activeConfiguration_->patternLengthFrames - std::int64_t{1});
        resetBeatCursor();
        break;
    case TransportCommandType::setLoopEnabled:
        loopEnabled_ = command.value != 0;
        break;
    case TransportCommandType::setMetronomeEnabled:
        metronomeEnabled_ = command.value != 0;
        break;
    case TransportCommandType::setCountInBars:
        countInBars_ =
            static_cast<CountInBars>(std::clamp(command.value, std::int64_t{0}, std::int64_t{2}));
        break;
    case TransportCommandType::panic:
        state_ = TransportState::stopped;
        panicRequested_ = true;
        clickFramesRemaining_ = 0U;
        break;
    }
}

void TransportEngine::beginBlock() noexcept {
    if (const auto* published = configuration_.load(std::memory_order_acquire);
        published != nullptr && published != activeConfiguration_) {
        activeConfiguration_ = published;
        if (framePosition_ >= activeConfiguration_->patternLengthFrames)
            framePosition_ = 0;
        resetBeatCursor();
    }
    TransportCommand command;
    while (commands_.tryPop(command))
        applyCommand(command);
    publishSnapshot();
}

void TransportEngine::resetBeatCursor() noexcept {
    nextBeatIndex_ = 0U;
    if (activeConfiguration_ == nullptr)
        return;
    while (nextBeatIndex_ < activeConfiguration_->beatCount &&
           activeConfiguration_->beatFrames[nextBeatIndex_] < framePosition_)
        ++nextBeatIndex_;
}

void TransportEngine::startClick(const bool accent) noexcept {
    if (activeConfiguration_ == nullptr)
        return;
    clickFramesRemaining_ = std::max(1U, activeConfiguration_->sampleRate / 100U);
    clickPhase_ = 0.0;
    const auto frequency = accent ? 1'800.0 : 1'200.0;
    clickPhaseStep_ = twoPi * frequency / static_cast<double>(activeConfiguration_->sampleRate);
    clickAmplitude_ = activeConfiguration_->metronomeVolume * (accent ? 0.35F : 0.22F);
}

void TransportEngine::processMetronomeAdd(float* const left, float* const right,
                                          const std::size_t frameCount) noexcept {
    if (left == nullptr || right == nullptr || activeConfiguration_ == nullptr)
        return;
    for (std::size_t frame = 0U; frame < frameCount; ++frame) {
        if (state_ != TransportState::stopped) {
            const auto clickEnabled = metronomeEnabled_ || state_ == TransportState::countIn;
            if (clickEnabled && nextBeatIndex_ < activeConfiguration_->beatCount &&
                framePosition_ == activeConfiguration_->beatFrames[nextBeatIndex_]) {
                startClick(activeConfiguration_->accentedBeats[nextBeatIndex_]);
                ++nextBeatIndex_;
            }
        }
        if (clickFramesRemaining_ > 0U) {
            const auto total = std::max(1U, activeConfiguration_->sampleRate / 100U);
            const auto envelope =
                static_cast<float>(clickFramesRemaining_) / static_cast<float>(total);
            const auto sample =
                clickAmplitude_ * envelope * static_cast<float>(std::sin(clickPhase_));
            left[frame] += sample;
            right[frame] += sample;
            clickPhase_ += clickPhaseStep_;
            if (clickPhase_ >= twoPi)
                clickPhase_ -= twoPi;
            --clickFramesRemaining_;
        }
        if (state_ == TransportState::stopped)
            continue;
        ++framePosition_;
        if (state_ == TransportState::countIn) {
            --countInFramesRemaining_;
            if (countInFramesRemaining_ <= 0) {
                state_ = TransportState::recording;
                framePosition_ = 0;
                loopIteration_ = 0U;
                resetBeatCursor();
            } else if (framePosition_ >= activeConfiguration_->firstBarLengthFrames) {
                framePosition_ = 0;
                resetBeatCursor();
            }
            continue;
        }
        if (framePosition_ >= activeConfiguration_->patternLengthFrames) {
            if (loopEnabled_) {
                framePosition_ = 0;
                ++loopIteration_;
                resetBeatCursor();
            } else {
                framePosition_ = activeConfiguration_->patternLengthFrames;
                state_ = TransportState::stopped;
                panicRequested_ = true;
            }
        }
    }
    publishSnapshot();
}

bool TransportEngine::consumePanicRequest() noexcept {
    const auto requested = panicRequested_;
    panicRequested_ = false;
    return requested;
}

void TransportEngine::stopAndPanicWhenQuiescent() noexcept {
    state_ = TransportState::stopped;
    framePosition_ = 0;
    loopIteration_ = 0U;
    countInFramesRemaining_ = 0;
    clickFramesRemaining_ = 0U;
    commands_.resetWhenQuiescent();
    panicRequested_ = true;
    resetBeatCursor();
    publishSnapshot();
}

void TransportEngine::publishSnapshot() noexcept {
    publishedState_.store(static_cast<std::uint8_t>(state_), std::memory_order_release);
    publishedFramePosition_.store(framePosition_, std::memory_order_release);
    publishedLoopIteration_.store(loopIteration_, std::memory_order_release);
    publishedConfigurationGeneration_.store(
        activeConfiguration_ != nullptr ? activeConfiguration_->generation : 0U,
        std::memory_order_release);
    publishedLoopEnabled_.store(loopEnabled_, std::memory_order_release);
    publishedMetronomeEnabled_.store(metronomeEnabled_, std::memory_order_release);
}

TransportPositionSnapshot TransportEngine::snapshot() const noexcept {
    return {static_cast<TransportState>(publishedState_.load(std::memory_order_acquire)),
            publishedFramePosition_.load(std::memory_order_acquire),
            publishedLoopIteration_.load(std::memory_order_acquire),
            publishedConfigurationGeneration_.load(std::memory_order_acquire),
            publishedLoopEnabled_.load(std::memory_order_acquire),
            publishedMetronomeEnabled_.load(std::memory_order_acquire)};
}

std::uint64_t TransportEngine::commandOverflowCount() const noexcept {
    return commandOverflows_.load(std::memory_order_acquire);
}
} // namespace padflow
