#pragma once

#include "Model/MusicalTime.h"

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace padflow {
inline constexpr std::uint64_t probabilityQ32Maximum = std::uint64_t{1} << 32U;
inline constexpr std::size_t maximumPatternCount = 128U;
inline constexpr std::size_t maximumPatternEventCount = 8192U;
inline constexpr std::uint16_t maximumPatternLaneCount = 64U;

using ProbabilityQ32 = std::uint64_t;

enum class StepResolution : std::uint16_t {
    quarter = 960U,
    eighth = 480U,
    eighthTriplet = 320U,
    sixteenth = 240U,
    sixteenthTriplet = 160U,
    thirtySecond = 120U
};

enum class RecordQuantization : std::uint16_t {
    off = 0U,
    quarter = 960U,
    eighth = 480U,
    eighthTriplet = 320U,
    sixteenth = 240U,
    sixteenthTriplet = 160U,
    thirtySecond = 120U
};

struct SequenceEvent final {
    juce::String uuid;
    juce::String padUuid;
    MusicalTime start;
    MusicalDuration duration{240, 0U};
    std::uint8_t velocity{100U};
    ProbabilityQ32 probability{probabilityQ32Maximum};
    std::uint8_t ratchetCount{1U};
    MusicalDuration ratchetSpacing{240, 0U};
    MicroOffsetQ16 microOffset;
    std::uint16_t stablePadOrder{0U};

    [[nodiscard]] friend bool operator==(const SequenceEvent&, const SequenceEvent&) = default;
};

struct Pattern final {
    juce::String uuid;
    juce::String name{"Pattern 1"};
    TimeSignature timeSignature;
    MusicalDuration length{4 * ppqTicksPerQuarterNote, 0U};
    StepResolution stepResolution{StepResolution::sixteenth};
    std::uint8_t swingPercent{0U};
    std::uint8_t quantizeStrengthPercent{100U};
    RecordQuantization recordQuantization{RecordQuantization::sixteenth};
    std::vector<SequenceEvent> events;
    std::uint64_t creationRevision{0U};
    std::uint64_t modificationRevision{0U};

    [[nodiscard]] friend bool operator==(const Pattern&, const Pattern&) = default;
};

struct PatternCollection final {
    std::vector<Pattern> patterns;
    juce::String selectedPatternUuid;

    [[nodiscard]] friend bool operator==(const PatternCollection&,
                                         const PatternCollection&) = default;
};

[[nodiscard]] std::int64_t resolutionTicks(StepResolution resolution) noexcept;
[[nodiscard]] std::int64_t quantizationTicks(RecordQuantization quantization) noexcept;
[[nodiscard]] bool isSupportedStepResolution(StepResolution resolution) noexcept;
[[nodiscard]] bool isSupportedRecordQuantization(RecordQuantization quantization) noexcept;
[[nodiscard]] Pattern makeDefaultPattern(juce::String uuid, juce::String name = "Pattern 1",
                                         std::uint64_t revision = 0U);
[[nodiscard]] juce::Result validateSequenceEvent(const SequenceEvent& event,
                                                 const Pattern& pattern) noexcept;
[[nodiscard]] juce::Result validatePattern(const Pattern& pattern) noexcept;
void sortPatternEvents(Pattern& pattern);
[[nodiscard]] juce::Result addSequenceEvent(Pattern& pattern, SequenceEvent event,
                                            std::uint64_t revision);
[[nodiscard]] juce::Result duplicateSequenceEvent(Pattern& pattern, const juce::String& sourceUuid,
                                                  juce::String duplicateUuid,
                                                  std::uint64_t revision);
[[nodiscard]] juce::Result addPattern(PatternCollection& collection, Pattern pattern);
[[nodiscard]] juce::Result duplicatePattern(PatternCollection& collection,
                                            const juce::String& sourceUuid,
                                            juce::String duplicatePatternUuid,
                                            const std::vector<juce::String>& duplicateEventUuids,
                                            std::uint64_t revision);
[[nodiscard]] juce::Result deletePattern(PatternCollection& collection, const juce::String& uuid);
[[nodiscard]] Pattern* findPattern(PatternCollection& collection,
                                   const juce::String& uuid) noexcept;
[[nodiscard]] const Pattern* findPattern(const PatternCollection& collection,
                                         const juce::String& uuid) noexcept;
} // namespace padflow
