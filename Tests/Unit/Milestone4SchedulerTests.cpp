#include "Audio/PlaybackEngine.h"
#include "Sampling/SampleAsset.h"
#include "Sequencing/Scheduler.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace padflow {
namespace {
constexpr auto projectSeed = "00010203-0405-4607-8809-0a0b0c0d0e0f";
constexpr auto patternUuid = "10000000-0000-4000-8000-000000000001";
constexpr auto eventUuid = "20000000-0000-4000-8000-000000000001";
constexpr auto padUuid = "30000000-0000-4000-8000-000000000001";
constexpr std::uint64_t schedulerGeneration = 44U;

juce::String indexedUuid(const std::size_t index) {
    return "20000000-0000-4000-8000-" +
           juce::String::toHexString(static_cast<std::int64_t>(index)).paddedLeft('0', 12);
}

SequenceEvent eventAt(const std::int64_t tick, const char* const uuid = eventUuid) {
    SequenceEvent event;
    event.uuid = uuid;
    event.padUuid = padUuid;
    event.start = {tick, 0U};
    return event;
}

Pattern patternWith(SequenceEvent event) {
    auto pattern = makeDefaultPattern(patternUuid);
    pattern.events.push_back(std::move(event));
    sortPatternEvents(pattern);
    return pattern;
}

juce::Result build(const Pattern& pattern, SchedulerSnapshot& output,
                   const std::uint32_t sampleRate = 48'000U) {
    TempoMap tempo;
    return makeSchedulerSnapshot(pattern, tempo, projectSeed, sampleRate, schedulerGeneration,
                                 output);
}

TransportPositionSnapshot transportAt(const std::int64_t frame, const std::uint64_t loop = 0U,
                                      const bool looping = true) {
    return {TransportState::playing, frame, loop, schedulerGeneration, looping, false};
}

std::size_t countType(const ScheduledCommandBuffer& buffer, const AudioCommandType type) {
    return static_cast<std::size_t>(
        std::count_if(buffer.view().begin(), buffer.view().end(),
                      [type](const AudioCommand& command) { return command.type == type; }));
}

class Milestone4SchedulerTests final : public juce::UnitTest {
  public:
    Milestone4SchedulerTests()
        : juce::UnitTest("Milestone 4 deterministic scheduler", "sequencing") {}

    void runTest() override {
        PatternScheduler scheduler;
        ScheduledCommandBuffer commands;

        beginTest("SEQ-M4-060 through SEQ-M4-062 use half-open sample-accurate blocks");
        SchedulerSnapshot insideSnapshot;
        expect(build(patternWith(eventAt(240)), insideSnapshot).wasOk());
        scheduler.publishSnapshot(&insideSnapshot);
        scheduler.processBlock(transportAt(6'000), 128U, commands);
        expectEquals(commands.size, std::size_t{1U});
        if (commands.size == 1U)
            expect(commands.commands[0U].frameOffset == 0U);
        scheduler.processBlock(transportAt(5'900), 100U, commands);
        expectEquals(commands.size, std::size_t{0U});
        scheduler.processBlock(transportAt(5'900), 200U, commands);
        expectEquals(commands.size, std::size_t{1U});
        if (commands.size == 1U)
            expect(commands.commands[0U].frameOffset == 100U);

        beginTest("SEQ-M4-063 gates crossing blocks trigger and release once");
        SchedulerSnapshot gateSnapshot;
        expect(build(patternWith(eventAt(0)), gateSnapshot).wasOk());
        scheduler.publishSnapshot(&gateSnapshot);
        scheduler.processBlock(transportAt(0), 128U, commands);
        expectEquals(countType(commands, AudioCommandType::triggerPad), std::size_t{1U});
        expectEquals(countType(commands, AudioCommandType::releaseSource), std::size_t{0U});
        scheduler.processBlock(transportAt(5'900), 200U, commands);
        expectEquals(countType(commands, AudioCommandType::triggerPad), std::size_t{0U});
        expectEquals(countType(commands, AudioCommandType::releaseSource), std::size_t{1U});
        if (commands.size == 1U)
            expect(commands.commands[0U].frameOffset == 100U);

        beginTest("SEQ-M4-064 and SEQ-M4-065 preserve events through multiple wraps");
        SchedulerSnapshot wrapSnapshot;
        auto wrapEvent = eventAt(3'780);
        wrapEvent.duration = {240, 0U};
        expect(build(patternWith(wrapEvent), wrapSnapshot).wasOk());
        scheduler.publishSnapshot(&wrapSnapshot);
        scheduler.processBlock(transportAt(94'400), 7'000U, commands);
        expectEquals(countType(commands, AudioCommandType::triggerPad), std::size_t{1U});
        expectEquals(countType(commands, AudioCommandType::releaseSource), std::size_t{1U});
        if (commands.size == 2U) {
            expect(commands.commands[0U].frameOffset == 100U);
            expect(commands.commands[1U].frameOffset == 6'100U);
            expect(commands.commands[0U].generation == commands.commands[1U].generation);
        }
        scheduler.publishSnapshot(&gateSnapshot);
        scheduler.processBlock(transportAt(0), 192'001U, commands);
        expectEquals(countType(commands, AudioCommandType::triggerPad), std::size_t{3U});
        expect(commands.commands[0U].frameOffset == 0U);
        expect(commands.commands[2U].frameOffset == 96'000U);
        expect(commands.commands[4U].frameOffset == 192'000U);

        beginTest("SEQ-M4-066 output order repeats exactly");
        ScheduledCommandBuffer repeated;
        scheduler.processBlock(transportAt(0), 192'001U, repeated);
        expectEquals(repeated.size, commands.size);
        for (std::size_t index = 0U; index < commands.size; ++index)
            expect(commands.commands[index].type == repeated.commands[index].type &&
                   commands.commands[index].frameOffset == repeated.commands[index].frameOffset &&
                   commands.commands[index].generation == repeated.commands[index].generation);

        beginTest("SEQ-M4-067 reports deterministic fixed-buffer truncation");
        auto overflowPattern = makeDefaultPattern(patternUuid);
        overflowPattern.events.reserve(300U);
        for (std::size_t index = 1U; index <= 300U; ++index) {
            auto event = eventAt(0);
            event.uuid = indexedUuid(index);
            event.ratchetCount = 16U;
            event.ratchetSpacing = {1, 0U};
            overflowPattern.events.push_back(std::move(event));
        }
        sortPatternEvents(overflowPattern);
        auto overflowSnapshot = std::make_unique<SchedulerSnapshot>();
        expect(build(overflowPattern, *overflowSnapshot).wasOk());
        scheduler.publishSnapshot(overflowSnapshot.get());
        scheduler.processBlock(transportAt(0), 96'000U, commands);
        expectEquals(commands.size, schedulerCommandCapacity);
        expect(commands.dropped > 0U);
        expect(scheduler.overflowCount() >= commands.dropped);

        beginTest("SEQ-M4-068 stopped transport clears scheduler output");
        auto stopped = transportAt(0);
        stopped.state = TransportState::stopped;
        scheduler.processBlock(stopped, 96'000U, commands);
        expectEquals(commands.size, std::size_t{0U});

        beginTest("SEQ-M4-100 through SEQ-M4-105 apply swing before nudge");
        auto swingPattern = patternWith(eventAt(480));
        swingPattern.stepResolution = StepResolution::eighth;
        SchedulerSnapshot swingSnapshot;
        expect(build(swingPattern, swingSnapshot).wasOk());
        expectEquals(swingSnapshot.pulses.front().loopingTriggerFrame, std::int64_t{12'000});
        swingPattern.swingPercent = 50U;
        expect(build(swingPattern, swingSnapshot).wasOk());
        expectEquals(swingSnapshot.pulses.front().loopingTriggerFrame, std::int64_t{18'000});
        swingPattern.swingPercent = 75U;
        expect(build(swingPattern, swingSnapshot).wasOk());
        expect(swingSnapshot.pulses.front().loopingTriggerFrame < 24'000);
        swingPattern.swingPercent = 50U;
        swingPattern.events.front().microOffset = MicroOffsetQ16::fromWholeTicks(120);
        expect(build(swingPattern, swingSnapshot).wasOk());
        expectEquals(swingSnapshot.pulses.front().loopingTriggerFrame, std::int64_t{21'000});
        auto firstSubdivision = patternWith(eventAt(0));
        firstSubdivision.swingPercent = 75U;
        expect(build(firstSubdivision, swingSnapshot).wasOk());
        expectEquals(swingSnapshot.pulses.front().loopingTriggerFrame, std::int64_t{0});
        auto boundary = patternWith(eventAt(3'600));
        boundary.swingPercent = 75U;
        expect(build(boundary, swingSnapshot).wasOk());
        expect(swingSnapshot.pulses.front().loopingTriggerFrame < 96'000);

        beginTest("SEQ-M4-120 through SEQ-M4-126 expand bounded parent ratchets");
        auto ratchetEvent = eventAt(0);
        ratchetEvent.ratchetCount = 3U;
        ratchetEvent.ratchetSpacing = {60, 0U};
        SchedulerSnapshot ratchetSnapshot;
        expect(build(patternWith(ratchetEvent), ratchetSnapshot).wasOk());
        expectEquals(ratchetSnapshot.pulses.size(), std::size_t{3U});
        expectEquals(ratchetSnapshot.pulses[0U].loopingTriggerFrame, std::int64_t{0});
        expectEquals(ratchetSnapshot.pulses[1U].loopingTriggerFrame, std::int64_t{1'500});
        expectEquals(ratchetSnapshot.pulses[2U].loopingTriggerFrame, std::int64_t{3'000});
        for (const auto& pulse : ratchetSnapshot.pulses)
            expect(pulse.loopingReleaseFrame >= 0 &&
                   pulse.loopingReleaseFrame < ratchetSnapshot.patternLengthFrames);
        const ProbabilityContext ratchetContext{ratchetSnapshot.pulses.front().probabilityIdentity,
                                                0U};
        const auto draw = probabilityDrawQ32(ratchetContext);
        const auto threshold = static_cast<ProbabilityQ32>(draw) + 1U;
        for (const auto& pulse : ratchetSnapshot.pulses)
            expect(probabilityAccepts(threshold, {pulse.probabilityIdentity, 0U}));

        auto endingRatchet = ratchetEvent;
        endingRatchet.start = {3'800, 0U};
        expect(build(patternWith(endingRatchet), ratchetSnapshot).wasOk());
        expectEquals(ratchetSnapshot.pulses[0U].triggerLoopDelta, static_cast<std::int16_t>(0));
        expectEquals(ratchetSnapshot.pulses[1U].triggerLoopDelta, static_cast<std::int16_t>(1));
        expectEquals(ratchetSnapshot.pulses[2U].triggerLoopDelta, static_cast<std::int16_t>(1));

        beginTest("SEQ-M4-127 stop discards pending ratchets and releases");
        scheduler.publishSnapshot(&ratchetSnapshot);
        scheduler.processBlock(stopped, 96'000U, commands);
        expectEquals(commands.size, std::size_t{0U});

        beginTest("SEQ-M4-140 through SEQ-M4-145 preserve signed Q16 nudge policy");
        auto nudged = eventAt(0);
        nudged.microOffset = MicroOffsetQ16::fromWholeTicks(120);
        SchedulerSnapshot nudgeSnapshot;
        expect(build(patternWith(nudged), nudgeSnapshot).wasOk());
        expectEquals(nudgeSnapshot.pulses.front().loopingTriggerFrame, std::int64_t{3'000});
        nudged.microOffset = MicroOffsetQ16::fromWholeTicks(-120);
        expect(build(patternWith(nudged), nudgeSnapshot).wasOk());
        expectEquals(nudgeSnapshot.pulses.front().nonLoopingTriggerFrame, std::int64_t{0});
        expectEquals(nudgeSnapshot.pulses.front().loopingTriggerFrame, std::int64_t{93'000});
        expectEquals(nudgeSnapshot.pulses.front().triggerLoopDelta, static_cast<std::int16_t>(-1));
        nudged.microOffset = {};
        expect(build(patternWith(nudged), nudgeSnapshot).wasOk());
        expectEquals(nudgeSnapshot.pulses.front().loopingTriggerFrame, std::int64_t{0});

        beginTest("SEQ-M4-069 scheduled sampler rendering is finite and non-silent");
        std::vector<float> pcm(512U, 0.25F);
        SampleAssetMetadata metadata;
        metadata.assetUuid = "scheduler-asset";
        metadata.displayName = "Scheduler";
        metadata.sampleRate = 48'000.0;
        metadata.channelCount = 1U;
        metadata.frameCount = pcm.size();
        const auto asset = SampleAsset::create(std::move(metadata), std::move(pcm));
        PlaybackSnapshot playback;
        playback.generation = 1U;
        auto& layer = playback.pads[0U].layers[0U];
        layer.asset = asset->view();
        layer.enabled = true;
        layer.endFrame = asset->metadata().frameCount;
        playback.pads[0U].envelope = {0.0F, 0.0F, 1.0F, 0.01F};
        scheduler.publishSnapshot(&gateSnapshot);
        scheduler.processBlock(transportAt(0), 128U, commands);
        PlaybackEngine engine;
        engine.prepare(48'000.0);
        engine.publishSnapshot(&playback);
        std::array<float, 128U> left{};
        std::array<float, 128U> right{};
        engine.processBlock(left.data(), right.data(), left.size(), commands.view());
        bool nonSilent = false;
        for (std::size_t index = 0U; index < left.size(); ++index) {
            expect(std::isfinite(left[index]) && std::isfinite(right[index]));
            nonSilent = nonSilent || std::abs(left[index]) > 0.00001F;
        }
        expect(nonSilent);
    }
};

static Milestone4SchedulerTests milestone4SchedulerTests;
} // namespace
} // namespace padflow
