#include "App/ApplicationController.h"
#include "Audio/PlaybackEngine.h"
#include "Sampling/SampleAsset.h"
#include "Serialization/ProjectSerializer.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace padflow {
namespace {
ExternalAssetReference makeReference(const juce::String& uuid, const std::uint64_t frames) {
    return {
        uuid,
        "synthetic/" + uuid + ".wav",
        uuid + ".wav",
        "WAV",
        "synthetic:" + uuid,
        frames * sizeof(float),
        0,
        1U,
        48000.0,
        frames,
        frames * sizeof(float),
        true,
    };
}

bool assignFixture(ApplicationController& controller, const juce::String& uuid,
                   const std::uint64_t frames) {
    const JobSpec target{controller.project().uuid(), controller.project().pad(0U).uuid,
                         controller.project().revision(), 0};
    return controller.commitImportedLayer(target, 0U, 0U, makeReference(uuid, frames)).wasOk();
}

std::shared_ptr<const SampleAsset> makeRampAsset(const std::uint64_t frames) {
    std::vector<float> pcm(static_cast<std::size_t>(frames));
    for (std::uint64_t frame = 0U; frame < frames; ++frame)
        pcm[static_cast<std::size_t>(frame)] = static_cast<float>(frame + 1U) / 16.0F;
    SampleAssetMetadata metadata;
    metadata.assetUuid = "edit-audio";
    metadata.displayName = "Edit audio";
    metadata.sampleRate = 48000.0;
    metadata.channelCount = 1U;
    metadata.frameCount = frames;
    metadata.contentFingerprint = "synthetic:edit-audio";
    return SampleAsset::create(std::move(metadata), std::move(pcm));
}

PlaybackSnapshot makeEditSnapshot(const SampleAssetRegistry& registry,
                                  SamplePlaybackSettings playback) {
    auto state = makeDefaultProjectState("edit-audio-project", "Edit audio");
    auto& pad = state.banks[0].pads[0];
    pad.layers[0].enabled = true;
    pad.layers[0].assetUuid = "edit-audio";
    pad.layers[0].playback = playback;
    pad.parameters.envelope = {0.0F, 0.0F, 1.0F, 0.001F};
    return makePlaybackSnapshot(state, registry);
}

float expectedRenderedSample(const std::uint64_t sourceFrame) {
    constexpr float centrePanGain = 0.70710678118F;
    return static_cast<float>(sourceFrame + 1U) / 16.0F * centrePanGain;
}
} // namespace

class Milestone2EditTests final : public juce::UnitTest {
  public:
    Milestone2EditTests() : juce::UnitTest("Milestone 2 editing and playback", "PadFlow") {}

    void runTest() override {
        ApplicationController controller;
        controller.createEmptyProject("Editing", "editing-project");
        expect(assignFixture(controller, "editing-asset", 100U));

        beginTest("EDIT-M2-001 default trim");
        auto playback = controller.project().pad(0U).layers[0].playback;
        expect(playback.initialized);
        expectEquals(static_cast<juce::int64>(playback.startFrame), juce::int64{0});
        expectEquals(static_cast<juce::int64>(playback.endFrame), juce::int64{100});
        expectEquals(static_cast<juce::int64>(playback.loopStartFrame), juce::int64{0});
        expectEquals(static_cast<juce::int64>(playback.loopEndFrame), juce::int64{100});
        expect(!playback.loopEnabled && !playback.reverseEnabled);

        beginTest("EDIT-M2-002 through EDIT-M2-007 inclusive/exclusive validation");
        expect(controller.setLayerTrim(0U, 0U, 10U, 90U).wasOk());
        expect(controller.setLayerLoop(0U, 0U, 20U, 40U).wasOk());
        const auto validState = controller.project().state();
        expect(controller.setLayerTrim(0U, 0U, 90U, 90U).failed());
        expect(controller.setLayerTrim(0U, 0U, 0U, 101U).failed());
        expect(controller.setLayerLoop(0U, 0U, 9U, 40U).failed());
        expect(controller.setLayerLoop(0U, 0U, 20U, 91U).failed());
        expect(controller.project().state() == validState);

        beginTest("EDIT-M2-003 one-frame trim and loop");
        expect(controller.setLayerTrim(0U, 0U, 37U, 38U).wasOk());
        expect(controller.setLayerLoop(0U, 0U, 37U, 38U).wasOk());
        expect(controller.setLayerLoopEnabled(0U, 0U, true).wasOk());

        beginTest("EDIT-M2-008 trim clamps or resets loop deterministically");
        expect(controller.resetLayerTrim(0U, 0U).wasOk());
        expect(controller.setLayerLoop(0U, 0U, 20U, 80U).wasOk());
        expect(controller.setLayerLoopEnabled(0U, 0U, true).wasOk());
        expect(controller.setLayerTrim(0U, 0U, 30U, 70U).wasOk());
        playback = controller.project().pad(0U).layers[0].playback;
        expectEquals(static_cast<juce::int64>(playback.loopStartFrame), juce::int64{30});
        expectEquals(static_cast<juce::int64>(playback.loopEndFrame), juce::int64{70});
        expect(playback.loopEnabled);
        expect(controller.setLayerTrim(0U, 0U, 80U, 90U).wasOk());
        playback = controller.project().pad(0U).layers[0].playback;
        expectEquals(static_cast<juce::int64>(playback.loopStartFrame), juce::int64{80});
        expectEquals(static_cast<juce::int64>(playback.loopEndFrame), juce::int64{90});
        expect(!playback.loopEnabled);

        beginTest("EDIT-M2-009 marker edit creates one undo transaction");
        expect(controller.resetLayerTrim(0U, 0U).wasOk());
        const auto beforeDrag = controller.project().pad(0U).layers[0].playback;
        expect(controller.setLayerTrim(0U, 0U, 12U, 88U).wasOk());
        const auto afterDrag = controller.project().pad(0U).layers[0].playback;
        expect(controller.undo());
        expect(controller.project().pad(0U).layers[0].playback == beforeDrag);
        expect(controller.redo());
        expect(controller.project().pad(0U).layers[0].playback == afterDrag);

        beginTest("EDIT-M2-010, EDIT-M2-011, and SAVE-M2-002 through SAVE-M2-004 round trip");
        expect(controller.setLayerLoop(0U, 0U, 24U, 72U).wasOk());
        expect(controller.setLayerLoopEnabled(0U, 0U, true).wasOk());
        expect(controller.setLayerReverseEnabled(0U, 0U, true).wasOk());
        expect(controller.setLayerZeroCrossingSnap(0U, 0U, true).wasOk());
        const auto manifest = ProjectSerializer::canonicalManifest(controller.project());
        auto restored = Project::createEmpty();
        expect(ProjectSerializer::restoreCanonicalManifest(manifest, restored).wasOk());
        expect(restored.state() == controller.project().state());

        beginTest("SAVE-M2-001 Milestone 1 layer payload remains loadable");
        auto legacy = Project::createEmpty("Legacy", "legacy-edit-project");
        auto legacyState = legacy.state();
        legacyState.banks[0].pads[0].layers[0].assetUuid = "legacy-asset";
        legacyState.banks[0].pads[0].layers[0].enabled = true;
        legacyState.assets.push_back(makeReference("legacy-asset", 64U));
        expect(legacy.restoreState(legacyState, 7U).wasOk());
        const auto legacyManifest = ProjectSerializer::canonicalManifest(legacy);
        expect(!legacyManifest.contains("\"editing\""));
        auto legacyRestored = Project::createEmpty();
        expect(ProjectSerializer::restoreCanonicalManifest(legacyManifest, legacyRestored).wasOk());
        const auto resolved = resolveSamplePlaybackSettings(legacyRestored.pad(0U).layers[0], 64U);
        expectEquals(static_cast<juce::int64>(resolved.startFrame), juce::int64{0});
        expectEquals(static_cast<juce::int64>(resolved.endFrame), juce::int64{64});

        beginTest("AUDIO-M2-001 through AUDIO-M2-004 forward and reverse trim bounds");
        SampleAssetRegistry registry{1024U * 1024U};
        const auto asset = makeRampAsset(16U);
        expect(registry.publish(asset));
        SamplePlaybackSettings audioPlayback;
        audioPlayback.startFrame = 2U;
        audioPlayback.endFrame = 6U;
        audioPlayback.loopStartFrame = 2U;
        audioPlayback.loopEndFrame = 6U;
        audioPlayback.initialized = true;
        auto snapshot = makeEditSnapshot(registry, audioPlayback);
        PlaybackEngine engine;
        engine.prepare(48000.0);
        engine.publishSnapshot(&snapshot);
        std::array<float, 32U> left{};
        std::array<float, 32U> right{};
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 1U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 8U);
        for (std::size_t index = 0U; index < 4U; ++index)
            expectWithinAbsoluteError(left[index], expectedRenderedSample(index + 2U), 0.00001F);
        for (std::size_t index = 4U; index < 8U; ++index)
            expectWithinAbsoluteError(left[index], 0.0F, 0.000001F);

        audioPlayback.reverseEnabled = true;
        snapshot = makeEditSnapshot(registry, audioPlayback);
        engine.panic();
        engine.publishSnapshot(&snapshot);
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 2U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 8U);
        for (std::size_t index = 0U; index < 4U; ++index)
            expectWithinAbsoluteError(left[index], expectedRenderedSample(5U - index), 0.00001F);
        for (std::size_t index = 4U; index < 8U; ++index)
            expectWithinAbsoluteError(left[index], 0.0F, 0.000001F);

        beginTest("AUDIO-M2-005 through AUDIO-M2-008 loop wrapping and one-frame loop");
        audioPlayback.startFrame = 0U;
        audioPlayback.endFrame = 6U;
        audioPlayback.loopStartFrame = 2U;
        audioPlayback.loopEndFrame = 4U;
        audioPlayback.loopEnabled = true;
        audioPlayback.reverseEnabled = false;
        snapshot = makeEditSnapshot(registry, audioPlayback);
        engine.panic();
        engine.publishSnapshot(&snapshot);
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 3U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 8U);
        constexpr std::array<std::uint64_t, 8U> forwardFrames{0U, 1U, 2U, 3U, 2U, 3U, 2U, 3U};
        for (std::size_t index = 0U; index < forwardFrames.size(); ++index)
            expectWithinAbsoluteError(left[index], expectedRenderedSample(forwardFrames[index]),
                                      0.00001F);

        audioPlayback.reverseEnabled = true;
        snapshot = makeEditSnapshot(registry, audioPlayback);
        engine.panic();
        engine.publishSnapshot(&snapshot);
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 4U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 8U);
        constexpr std::array<std::uint64_t, 8U> reverseFrames{5U, 4U, 3U, 2U, 3U, 2U, 3U, 2U};
        for (std::size_t index = 0U; index < reverseFrames.size(); ++index)
            expectWithinAbsoluteError(left[index], expectedRenderedSample(reverseFrames[index]),
                                      0.00001F);

        audioPlayback.loopStartFrame = 2U;
        audioPlayback.loopEndFrame = 3U;
        audioPlayback.reverseEnabled = false;
        snapshot = makeEditSnapshot(registry, audioPlayback);
        engine.panic();
        engine.publishSnapshot(&snapshot);
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 5U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 8U);
        expectWithinAbsoluteError(left[0], expectedRenderedSample(0U), 0.00001F);
        expectWithinAbsoluteError(left[1], expectedRenderedSample(1U), 0.00001F);
        for (std::size_t index = 2U; index < 8U; ++index)
            expectWithinAbsoluteError(left[index], expectedRenderedSample(2U), 0.00001F);

        beginTest("AUDIO-M2-009 through AUDIO-M2-013 release, finite bounds, and new triggers");
        snapshot.pads[0].playbackMode = PlaybackMode::gate;
        engine.panic();
        engine.publishSnapshot(&snapshot);
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 6U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 2U);
        expect(engine.enqueue(AudioCommand{AudioCommandType::releaseSource, 0U, 6U, 0.0F}));
        for (int block = 0; block < 5; ++block) {
            engine.processBlock(left.data(), right.data(), left.size());
            for (const auto sample : left)
                expect(std::isfinite(sample));
        }
        expectEquals(static_cast<int>(engine.activeVoiceCount()), 0);

        snapshot.pads[0].playbackMode = PlaybackMode::oneShot;
        snapshot.pads[0].layers[0].reverseEnabled = false;
        engine.panic();
        engine.publishSnapshot(&snapshot);
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 7U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 1U);
        snapshot.pads[0].layers[0].reverseEnabled = true;
        engine.processBlock(left.data(), right.data(), 1U);
        expectWithinAbsoluteError(left[0], expectedRenderedSample(1U), 0.00001F);

        beginTest("REGRESSION-M2-001 legacy raw snapshot resolves complete asset bounds");
        PlaybackSnapshot legacySnapshot;
        legacySnapshot.pads[0].layers[0].enabled = true;
        legacySnapshot.pads[0].layers[0].asset = asset->view();
        legacySnapshot.pads[0].envelope = {0.0F, 0.0F, 1.0F, 0.001F};
        engine.panic();
        engine.publishSnapshot(&legacySnapshot);
        expect(engine.enqueue(AudioCommand{AudioCommandType::triggerPad, 0U, 8U, 127.0F}));
        engine.processBlock(left.data(), right.data(), 2U);
        expect(engine.activeVoiceCount() > 0U);
        expectWithinAbsoluteError(left[0], expectedRenderedSample(0U), 0.00001F);
    }
};

static Milestone2EditTests milestone2EditTests;
} // namespace padflow
