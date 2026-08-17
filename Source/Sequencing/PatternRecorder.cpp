#include "PatternRecorder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace padflow {
juce::Result PatternRecorder::beginTake(const ProjectState& project, const PatternRecordMode mode,
                                        const std::uint32_t sampleRate) {
    if (active_.load(std::memory_order_acquire))
        return juce::Result::fail("A pattern take is already active");
    const auto* pattern =
        findPattern(project.sequencer.patterns, project.sequencer.patterns.selectedPatternUuid);
    if (pattern == nullptr || sampleRate == 0U)
        return juce::Result::fail("Pattern recording configuration is invalid");
    TempoMap tempoMap;
    if (const auto result = tempoMap.replacePoints(project.sequencer.tempoPoints); result.failed())
        return result;
    if (project.sequencer.tempoPoints.size() != 1U)
        return juce::Result::fail("Live recording currently requires a constant project tempo");
    const auto length =
        MusicalTime{pattern->length.wholePpqTicks, pattern->length.fractionalTickQ16};
    if (const auto result = tempoMap.absoluteFrameAt(length, sampleRate, patternLengthFrames_);
        result.failed())
        return result;
    if (patternLengthFrames_ <= 0)
        return juce::Result::fail("Pattern length resolves to no audio frames");
    basePattern_ = *pattern;
    for (std::size_t index = 0; index < totalPadCount; ++index)
        padUuids_[index] = project.banks[index / padsPerBank].pads[index % padsPerBank].uuid;
    captured_.clear();
    mode_ = mode;
    microBpm_ = project.sequencer.tempoPoints.front().microBpm;
    sampleRate_ = sampleRate;
    PatternRecordInput stale;
    while (queue_.tryPop(stale)) {
    }
    active_.store(true, std::memory_order_release);
    return juce::Result::ok();
}

bool PatternRecorder::capture(PatternRecordInput event,
                              const TransportState transportState) noexcept {
    if (!active_.load(std::memory_order_acquire) || transportState != TransportState::recording ||
        event.padIndex >= totalPadCount)
        return false;
    if (!queue_.tryPush(event)) {
        overflows_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    return true;
}

std::size_t PatternRecorder::drain() {
    std::size_t count = 0U;
    PatternRecordInput event;
    while (queue_.tryPop(event)) {
        captured_.push_back(event);
        ++count;
    }
    return count;
}

std::int64_t PatternRecorder::absoluteInputFrame(const PatternRecordInput& input) const noexcept {
    const auto maximumLoop =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / patternLengthFrames_);
    if (input.loopIteration > maximumLoop)
        return std::numeric_limits<std::int64_t>::max();
    return static_cast<std::int64_t>(input.loopIteration) * patternLengthFrames_ +
           input.framePosition;
}

std::int64_t PatternRecorder::frameToQ16(const std::int64_t frame) const noexcept {
    const auto value = static_cast<long double>(std::max<std::int64_t>(0, frame)) *
                       static_cast<long double>(microBpm_) *
                       static_cast<long double>(ppqTicksPerQuarterNote) *
                       static_cast<long double>(subTickUnitsPerTick) /
                       (60.0L * 1'000'000.0L * static_cast<long double>(sampleRate_));
    return value >= static_cast<long double>(std::numeric_limits<std::int64_t>::max())
               ? std::numeric_limits<std::int64_t>::max()
               : static_cast<std::int64_t>(std::llround(value));
}

std::int64_t PatternRecorder::quantizeQ16(const std::int64_t value) const noexcept {
    const auto gridTicks = quantizationTicks(basePattern_.recordQuantization);
    if (gridTicks <= 0 || basePattern_.quantizeStrengthPercent == 0U)
        return value;
    const auto grid = gridTicks * static_cast<std::int64_t>(subTickUnitsPerTick);
    const auto rounded = static_cast<std::int64_t>(std::llround(static_cast<long double>(value) /
                                                                static_cast<long double>(grid))) *
                         grid;
    const auto delta = rounded - value;
    return value + delta * basePattern_.quantizeStrengthPercent / 100;
}

juce::Result PatternRecorder::finishTake(const std::int64_t stopFrame, Pattern& output) {
    if (!active_.exchange(false, std::memory_order_acq_rel))
        return juce::Result::fail("No pattern take is active");
    juce::ignoreUnused(drain());
    struct Held final {
        PatternRecordInput input;
        bool active{false};
    };
    std::unordered_map<std::uint64_t, Held> held;
    std::vector<SequenceEvent> take;
    std::unordered_set<std::uint16_t> recordedLanes;
    std::int64_t rangeStart = std::numeric_limits<std::int64_t>::max();
    std::int64_t rangeEnd = 0;
    const auto append = [&](const PatternRecordInput& down, const PatternRecordInput& up,
                            std::vector<SequenceEvent>& events) {
        const auto absoluteStart = absoluteInputFrame(down);
        const auto absoluteEnd = std::max(absoluteStart + 1, absoluteInputFrame(up));
        auto startQ16 = quantizeQ16(frameToQ16(absoluteStart));
        auto endQ16 = quantizeQ16(frameToQ16(absoluteEnd));
        if (endQ16 <= startQ16)
            endQ16 = startQ16 + static_cast<std::int64_t>(subTickUnitsPerTick);
        std::int64_t lengthQ16 = 0;
        juce::ignoreUnused(musicalDurationToQ16(basePattern_.length, lengthQ16));
        startQ16 %= lengthQ16;
        SequenceEvent event;
        event.uuid = juce::Uuid{}.toString();
        event.padUuid = padUuids_[down.padIndex];
        event.start = musicalTimeFromQ16(startQ16);
        event.duration = musicalDurationFromQ16(std::min(endQ16 - startQ16, lengthQ16));
        event.ratchetSpacing = event.duration;
        event.velocity = down.velocity;
        event.stablePadOrder = static_cast<std::uint16_t>(down.padIndex);
        events.push_back(std::move(event));
    };
    for (const auto& input : captured_) {
        const auto absolute = absoluteInputFrame(input);
        rangeStart = std::min(rangeStart, absolute);
        rangeEnd = std::max(rangeEnd, absolute);
        if (input.type == PatternRecordInputType::noteOn) {
            held[input.sourceId] = {input, true};
            recordedLanes.insert(static_cast<std::uint16_t>(input.padIndex));
        } else {
            const auto found = held.find(input.sourceId);
            if (found != held.end() && found->second.active) {
                append(found->second.input, input, take);
                found->second.active = false;
            }
        }
    }
    for (const auto& [source, value] : held) {
        juce::ignoreUnused(source);
        if (value.active) {
            auto release = value.input;
            release.type = PatternRecordInputType::noteOff;
            release.framePosition = std::max(stopFrame, value.input.framePosition + 1);
            append(value.input, release, take);
        }
    }
    if (take.empty())
        return juce::Result::fail("Pattern take contains no completed notes");

    output = basePattern_;
    if (mode_ == PatternRecordMode::replace &&
        rangeStart != std::numeric_limits<std::int64_t>::max()) {
        const auto startQ16 = frameToQ16(rangeStart);
        const auto endQ16 = frameToQ16(std::max(rangeEnd, stopFrame));
        output.events.erase(
            std::remove_if(output.events.begin(), output.events.end(),
                           [&](const auto& event) {
                               std::int64_t eventQ16 = 0;
                               juce::ignoreUnused(musicalTimeToQ16(event.start, eventQ16));
                               return recordedLanes.contains(event.stablePadOrder) &&
                                      eventQ16 >= startQ16 && eventQ16 <= endQ16;
                           }),
            output.events.end());
    }
    output.events.insert(output.events.end(), take.begin(), take.end());
    sortPatternEvents(output);
    ++output.modificationRevision;
    return validatePattern(output);
}

void PatternRecorder::cancel() noexcept {
    active_.store(false, std::memory_order_release);
    PatternRecordInput input;
    while (queue_.tryPop(input)) {
    }
    captured_.clear();
}

bool PatternRecorder::isActive() const noexcept {
    return active_.load(std::memory_order_acquire);
}

std::uint64_t PatternRecorder::overflowCount() const noexcept {
    return overflows_.load(std::memory_order_acquire);
}

juce::Result PatternRecorder::stepRecord(Pattern& pattern, const juce::String& padUuid,
                                         const std::uint16_t stablePadOrder,
                                         const std::uint8_t velocity, std::int64_t& cursorTicks,
                                         const std::uint64_t revision) {
    const auto step = resolutionTicks(pattern.stepResolution);
    if (cursorTicks < 0 || step <= 0)
        return juce::Result::fail("Step-record cursor is invalid");
    SequenceEvent event;
    event.uuid = juce::Uuid{}.toString();
    event.padUuid = padUuid;
    event.start = {cursorTicks, 0U};
    event.duration = {step, 0U};
    event.ratchetSpacing = event.duration;
    event.velocity = velocity;
    event.stablePadOrder = stablePadOrder;
    if (const auto result = addSequenceEvent(pattern, std::move(event), revision); result.failed())
        return result;
    const auto length = pattern.length.wholePpqTicks;
    cursorTicks = length > 0 ? (cursorTicks + step) % length : 0;
    return juce::Result::ok();
}
} // namespace padflow
