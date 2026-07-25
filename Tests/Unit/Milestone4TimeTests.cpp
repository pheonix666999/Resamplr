#include "Model/MusicalTime.h"

#include <juce_core/juce_core.h>

#include <array>
#include <limits>
#include <vector>

namespace padflow {
class Milestone4TimeTests final : public juce::UnitTest {
  public:
    Milestone4TimeTests() : juce::UnitTest("Milestone 4 musical time and tempo") {}

    void runTest() override {
        beginTest("SEQ-M4-001 and SEQ-M4-002 canonical PPQ and Q16 timing");
        expectEquals(ppqTicksPerQuarterNote, std::int64_t{960});
        MusicalTime fractional;
        expect(addMicroOffset({10, 0U}, {32'768}, fractional).wasOk());
        expect(fractional == MusicalTime{10, 32'768U});
        expect(addMicroOffset(fractional, {-65'536}, fractional).wasOk());
        expect(fractional == MusicalTime{9, 32'768U});

        beginTest("SEQ-M4-003 through SEQ-M4-006 supported sample-rate conversion");
        constexpr std::array<std::uint32_t, 4U> rates{44'100U, 48'000U, 88'200U, 96'000U};
        for (const auto rate : rates) {
            std::int64_t frame = 0;
            expect(musicalDurationToFrames({960, 0U}, defaultTempoMicroBpm, rate, frame).wasOk());
            expectEquals(frame, static_cast<std::int64_t>(rate / 2U));
        }

        beginTest("SEQ-M4-007 buffer-size-independent absolute positions");
        TempoMap map;
        std::int64_t absolute = 0;
        expect(map.absoluteFrameAt({960, 0U}, 48'000U, absolute).wasOk());
        expectEquals(absolute, std::int64_t{24'000});
        for (const auto blockSize : {64U, 127U, 512U, 2048U}) {
            const auto blockStart = absolute - static_cast<std::int64_t>(absolute % blockSize);
            std::int32_t offset = 0;
            expect(absoluteFrameToBlockOffset(absolute, blockStart, blockSize, offset).wasOk());
            expectEquals(blockStart + static_cast<std::int64_t>(offset), absolute);
        }

        beginTest("SEQ-M4-008 tempo changes preserve musical position");
        expect(map.setTempo({960, 0U}, 60'000'000).wasOk());
        expectEquals(map.tempoAt({959, 65'535U}), defaultTempoMicroBpm);
        expectEquals(map.tempoAt({960, 0U}), std::int64_t{60'000'000});
        expect(map.absoluteFrameAt({1'920, 0U}, 48'000U, absolute).wasOk());
        expectEquals(absolute, std::int64_t{72'000});
        expect(map.setTempo({960, 0U}, 90'000'000).wasOk());
        expectEquals(map.points().size(), std::size_t{2U});
        expect(map.replacePoints({{{1, 0U}, defaultTempoMicroBpm}}).failed());
        expect(
            map.replacePoints({{{0, 0U}, defaultTempoMicroBpm}, {{0, 0U}, 100'000'000}}).failed());

        beginTest("SEQ-M4-009 ties-to-even rounding");
        std::int64_t rounded = -1;
        constexpr auto tieTempo = std::int64_t{117'187'500};
        expect(musicalDurationToFrames({0, 1'280U}, tieTempo, 48'000U, rounded).wasOk());
        expectEquals(rounded, std::int64_t{0});
        expect(musicalDurationToFrames({0, 3'840U}, tieTempo, 48'000U, rounded).wasOk());
        expectEquals(rounded, std::int64_t{2});

        beginTest("SEQ-M4-010 checked large-position arithmetic");
        expect(
            map.absoluteFrameAt({std::numeric_limits<std::int64_t>::max(), 0U}, 96'000U, absolute)
                .failed());
        MusicalTime overflow;
        expect(addMusicalDuration({std::numeric_limits<std::int64_t>::max(), 0U}, {1, 0U}, overflow)
                   .failed());

        beginTest("SEQ-M4-044 supported time signatures and bar conversion");
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
            expect(isSupportedTimeSignature(signature));
            const auto barLength = ticksPerBar(signature);
            BarBeatTick display;
            expect(musicalTimeToBarBeatTick({barLength + ticksPerBeat(signature), 123U}, signature,
                                            display)
                       .wasOk());
            expectEquals(display.bar, std::int64_t{1});
            expectEquals(static_cast<int>(display.beat), 1);
            MusicalTime restored;
            expect(barBeatTickToMusicalTime(display, signature, restored).wasOk());
            expect(restored == MusicalTime{barLength + ticksPerBeat(signature),
                                           static_cast<std::uint16_t>(123U)});
        }
        expect(!isSupportedTimeSignature({4U, 8U}));
    }
};

static Milestone4TimeTests milestone4TimeTests;
} // namespace padflow
