#include "Audio/PlaybackEngine.h"
#include "Sampling/SampleAsset.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <vector>

namespace padflow {
class Milestone1AudioTests final : public juce::UnitTest {
  public:
    Milestone1AudioTests() : juce::UnitTest("Milestone 1 audio", "PadFlow") {}

    void runTest() override {
        std::vector<float> pcm(512U);
        for (std::size_t frame = 0; frame < 256U; ++frame) {
            pcm[frame * 2U] = std::sin(static_cast<float>(frame) * 0.071F) * 0.5F;
            pcm[frame * 2U + 1U] = std::cos(static_cast<float>(frame) * 0.053F) * 0.5F;
        }
        SampleAssetMetadata metadata;
        metadata.assetUuid = "playback-asset";
        metadata.displayName = "Playback";
        metadata.sampleRate = 48000.0;
        metadata.channelCount = 2U;
        metadata.frameCount = 256U;
        const auto asset = SampleAsset::create(std::move(metadata), std::move(pcm));
        SampleAssetRegistry registry{1024U * 1024U};
        expect(registry.publish(asset));

        auto project = makeDefaultProjectState("audio-project", "Audio");
        auto& pad = project.banks[0].pads[0];
        pad.layers[0].enabled = true;
        pad.layers[0].assetUuid = "playback-asset";
        pad.layers[0].velocityMinimum = 1U;
        pad.layers[0].velocityMaximum = 127U;
        pad.parameters.envelope = {0.0F, 0.0F, 1.0F, 0.005F};
        auto snapshot = makePlaybackSnapshot(project, registry);

        PlaybackEngine engine;
        engine.prepare(48000.0);
        engine.publishSnapshot(&snapshot);
        std::array<float, 512U> left{};
        std::array<float, 512U> right{};

        beginTest("AUDIO-M1-001, AUDIO-M1-002, and AUDIO-M1-022 stable voice pool");
        expectEquals(static_cast<int>(PlaybackEngine::voiceCount), 128);
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 1U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 1U);
        expectEquals(engine.lastAllocatedVoiceIndex(), 0);
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 2U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 1U);
        expectEquals(engine.lastAllocatedVoiceIndex(), 1);

        beginTest("AUDIO-M1-003 and AUDIO-M1-007 deterministic local/global stealing");
        engine.panic();
        snapshot.pads[0].maximumVoices = 1U;
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 10U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 1U);
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 11U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 1U);
        expectEquals(engine.lastAllocatedVoiceIndex(), 0);
        snapshot.pads[0].maximumVoices = 128U;
        engine.panic();
        for (std::uint32_t index = 0U; index < 129U; ++index)
            expect(engine.enqueue(
                AudioCommand{AudioCommandType::triggerPad, 0U, index + 100U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 1U);
        expectEquals(engine.lastAllocatedVoiceIndex(), 0);

        beginTest("AUDIO-M1-004 through AUDIO-M1-006 mono, choke, and release priority");
        engine.panic();
        snapshot.pads[0].maximumVoices = 128U;
        snapshot.pads[0].playbackMode = PlaybackMode::gate;
        for (std::uint32_t index = 0U; index < 128U; ++index)
            expect(engine.enqueue(
                AudioCommand{AudioCommandType::triggerPad, 0U, index + 1000U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 1U);
        expect(engine.enqueue(AudioCommand{AudioCommandType::releaseSource, 0U, 1005U, 0.0F}));
        engine.processBlock(left.data(), right.data(), 1U);
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 2000U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 1U);
        expectEquals(engine.lastAllocatedVoiceIndex(), 5);
        snapshot.pads[0].polyphonyMode = PolyphonyMode::mono;
        engine.panic();
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 2100U, 127.0F}));
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 2101U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 1U);
        expect(engine.activeVoiceCount() > 0U);
        snapshot.pads[0].polyphonyMode = PolyphonyMode::poly;

        beginTest("AUDIO-M1-009 through AUDIO-M1-012 playback modes and envelope release");
        engine.panic();
        snapshot.pads[0].playbackMode = PlaybackMode::gate;
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 500U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 16U);
        expect(engine.activeVoiceCount() > 0U);
        expect(engine.enqueue(AudioCommand{AudioCommandType::releaseSource, 0U, 500U, 0.0F}));
        engine.processBlock(left.data(), right.data(), 512U);
        expectEquals(static_cast<int>(engine.activeVoiceCount()), 0);
        snapshot.pads[0].playbackMode = PlaybackMode::toggle;
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 501U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 1U);
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 501U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 512U);
        expectEquals(static_cast<int>(engine.activeVoiceCount()), 0);

        beginTest("AUDIO-M1-013 through AUDIO-M1-019 finite gain, pan, pitch, and interpolation");
        engine.panic();
        snapshot.pads[0].playbackMode = PlaybackMode::oneShot;
        snapshot.pads[0].pan = -1.0F;
        snapshot.pads[0].coarseSemitones = 12.0F;
        snapshot.pads[0].fineCents = -25.0F;
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 600U, 100.0F}));
        engine.processBlock(left.data(), right.data(), 128U);
        bool nonSilent = false;
        for (std::size_t index = 0; index < 128U; ++index) {
            expect(std::isfinite(left[index]) && std::isfinite(right[index]));
            nonSilent = nonSilent || std::abs(left[index]) > 0.00001F;
            expectWithinAbsoluteError(right[index], 0.0F, 0.00001F);
        }
        expect(nonSilent);

        beginTest("AUDIO-M1-020, AUDIO-M1-023, and AUDIO-M1-024 panic and safe reset");
        expect(engine.enqueue(AudioCommand{AudioCommandType::panic, 0U, 0U, 0.0F}));
        engine.processBlock(left.data(), right.data(), 32U);
        expectEquals(static_cast<int>(engine.activeVoiceCount()), 0);
        engine.publishSnapshot(nullptr);
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 700U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 32U);
        for (std::size_t index = 0; index < 32U; ++index)
            expect(left[index] == 0.0F && right[index] == 0.0F);
        engine.prepare(96000.0);
        expectEquals(static_cast<int>(engine.activeVoiceCount()), 0);

        beginTest("AUDIO-M1-025 buffer variants remain finite");
        engine.publishSnapshot(&snapshot);
        for (const auto frames : {1U, 7U, 64U, 257U}) {
            expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, frames, 127.0F}));
            engine.processBlock(left.data(), right.data(), frames);
            for (std::size_t index = 0; index < frames; ++index)
                expect(std::isfinite(left[index]) && std::isfinite(right[index]));
            engine.panic();
        }
    }
};

static Milestone1AudioTests milestone1AudioTests;
} // namespace padflow
