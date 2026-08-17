#include "Scheduler.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace padflow {
namespace {
struct NormalizedPosition final {
    std::int64_t value{0};
    std::int16_t loopDelta{0};
};

bool checkedAdd(const std::int64_t left, const std::int64_t right, std::int64_t& output) noexcept {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::lowest() - right))
        return false;
    output = left + right;
    return true;
}

bool checkedMultiply(const std::int64_t left, const std::int64_t right,
                     std::int64_t& output) noexcept {
    if (left == 0 || right == 0) {
        output = 0;
        return true;
    }
    if (left == -1 && right == std::numeric_limits<std::int64_t>::lowest())
        return false;
    if (right == -1 && left == std::numeric_limits<std::int64_t>::lowest())
        return false;
    const auto magnitudeLeft =
        left < 0 ? static_cast<std::uint64_t>(-(left + 1)) + 1U : static_cast<std::uint64_t>(left);
    const auto magnitudeRight = right < 0 ? static_cast<std::uint64_t>(-(right + 1)) + 1U
                                          : static_cast<std::uint64_t>(right);
    const auto negative = (left < 0) != (right < 0);
    const auto limit = negative
                           ? std::uint64_t{1} << 63U
                           : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (magnitudeRight != 0U && magnitudeLeft > limit / magnitudeRight)
        return false;
    const auto magnitude = magnitudeLeft * magnitudeRight;
    if (negative) {
        if (magnitude == (std::uint64_t{1} << 63U))
            output = std::numeric_limits<std::int64_t>::lowest();
        else
            output = -static_cast<std::int64_t>(magnitude);
    } else {
        output = static_cast<std::int64_t>(magnitude);
    }
    return true;
}

bool normalizeLooping(const std::int64_t raw, const std::int64_t length,
                      NormalizedPosition& output) noexcept {
    if (length <= 0)
        return false;
    auto quotient = raw / length;
    auto remainder = raw % length;
    if (remainder < 0) {
        remainder += length;
        --quotient;
    }
    if (quotient < std::numeric_limits<std::int16_t>::lowest() ||
        quotient > std::numeric_limits<std::int16_t>::max())
        return false;
    output = {remainder, static_cast<std::int16_t>(quotient)};
    return true;
}

juce::Result frameAtQ16(const TempoMap& tempoMap, const std::int64_t q16,
                        const std::uint32_t sampleRate, std::int64_t& output) {
    if (q16 < 0)
        return juce::Result::fail("Scheduler frame position must not be negative");
    return tempoMap.absoluteFrameAt(musicalTimeFromQ16(q16), sampleRate, output);
}

bool commandBefore(const AudioCommand& left, const AudioCommand& right) noexcept {
    if (left.frameOffset != right.frameOffset)
        return left.frameOffset < right.frameOffset;
    const auto leftRank = left.type == AudioCommandType::releaseSource ? 0U : 1U;
    const auto rightRank = right.type == AudioCommandType::releaseSource ? 0U : 1U;
    if (leftRank != rightRank)
        return leftRank < rightRank;
    if (left.sequenceOrder != right.sequenceOrder)
        return left.sequenceOrder < right.sequenceOrder;
    return left.generation < right.generation;
}

void siftDown(std::array<AudioCommand, schedulerCommandCapacity>& commands, std::size_t root,
              const std::size_t count) noexcept {
    if (count < 2U)
        return;
    while (root <= (count - 2U) / 2U) {
        auto child = root * 2U + 1U;
        if (child + 1U < count && commandBefore(commands[child], commands[child + 1U]))
            ++child;
        if (!commandBefore(commands[root], commands[child]))
            return;
        std::swap(commands[root], commands[child]);
        root = child;
    }
}

void heapSort(ScheduledCommandBuffer& buffer) noexcept {
    if (buffer.size < 2U)
        return;
    for (auto root = buffer.size / 2U; root > 0U; --root)
        siftDown(buffer.commands, root - 1U, buffer.size);
    for (auto count = buffer.size; count > 1U; --count) {
        std::swap(buffer.commands[0U], buffer.commands[count - 1U]);
        siftDown(buffer.commands, 0U, count - 1U);
    }
}

bool parentLoopForOutput(const std::uint64_t outputLoop, const std::int16_t loopDelta,
                         std::uint64_t& parentLoop) noexcept {
    if (loopDelta >= 0) {
        const auto delta = static_cast<std::uint64_t>(loopDelta);
        if (outputLoop < delta)
            return false;
        parentLoop = outputLoop - delta;
        return true;
    }
    const auto delta = static_cast<std::uint64_t>(-static_cast<std::int32_t>(loopDelta));
    if (outputLoop > std::numeric_limits<std::uint64_t>::max() - delta)
        return false;
    parentLoop = outputLoop + delta;
    return true;
}

std::uint64_t sourceId(const SchedulerPulse& pulse, const std::uint64_t parentLoop) noexcept {
    constexpr std::uint64_t tokensPerLoop =
        static_cast<std::uint64_t>(maximumPatternEventCount) * 16U + 1U;
    return parentLoop * tokensPerLoop + static_cast<std::uint64_t>(pulse.sourceToken);
}

void appendCommand(ScheduledCommandBuffer& output, const AudioCommand& command) noexcept {
    if (output.size < output.commands.size())
        output.commands[output.size++] = command;
    else
        ++output.dropped;
}

void schedulePulseCommand(const SchedulerPulse& pulse, const bool release, const bool looping,
                          const std::int64_t segmentStart, const std::int64_t segmentEnd,
                          const std::uint64_t outputLoop, const std::size_t blockOffset,
                          ScheduledCommandBuffer& output) noexcept {
    const auto frame =
        looping ? (release ? pulse.loopingReleaseFrame : pulse.loopingTriggerFrame)
                : (release ? pulse.nonLoopingReleaseFrame : pulse.nonLoopingTriggerFrame);
    if (frame < segmentStart || frame >= segmentEnd)
        return;

    std::uint64_t parentLoop = 0U;
    if (looping) {
        const auto delta = release ? pulse.releaseLoopDelta : pulse.triggerLoopDelta;
        if (!parentLoopForOutput(outputLoop, delta, parentLoop))
            return;
    }
    const ProbabilityContext context{pulse.probabilityIdentity, parentLoop};
    if (!probabilityAccepts(pulse.probability, context))
        return;

    const auto relative = static_cast<std::uint64_t>(frame - segmentStart);
    const auto offset = static_cast<std::uint64_t>(blockOffset) + relative;
    if (offset > std::numeric_limits<std::uint32_t>::max()) {
        ++output.dropped;
        return;
    }
    appendCommand(output, {release ? AudioCommandType::releaseSource : AudioCommandType::triggerPad,
                           pulse.padIndex, sourceId(pulse, parentLoop), pulse.velocity,
                           static_cast<std::uint32_t>(offset),
                           pulse.sequenceOrder * 2U + (release ? 1U : 0U)});
}
} // namespace

void ScheduledCommandBuffer::clear() noexcept {
    size = 0U;
    dropped = 0U;
}

std::span<const AudioCommand> ScheduledCommandBuffer::view() const noexcept {
    return {commands.data(), size};
}

juce::Result makeSchedulerSnapshot(const Pattern& pattern, const TempoMap& tempoMap,
                                   const juce::String& projectProbabilitySeed,
                                   const std::uint32_t sampleRate, const std::uint64_t generation,
                                   SchedulerSnapshot& output) {
    if (const auto result = validatePattern(pattern); result.failed())
        return result;
    auto orderedPattern = pattern;
    sortPatternEvents(orderedPattern);

    std::int64_t patternLengthQ16 = 0;
    if (const auto result = musicalDurationToQ16(pattern.length, patternLengthQ16); result.failed())
        return result;
    std::int64_t patternLengthFrames = 0;
    if (const auto result = frameAtQ16(tempoMap, patternLengthQ16, sampleRate, patternLengthFrames);
        result.failed())
        return result;
    if (patternLengthFrames <= 0)
        return juce::Result::fail("Scheduler pattern length must resolve to positive frames");

    SchedulerSnapshot candidate;
    candidate.generation = generation;
    candidate.patternLengthFrames = patternLengthFrames;
    std::size_t pulseCapacity = 0U;
    for (const auto& event : orderedPattern.events)
        pulseCapacity += event.ratchetCount;
    candidate.pulses.reserve(pulseCapacity);

    const auto stepQ16 = resolutionTicks(orderedPattern.stepResolution) *
                         static_cast<std::int64_t>(subTickUnitsPerTick);
    for (std::size_t eventIndex = 0U; eventIndex < orderedPattern.events.size(); ++eventIndex) {
        const auto& event = orderedPattern.events[eventIndex];
        ProbabilityIdentity identity;
        if (const auto result = makeProbabilityIdentity(projectProbabilitySeed, orderedPattern.uuid,
                                                        event.uuid, identity);
            result.failed())
            return result;

        std::int64_t startQ16 = 0;
        std::int64_t durationQ16 = 0;
        std::int64_t spacingQ16 = 0;
        if (const auto result = musicalTimeToQ16(event.start, startQ16); result.failed())
            return result;
        if (const auto result = musicalDurationToQ16(event.duration, durationQ16); result.failed())
            return result;
        if (const auto result = musicalDurationToQ16(event.ratchetSpacing, spacingQ16);
            result.failed())
            return result;

        auto transformedQ16 = startQ16;
        const auto subdivision = startQ16 / stepQ16;
        if ((subdivision & 1) != 0 && orderedPattern.swingPercent > 0U) {
            const auto delay =
                stepQ16 * static_cast<std::int64_t>(orderedPattern.swingPercent) / 100;
            const auto nextSubdivision = (subdivision + 1) * stepQ16;
            transformedQ16 = std::min(startQ16 + delay, nextSubdivision - 1);
        }
        if (!checkedAdd(transformedQ16, static_cast<std::int64_t>(event.microOffset.rawValue),
                        transformedQ16))
            return juce::Result::fail("Scheduler microtiming overflowed");

        const auto ratchetDivisor = static_cast<std::int64_t>(event.ratchetCount);
        const auto subGateQ16 =
            event.ratchetCount == 1U
                ? durationQ16
                : std::max(std::int64_t{1}, std::min(spacingQ16, durationQ16 / ratchetDivisor));
        for (std::uint8_t ratchetIndex = 0U; ratchetIndex < event.ratchetCount; ++ratchetIndex) {
            std::int64_t ratchetOffset = 0;
            if (!checkedMultiply(spacingQ16, static_cast<std::int64_t>(ratchetIndex),
                                 ratchetOffset))
                return juce::Result::fail("Scheduler ratchet offset overflowed");
            std::int64_t rawTrigger = 0;
            std::int64_t rawRelease = 0;
            if (!checkedAdd(transformedQ16, ratchetOffset, rawTrigger) ||
                !checkedAdd(rawTrigger, subGateQ16, rawRelease))
                return juce::Result::fail("Scheduler ratchet timing overflowed");

            NormalizedPosition loopingTrigger;
            NormalizedPosition loopingRelease;
            if (!normalizeLooping(rawTrigger, patternLengthQ16, loopingTrigger) ||
                !normalizeLooping(rawRelease, patternLengthQ16, loopingRelease))
                return juce::Result::fail("Scheduler loop normalization exceeded limits");

            SchedulerPulse pulse;
            pulse.probabilityIdentity = identity;
            pulse.probability = event.probability;
            pulse.triggerLoopDelta = loopingTrigger.loopDelta;
            pulse.releaseLoopDelta = loopingRelease.loopDelta;
            pulse.padIndex = event.stablePadOrder;
            pulse.velocity = static_cast<float>(event.velocity);
            pulse.sourceToken = static_cast<std::uint32_t>(eventIndex * 16U + ratchetIndex + 1U);
            pulse.sequenceOrder = static_cast<std::uint32_t>(eventIndex * 16U + ratchetIndex);
            if (const auto result = frameAtQ16(tempoMap, loopingTrigger.value, sampleRate,
                                               pulse.loopingTriggerFrame);
                result.failed())
                return result;
            if (const auto result = frameAtQ16(tempoMap, loopingRelease.value, sampleRate,
                                               pulse.loopingReleaseFrame);
                result.failed())
                return result;

            if (rawTrigger < 0) {
                if (ratchetIndex == 0U)
                    pulse.nonLoopingTriggerFrame = 0;
            } else if (rawTrigger < patternLengthQ16) {
                if (const auto result =
                        frameAtQ16(tempoMap, rawTrigger, sampleRate, pulse.nonLoopingTriggerFrame);
                    result.failed())
                    return result;
            }
            if (pulse.nonLoopingTriggerFrame >= 0) {
                const auto nonLoopingReleaseQ16 =
                    std::min(patternLengthQ16, std::max(std::int64_t{0}, rawTrigger) + subGateQ16);
                if (const auto result = frameAtQ16(tempoMap, nonLoopingReleaseQ16, sampleRate,
                                                   pulse.nonLoopingReleaseFrame);
                    result.failed())
                    return result;
            }
            candidate.pulses.push_back(pulse);
        }
    }
    output = std::move(candidate);
    return juce::Result::ok();
}

void PatternScheduler::publishSnapshot(const SchedulerSnapshot* const snapshot) noexcept {
    snapshot_.store(snapshot, std::memory_order_release);
}

void PatternScheduler::processBlock(const TransportPositionSnapshot& transport,
                                    const std::size_t frameCount,
                                    ScheduledCommandBuffer& output) noexcept {
    output.clear();
    const auto* current = snapshot_.load(std::memory_order_acquire);
    if (current == nullptr || current->patternLengthFrames <= 0 || frameCount == 0U ||
        transport.configurationGeneration != current->generation ||
        (transport.state != TransportState::playing &&
         transport.state != TransportState::recording)) {
        acknowledgedGeneration_.store(current != nullptr ? current->generation : 0U,
                                      std::memory_order_release);
        return;
    }

    auto position =
        std::clamp(transport.framePosition, std::int64_t{0}, current->patternLengthFrames);
    auto outputLoop = transport.loopIteration;
    auto remaining = frameCount;
    std::size_t blockOffset = 0U;
    while (remaining > 0U && position < current->patternLengthFrames) {
        const auto available = static_cast<std::uint64_t>(current->patternLengthFrames - position);
        const auto segmentFrames =
            std::min<std::uint64_t>(available, static_cast<std::uint64_t>(remaining));
        const auto segmentEnd = position + static_cast<std::int64_t>(segmentFrames);
        for (const auto& pulse : current->pulses) {
            schedulePulseCommand(pulse, false, transport.loopEnabled, position, segmentEnd,
                                 outputLoop, blockOffset, output);
            schedulePulseCommand(pulse, true, transport.loopEnabled, position, segmentEnd,
                                 outputLoop, blockOffset, output);
        }
        remaining -= static_cast<std::size_t>(segmentFrames);
        blockOffset += static_cast<std::size_t>(segmentFrames);
        if (!transport.loopEnabled || remaining == 0U)
            break;
        position = 0;
        ++outputLoop;
    }
    heapSort(output);
    if (output.dropped > 0U)
        overflows_.fetch_add(output.dropped, std::memory_order_relaxed);
    acknowledgedGeneration_.store(current->generation, std::memory_order_release);
}

std::uint64_t PatternScheduler::overflowCount() const noexcept {
    return overflows_.load(std::memory_order_acquire);
}

std::uint64_t PatternScheduler::acknowledgedSnapshotGeneration() const noexcept {
    return acknowledgedGeneration_.load(std::memory_order_acquire);
}
} // namespace padflow
