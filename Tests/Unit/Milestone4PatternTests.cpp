#include "Sequencing/PatternModel.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <vector>

namespace padflow {
namespace {
constexpr auto patternOneUuid = "11111111-1111-4111-8111-111111111111";
constexpr auto patternTwoUuid = "22222222-2222-4222-8222-222222222222";
constexpr auto eventOneUuid = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto eventTwoUuid = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
constexpr auto padOneUuid = "01010101-0101-4101-8101-010101010101";

SequenceEvent makeEvent(const char* const uuid) {
    SequenceEvent event;
    event.uuid = uuid;
    event.padUuid = padOneUuid;
    return event;
}

class Milestone4PatternTests final : public juce::UnitTest {
  public:
    Milestone4PatternTests() : juce::UnitTest("Milestone 4 pattern model", "sequencing") {}

    void runTest() override {
        beginTest("SEQ-M4-040 creates stable deterministic pattern defaults");
        auto pattern = makeDefaultPattern(patternOneUuid, "Beat", 4U);
        expect(validatePattern(pattern).wasOk());
        expectEquals(pattern.uuid, juce::String{patternOneUuid});
        expectEquals(pattern.length.wholePpqTicks, std::int64_t{3840});
        expect(pattern.timeSignature == TimeSignature{4U, 4U});
        expect(pattern.stepResolution == StepResolution::sixteenth);
        expectEquals(pattern.creationRevision, std::uint64_t{4U});

        beginTest("SEQ-M4-041 duplicates content with new pattern and event UUIDs");
        expect(addSequenceEvent(pattern, makeEvent(eventOneUuid), 5U).wasOk());
        PatternCollection collection;
        expect(addPattern(collection, pattern).wasOk());
        expect(duplicatePattern(collection, patternOneUuid, patternTwoUuid,
                                std::vector<juce::String>{eventTwoUuid}, 6U)
                   .wasOk());
        expectEquals(collection.patterns.size(), std::size_t{2U});
        const auto* duplicate = findPattern(collection, patternTwoUuid);
        expect(duplicate != nullptr);
        if (duplicate != nullptr) {
            expectEquals(duplicate->events.size(), std::size_t{1U});
            expectEquals(duplicate->events.front().uuid, juce::String{eventTwoUuid});
            expect(duplicate->events.front().padUuid == pattern.events.front().padUuid);
            expect(duplicate->events.front().start == pattern.events.front().start);
            expectEquals(duplicate->creationRevision, std::uint64_t{6U});
        }

        beginTest("SEQ-M4-042 deletion retains a valid selected pattern");
        expect(deletePattern(collection, patternTwoUuid).wasOk());
        expectEquals(collection.patterns.size(), std::size_t{1U});
        expectEquals(collection.selectedPatternUuid, juce::String{patternOneUuid});
        expect(deletePattern(collection, patternOneUuid).failed());

        beginTest("SEQ-M4-043 enforces one-step minimum and 128-bar maximum");
        for (const auto resolution :
             {StepResolution::quarter, StepResolution::eighth, StepResolution::eighthTriplet,
              StepResolution::sixteenth, StepResolution::sixteenthTriplet,
              StepResolution::thirtySecond}) {
            auto boundary = makeDefaultPattern(patternOneUuid);
            boundary.stepResolution = resolution;
            boundary.length = {resolutionTicks(resolution), 0U};
            expect(validatePattern(boundary).wasOk());
            boundary.length = {resolutionTicks(resolution) - 1, 0U};
            expect(validatePattern(boundary).failed());
            boundary.length = {ticksPerBar(boundary.timeSignature) * 128, 0U};
            expect(validatePattern(boundary).wasOk());
            boundary.length.wholePpqTicks += 1;
            expect(validatePattern(boundary).failed());
        }

        beginTest("SEQ-M4-044 accepts every approved pattern metre");
        constexpr std::array<TimeSignature, 9U> signatures{{
            {2U, 4U},
            {3U, 4U},
            {4U, 4U},
            {5U, 4U},
            {6U, 4U},
            {7U, 4U},
            {6U, 8U},
            {9U, 8U},
            {12U, 8U},
        }};
        for (const auto signature : signatures) {
            auto candidate = makeDefaultPattern(patternOneUuid);
            candidate.timeSignature = signature;
            candidate.length = {ticksPerBar(signature), 0U};
            expect(validatePattern(candidate).wasOk());
        }
        auto unsupported = makeDefaultPattern(patternOneUuid);
        unsupported.timeSignature = {4U, 8U};
        expect(validatePattern(unsupported).failed());

        beginTest("SEQ-M4-045 orders by start, stable pad order, then UUID");
        auto ordering = makeDefaultPattern(patternOneUuid);
        auto later = makeEvent("cccccccc-cccc-4ccc-8ccc-cccccccccccc");
        later.start = {240, 0U};
        auto secondLane = makeEvent(eventTwoUuid);
        secondLane.stablePadOrder = 2U;
        auto firstLaneSecondUuid = makeEvent(eventOneUuid);
        firstLaneSecondUuid.stablePadOrder = 1U;
        auto firstLaneFirstUuid = makeEvent("00000000-0000-4000-8000-000000000001");
        firstLaneFirstUuid.stablePadOrder = 1U;
        ordering.events = {later, secondLane, firstLaneSecondUuid, firstLaneFirstUuid};
        sortPatternEvents(ordering);
        expectEquals(ordering.events[0U].uuid, firstLaneFirstUuid.uuid);
        expectEquals(ordering.events[1U].uuid, firstLaneSecondUuid.uuid);
        expectEquals(ordering.events[2U].uuid, secondLane.uuid);
        expectEquals(ordering.events[3U].uuid, later.uuid);

        beginTest("SEQ-M4-046 event duplication always requires a new UUID");
        auto eventDuplication = makeDefaultPattern(patternOneUuid);
        expect(addSequenceEvent(eventDuplication, makeEvent(eventOneUuid), 1U).wasOk());
        expect(duplicateSequenceEvent(eventDuplication, eventOneUuid, eventOneUuid, 2U).failed());
        expect(duplicateSequenceEvent(eventDuplication, eventOneUuid, eventTwoUuid, 2U).wasOk());
        expectEquals(eventDuplication.events.size(), std::size_t{2U});

        beginTest("SEQ-M4-047 rejects non-positive or overlong durations");
        auto invalidEvent = makeEvent(eventOneUuid);
        invalidEvent.duration = {};
        expect(validateSequenceEvent(invalidEvent, pattern).failed());
        invalidEvent.duration = {pattern.length.wholePpqTicks + 1, 0U};
        expect(validateSequenceEvent(invalidEvent, pattern).failed());

        beginTest("SEQ-M4-048 accepts velocity 1 through 127 only");
        invalidEvent = makeEvent(eventOneUuid);
        invalidEvent.velocity = 1U;
        expect(validateSequenceEvent(invalidEvent, pattern).wasOk());
        invalidEvent.velocity = 127U;
        expect(validateSequenceEvent(invalidEvent, pattern).wasOk());
        invalidEvent.velocity = 0U;
        expect(validateSequenceEvent(invalidEvent, pattern).failed());
        invalidEvent.velocity = 128U;
        expect(validateSequenceEvent(invalidEvent, pattern).failed());
    }
};

static Milestone4PatternTests milestone4PatternTests;
} // namespace
} // namespace padflow
