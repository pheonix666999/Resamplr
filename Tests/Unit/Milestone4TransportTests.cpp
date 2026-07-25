#include "Sequencing/Transport.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace padflow {
namespace {
TransportConfiguration configuration(const TimeSignature signature = {4U, 4U},
                                     const std::int64_t bars = 1,
                                     const std::uint64_t generation = 1U) {
    TempoMap tempo;
    TransportConfiguration result;
    const auto length = ticksPerBar(signature) * bars;
    juce::ignoreUnused(makeTransportConfiguration(tempo, signature, {length, 0U}, 48'000U,
                                                  generation, 0.5F, result));
    return result;
}

void process(TransportEngine& transport, const std::size_t frames, float* left = nullptr,
             float* right = nullptr) {
    std::vector<float> scratchLeft;
    std::vector<float> scratchRight;
    if (left == nullptr)
        scratchLeft.resize(frames);
    if (right == nullptr)
        scratchRight.resize(frames);
    transport.beginBlock();
    transport.processMetronomeAdd(left != nullptr ? left : scratchLeft.data(),
                                  right != nullptr ? right : scratchRight.data(), frames);
}
} // namespace

class Milestone4TransportTests final : public juce::UnitTest {
  public:
    Milestone4TransportTests() : juce::UnitTest("Milestone 4 transport and metronome") {}

    void runTest() override {
        beginTest("SEQ-M4-020 through SEQ-M4-023 play stop position and return");
        const auto config = configuration();
        TransportEngine transport;
        expect(transport.publishConfiguration(&config));
        expect(transport.play());
        process(transport, 64U);
        expect(transport.snapshot().state == TransportState::playing);
        expectEquals(transport.snapshot().framePosition, std::int64_t{64});
        expect(transport.setPositionFrames(1'000));
        process(transport, 32U);
        expectEquals(transport.snapshot().framePosition, std::int64_t{1'032});
        expect(transport.stop());
        transport.beginBlock();
        expect(transport.consumePanicRequest());
        expect(transport.snapshot().state == TransportState::stopped);
        expect(transport.returnToStart());
        process(transport, 1U);
        expectEquals(transport.snapshot().framePosition, std::int64_t{0});

        beginTest("SEQ-M4-024 and SEQ-M4-025 exact loop iteration for large blocks");
        TransportEngine looping;
        expect(looping.publishConfiguration(&config));
        expect(looping.play());
        process(looping, static_cast<std::size_t>(config.patternLengthFrames * 3 + 17));
        expectEquals(looping.snapshot().loopIteration, std::uint64_t{3U});
        expectEquals(looping.snapshot().framePosition, std::int64_t{17});

        beginTest("SEQ-M4-026 panic and SEQ-M4-028 immutable configuration swap");
        expect(looping.panic());
        looping.beginBlock();
        expect(looping.consumePanicRequest());
        expect(looping.snapshot().state == TransportState::stopped);
        const auto replacement = configuration({3U, 4U}, 1, 2U);
        expect(looping.publishConfiguration(&replacement));
        process(looping, 1U);
        expectEquals(looping.snapshot().configurationGeneration, std::uint64_t{2U});

        beginTest("SEQ-M4-027 device-change stop policy");
        expect(looping.play());
        process(looping, 32U);
        looping.stopAndPanicWhenQuiescent();
        expect(looping.snapshot().state == TransportState::stopped);
        expectEquals(looping.snapshot().framePosition, std::int64_t{0});
        expect(looping.consumePanicRequest());

        beginTest("SEQ-M4-030 through SEQ-M4-032 generated meter accents");
        for (const auto signature :
             {TimeSignature{4U, 4U}, TimeSignature{3U, 4U}, TimeSignature{6U, 8U}}) {
            const auto meterConfig = configuration(signature);
            expectEquals(meterConfig.beatCount, static_cast<std::size_t>(signature.numerator));
            expect(meterConfig.accentedBeats[0]);
            for (std::size_t beat = 1U; beat < meterConfig.beatCount; ++beat)
                expect(!meterConfig.accentedBeats[beat]);
        }
        TransportEngine click;
        expect(click.publishConfiguration(&config));
        expect(click.setMetronomeEnabled(true));
        expect(click.play());
        std::array<float, 512U> left{};
        std::array<float, 512U> right{};
        process(click, left.size(), left.data(), right.data());
        bool nonSilent = false;
        for (const auto sample : left)
            nonSilent = nonSilent || std::abs(sample) > 0.0F;
        expect(nonSilent);

        beginTest("SEQ-M4-033 through SEQ-M4-035 count-in duration and record boundary");
        for (const auto& [bars, multiplier] :
             {std::pair{CountInBars::one, 1}, std::pair{CountInBars::two, 2}}) {
            TransportEngine countIn;
            expect(countIn.publishConfiguration(&config));
            expect(countIn.setCountInBars(bars));
            expect(countIn.record());
            process(countIn,
                    static_cast<std::size_t>(config.firstBarLengthFrames * multiplier - 1));
            expect(countIn.snapshot().state == TransportState::countIn);
            process(countIn, 1U);
            expect(countIn.snapshot().state == TransportState::recording);
            expectEquals(countIn.snapshot().framePosition, std::int64_t{0});
        }

        beginTest("Tap tempo uses bounded intervals and BPM limits");
        TapTempoEstimator taps;
        expectEquals(taps.tap(1'000), defaultTempoMicroBpm);
        expectEquals(taps.tap(1'500), std::int64_t{120'000'000});
        expectEquals(taps.tap(2'000), std::int64_t{120'000'000});
        expectEquals(taps.tap(5'000), defaultTempoMicroBpm);
    }
};

static Milestone4TransportTests milestone4TransportTests;
} // namespace padflow
