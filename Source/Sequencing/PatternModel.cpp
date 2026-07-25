#include "PatternModel.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace padflow {
namespace {
bool hasUsableIdentity(const juce::String& value) noexcept {
    return value.isNotEmpty() && value.length() <= 128;
}

bool eventLess(const SequenceEvent& left, const SequenceEvent& right) noexcept {
    if (left.start != right.start)
        return left.start < right.start;
    if (left.stablePadOrder != right.stablePadOrder)
        return left.stablePadOrder < right.stablePadOrder;
    return left.uuid < right.uuid;
}
} // namespace

std::int64_t resolutionTicks(const StepResolution resolution) noexcept {
    switch (resolution) {
    case StepResolution::quarter:
    case StepResolution::eighth:
    case StepResolution::eighthTriplet:
    case StepResolution::sixteenth:
    case StepResolution::sixteenthTriplet:
    case StepResolution::thirtySecond:
        return static_cast<std::int64_t>(resolution);
    }
    return 0;
}

std::int64_t quantizationTicks(const RecordQuantization quantization) noexcept {
    return quantization == RecordQuantization::off ? 0 : static_cast<std::int64_t>(quantization);
}

bool isSupportedStepResolution(const StepResolution resolution) noexcept {
    return resolutionTicks(resolution) > 0;
}

bool isSupportedRecordQuantization(const RecordQuantization quantization) noexcept {
    switch (quantization) {
    case RecordQuantization::off:
    case RecordQuantization::quarter:
    case RecordQuantization::eighth:
    case RecordQuantization::eighthTriplet:
    case RecordQuantization::sixteenth:
    case RecordQuantization::sixteenthTriplet:
    case RecordQuantization::thirtySecond:
        return true;
    }
    return false;
}

Pattern makeDefaultPattern(juce::String uuid, juce::String name, const std::uint64_t revision) {
    Pattern pattern;
    pattern.uuid = std::move(uuid);
    pattern.name = std::move(name);
    pattern.creationRevision = revision;
    pattern.modificationRevision = revision;
    return pattern;
}

juce::Result validateSequenceEvent(const SequenceEvent& event, const Pattern& pattern) noexcept {
    if (!hasUsableIdentity(event.uuid) || !hasUsableIdentity(event.padUuid))
        return juce::Result::fail("Sequence event identity is invalid");
    if (event.start < MusicalTime{})
        return juce::Result::fail("Sequence event start must not be negative");

    std::int64_t startQ16 = 0;
    std::int64_t lengthQ16 = 0;
    std::int64_t durationQ16 = 0;
    std::int64_t spacingQ16 = 0;
    if (const auto result = musicalTimeToQ16(event.start, startQ16); result.failed())
        return result;
    if (const auto result = musicalDurationToQ16(pattern.length, lengthQ16); result.failed())
        return result;
    if (const auto result = musicalDurationToQ16(event.duration, durationQ16); result.failed())
        return result;
    if (const auto result = musicalDurationToQ16(event.ratchetSpacing, spacingQ16); result.failed())
        return result;
    if (startQ16 >= lengthQ16)
        return juce::Result::fail("Sequence event start must be inside the pattern");
    if (durationQ16 <= 0 || durationQ16 > lengthQ16)
        return juce::Result::fail("Sequence event duration is invalid");
    if (event.velocity < 1U || event.velocity > 127U)
        return juce::Result::fail("Sequence event velocity must be between 1 and 127");
    if (event.probability > probabilityQ32Maximum)
        return juce::Result::fail("Sequence event probability exceeds canonical Q32");
    if (event.ratchetCount < 1U || event.ratchetCount > 16U)
        return juce::Result::fail("Sequence event ratchet count must be between 1 and 16");
    if (spacingQ16 <= 0 || spacingQ16 > durationQ16)
        return juce::Result::fail("Sequence event ratchet spacing is invalid");
    if (event.ratchetCount > 1U &&
        spacingQ16 > std::numeric_limits<std::int64_t>::max() /
                         static_cast<std::int64_t>(event.ratchetCount - 1U))
        return juce::Result::fail("Sequence event ratchet expansion overflows");
    if (event.stablePadOrder >= maximumPatternLaneCount)
        return juce::Result::fail("Sequence event pad order is invalid");
    return juce::Result::ok();
}

juce::Result validatePattern(const Pattern& pattern) noexcept {
    if (!hasUsableIdentity(pattern.uuid))
        return juce::Result::fail("Pattern identity is invalid");
    const auto trimmedName = pattern.name.trim();
    if (trimmedName.isEmpty() || trimmedName.length() > 64)
        return juce::Result::fail("Pattern name is invalid");
    if (!isSupportedTimeSignature(pattern.timeSignature))
        return juce::Result::fail("Pattern time signature is unsupported");
    if (!isSupportedStepResolution(pattern.stepResolution) ||
        !isSupportedRecordQuantization(pattern.recordQuantization))
        return juce::Result::fail("Pattern resolution is unsupported");
    if (pattern.swingPercent > 75U || pattern.quantizeStrengthPercent > 100U)
        return juce::Result::fail("Pattern swing or quantize strength is invalid");
    if (pattern.events.size() > maximumPatternEventCount)
        return juce::Result::fail("Pattern event capacity exceeded");

    std::int64_t lengthQ16 = 0;
    if (const auto result = musicalDurationToQ16(pattern.length, lengthQ16); result.failed())
        return result;
    const auto minimumLengthQ16 =
        resolutionTicks(pattern.stepResolution) * static_cast<std::int64_t>(subTickUnitsPerTick);
    const auto maximumLengthTicks = ticksPerBar(pattern.timeSignature) * 128;
    const auto maximumLengthQ16 =
        maximumLengthTicks * static_cast<std::int64_t>(subTickUnitsPerTick);
    if (lengthQ16 < minimumLengthQ16 || lengthQ16 > maximumLengthQ16)
        return juce::Result::fail("Pattern length must be between one step and 128 bars");

    for (std::size_t index = 0U; index < pattern.events.size(); ++index) {
        if (const auto result = validateSequenceEvent(pattern.events[index], pattern);
            result.failed())
            return result;
        for (std::size_t following = index + 1U; following < pattern.events.size(); ++following)
            if (pattern.events[index].uuid == pattern.events[following].uuid)
                return juce::Result::fail("Pattern event UUIDs must be unique");
    }
    return juce::Result::ok();
}

void sortPatternEvents(Pattern& pattern) {
    std::stable_sort(pattern.events.begin(), pattern.events.end(), eventLess);
}

juce::Result addSequenceEvent(Pattern& pattern, SequenceEvent event, const std::uint64_t revision) {
    if (pattern.events.size() >= maximumPatternEventCount)
        return juce::Result::fail("Pattern event capacity exceeded");
    for (const auto& existing : pattern.events)
        if (existing.uuid == event.uuid)
            return juce::Result::fail("Pattern event UUID already exists");
    if (const auto result = validateSequenceEvent(event, pattern); result.failed())
        return result;
    pattern.events.push_back(std::move(event));
    sortPatternEvents(pattern);
    pattern.modificationRevision = revision;
    return juce::Result::ok();
}

juce::Result duplicateSequenceEvent(Pattern& pattern, const juce::String& sourceUuid,
                                    juce::String duplicateUuid, const std::uint64_t revision) {
    const auto source =
        std::find_if(pattern.events.begin(), pattern.events.end(),
                     [&](const SequenceEvent& event) { return event.uuid == sourceUuid; });
    if (source == pattern.events.end())
        return juce::Result::fail("Source sequence event was not found");
    auto duplicate = *source;
    duplicate.uuid = std::move(duplicateUuid);
    return addSequenceEvent(pattern, std::move(duplicate), revision);
}

juce::Result addPattern(PatternCollection& collection, Pattern pattern) {
    if (collection.patterns.size() >= maximumPatternCount)
        return juce::Result::fail("Pattern capacity exceeded");
    for (const auto& existing : collection.patterns)
        if (existing.uuid == pattern.uuid)
            return juce::Result::fail("Pattern UUID already exists");
    if (const auto result = validatePattern(pattern); result.failed())
        return result;
    collection.patterns.push_back(std::move(pattern));
    if (collection.selectedPatternUuid.isEmpty())
        collection.selectedPatternUuid = collection.patterns.back().uuid;
    return juce::Result::ok();
}

juce::Result duplicatePattern(PatternCollection& collection, const juce::String& sourceUuid,
                              juce::String duplicatePatternUuid,
                              const std::vector<juce::String>& duplicateEventUuids,
                              const std::uint64_t revision) {
    const auto* source = findPattern(collection, sourceUuid);
    if (source == nullptr)
        return juce::Result::fail("Source pattern was not found");
    if (duplicateEventUuids.size() != source->events.size())
        return juce::Result::fail("Duplicate pattern requires one new UUID per event");
    auto duplicate = *source;
    duplicate.uuid = std::move(duplicatePatternUuid);
    duplicate.name = source->name + " Copy";
    duplicate.creationRevision = revision;
    duplicate.modificationRevision = revision;
    for (std::size_t index = 0U; index < duplicate.events.size(); ++index)
        duplicate.events[index].uuid = duplicateEventUuids[index];
    if (const auto result = addPattern(collection, std::move(duplicate)); result.failed())
        return result;
    collection.selectedPatternUuid = collection.patterns.back().uuid;
    return juce::Result::ok();
}

juce::Result deletePattern(PatternCollection& collection, const juce::String& uuid) {
    if (collection.patterns.size() <= 1U)
        return juce::Result::fail("At least one pattern must remain");
    const auto existing =
        std::find_if(collection.patterns.begin(), collection.patterns.end(),
                     [&](const Pattern& pattern) { return pattern.uuid == uuid; });
    if (existing == collection.patterns.end())
        return juce::Result::fail("Pattern was not found");
    const auto deletingSelected = collection.selectedPatternUuid == uuid;
    collection.patterns.erase(existing);
    if (deletingSelected)
        collection.selectedPatternUuid = collection.patterns.front().uuid;
    return juce::Result::ok();
}

Pattern* findPattern(PatternCollection& collection, const juce::String& uuid) noexcept {
    const auto existing =
        std::find_if(collection.patterns.begin(), collection.patterns.end(),
                     [&](const Pattern& pattern) { return pattern.uuid == uuid; });
    return existing == collection.patterns.end() ? nullptr : &*existing;
}

const Pattern* findPattern(const PatternCollection& collection, const juce::String& uuid) noexcept {
    const auto existing =
        std::find_if(collection.patterns.begin(), collection.patterns.end(),
                     [&](const Pattern& pattern) { return pattern.uuid == uuid; });
    return existing == collection.patterns.end() ? nullptr : &*existing;
}
} // namespace padflow
