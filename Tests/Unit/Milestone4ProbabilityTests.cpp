#include "Sequencing/Probability.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <limits>

namespace padflow {
namespace {
constexpr auto seedUuid = "00010203-0405-0607-0809-0a0b0c0d0e0f";
constexpr auto patternUuid = "11111111-2222-4333-8444-555555555555";
constexpr auto eventUuid = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
constexpr auto duplicateEventUuid = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeef";

ProbabilityContext makeContext(const char* const event = eventUuid,
                               const std::uint64_t iteration = 0U) {
    ProbabilityContext context;
    const auto result = makeProbabilityIdentity(seedUuid, patternUuid, event, context.identity);
    juce::ignoreUnused(result);
    context.patternLoopIteration = iteration;
    return context;
}

class Milestone4ProbabilityTests final : public juce::UnitTest {
  public:
    Milestone4ProbabilityTests()
        : juce::UnitTest("Milestone 4 stateless probability", "sequencing") {}

    void runTest() override {
        beginTest("siphash24-v1 matches the canonical empty-message vector");
        std::array<std::uint8_t, 16U> key{};
        for (std::size_t index = 0U; index < key.size(); ++index)
            key[index] = static_cast<std::uint8_t>(index);
        expectEquals(sipHash24(key, {}), std::uint64_t{0x726fdb47dd0e0e31U});

        beginTest("Probability identities parse canonical UUID bytes");
        ProbabilityIdentity identity;
        expect(makeProbabilityIdentity(seedUuid, patternUuid, eventUuid, identity).wasOk());
        expectEquals(identity.projectSeed.front(), std::uint8_t{0U});
        expectEquals(identity.projectSeed.back(), std::uint8_t{15U});
        expect(parseCanonicalUuid("not-a-uuid", identity.eventUuid).failed());

        const auto context = makeContext();
        beginTest("SEQ-M4-080 and SEQ-M4-081 preserve probability endpoints");
        expect(!probabilityAccepts(0U, context));
        expect(probabilityAccepts(probabilityQ32Maximum, context));

        beginTest("SEQ-M4-082 repeats siphash24-v1 decisions exactly");
        const auto first = probabilityDrawQ32(context);
        for (int repetition = 0; repetition < 32; ++repetition)
            expectEquals(probabilityDrawQ32(context), first);

        beginTest("SEQ-M4-083 through SEQ-M4-085 exclude runtime traversal inputs");
        const auto buffer64 = probabilityDrawQ32(context);
        const auto buffer1024 = probabilityDrawQ32(context);
        const auto sample44100 = probabilityDrawQ32(context);
        const auto sample96000 = probabilityDrawQ32(context);
        const auto reordered = probabilityDrawQ32(makeContext());
        expectEquals(buffer64, buffer1024);
        expectEquals(sample44100, sample96000);
        expectEquals(reordered, first);

        beginTest("SEQ-M4-086 loop iteration participates in the fixed tuple");
        const auto nextLoop = probabilityDrawQ32(makeContext(eventUuid, 1U));
        expect(first != nextLoop);

        beginTest("SEQ-M4-087 duplicated event UUID changes the probability sequence");
        const auto duplicate = probabilityDrawQ32(makeContext(duplicateEventUuid));
        expect(first != duplicate);

        beginTest("SEQ-M4-088 real-time and offline decisions use the same pure function");
        const auto threshold = first == std::numeric_limits<std::uint32_t>::max()
                                   ? probabilityQ32Maximum
                                   : static_cast<ProbabilityQ32>(first) + 1U;
        const auto realtimeDecision = probabilityAccepts(threshold, context);
        const auto offlineDecision = probabilityAccepts(threshold, context);
        expect(realtimeDecision == offlineDecision);
        expect(realtimeDecision);
    }
};

static Milestone4ProbabilityTests milestone4ProbabilityTests;
} // namespace
} // namespace padflow
