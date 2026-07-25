#include "App/ApplicationController.h"
#include "Audio/PlaybackEngine.h"
#include "Chopping/AssignmentPlan.h"
#include "Sampling/SampleAsset.h"
#include "Serialization/ProjectSerializer.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace padflow {
namespace {
ExternalAssetReference persistentAsset() {
    return {"persist-asset",
            {},
            "Synthetic.wav",
            "wav",
            "persist-fingerprint",
            0U,
            0,
            1U,
            48000.0,
            16U,
            64U,
            false};
}

juce::Result assignSlices(ApplicationController& controller) {
    JobSpec target{controller.project().uuid(), controller.project().pad(0U).uuid,
                   controller.project().revision(), 0, JobKind::sampleImport};
    if (const auto imported = controller.commitImportedLayer(target, 0U, 0U, persistentAsset());
        imported.failed())
        return imported;

    SliceGenerationRequest generation{"persist-set",
                                      "persist-asset",
                                      "persist-fingerprint",
                                      controller.project().pad(0U).layers[0].uuid,
                                      0,
                                      16,
                                      4,
                                      SliceRemainderPolicy::include,
                                      SliceDisplayUnit::frames};
    SliceSet slices;
    if (const auto generated = generateEqualSlices(generation, slices); generated.failed())
        return generated;
    slices.parameters.transientSensitivity = 0.75F;
    slices.slices[0].colourArgb = 0xff2288ccU;

    AssignmentRequest request{"persist-session",
                              controller.project().uuid(),
                              controller.project().revision(),
                              controller.project().pad(0U).uuid,
                              controller.project().pad(0U).layers[0].uuid,
                              "persist-asset",
                              "persist-fingerprint",
                              std::move(slices),
                              AssignmentDestinationMode::consecutivePads,
                              1U,
                              0U,
                              false,
                              false};
    AssignmentPlan plan;
    if (const auto planned = buildAssignmentPlan(controller.project().state(), request, plan);
        planned.failed())
        return planned;
    AssignmentCommitReport report;
    return controller.commitSliceAssignment(plan, report);
}

std::shared_ptr<const SampleAsset> playbackAsset() {
    std::vector<float> pcm(16U);
    for (std::size_t frame = 0U; frame < pcm.size(); ++frame)
        pcm[frame] = 0.2F + static_cast<float>(frame) * 0.02F;
    return SampleAsset::create({"persist-asset",
                                "Synthetic",
                                48000.0,
                                1U,
                                16U,
                                {},
                                {},
                                {},
                                {},
                                "persist-fingerprint",
                                0U,
                                0,
                                0.0},
                               std::move(pcm));
}
} // namespace

class Milestone3PersistenceTests final : public juce::UnitTest {
  public:
    Milestone3PersistenceTests()
        : juce::UnitTest("Milestone 3 persistence and slice playback", "PadFlow") {}

    void runTest() override {
        ApplicationController controller;
        controller.createEmptyProject("Persistence", "persist-project");
        expect(assignSlices(controller).wasOk());

        beginTest("SAVE-M3-001 through SAVE-M3-005 semantic chopping round trip");
        const auto manifest = ProjectSerializer::canonicalManifest(controller.project());
        expect(manifest.contains("\"sliceSets\""));
        expect(manifest.contains("\"sliceAssignment\""));
        auto restored = Project::createEmpty();
        expect(ProjectSerializer::restoreCanonicalManifest(manifest, restored).wasOk());
        expect(restored.state() == controller.project().state());
        expectEquals(restored.revision(), controller.project().revision());
        expectEquals(restored.state().sliceSets.size(), std::size_t{1U});
        expectEquals(restored.state().sliceSets[0].slices.size(), std::size_t{4U});
        expect(restored.state().sliceSets[0].slices[0].colourArgb.has_value());

        beginTest("SAVE-M3-006 missing source retains slice metadata");
        auto missingState = restored.state();
        missingState.assets[0].missing = true;
        auto missingProject = Project::createEmpty();
        expect(missingProject.restoreState(missingState, restored.revision()).wasOk());
        auto missingRestored = Project::createEmpty();
        expect(ProjectSerializer::restoreCanonicalManifest(
                   ProjectSerializer::canonicalManifest(missingProject), missingRestored)
                   .wasOk());
        expect(missingRestored.state().assets[0].missing);
        expect(missingRestored.state().sliceSets == missingState.sliceSets);

        beginTest("SAVE-M3-007 and SAVE-M3-008 invalid slices reject atomically");
        auto invalidJson = juce::JSON::parse(manifest);
        auto* root = invalidJson.getDynamicObject();
        expect(root != nullptr);
        if (root != nullptr) {
            auto sliceSetsValue = root->getProperty("sliceSets");
            auto* sliceSets = sliceSetsValue.getArray();
            expect(sliceSets != nullptr && !sliceSets->isEmpty());
            if (sliceSets != nullptr && !sliceSets->isEmpty()) {
                auto* set = (*sliceSets)[0].getDynamicObject();
                auto slicesValue = set->getProperty("slices");
                auto* slices = slicesValue.getArray();
                auto* first = (*slices)[0].getDynamicObject();
                first->setProperty("endFrame", first->getProperty("startFrame"));
                const auto beforeInvalid = missingRestored.state();
                expect(ProjectSerializer::restoreCanonicalManifest(
                           juce::JSON::toString(invalidJson, false, 17), missingRestored)
                           .failed());
                expect(missingRestored.state() == beforeInvalid);
            }
        }

        beginTest("AUDIO-M3-001 through AUDIO-M3-010 bounded finite slice playback");
        SampleAssetRegistry registry{1024U * 1024U};
        expect(registry.publish(playbackAsset()));
        auto snapshot = makePlaybackSnapshot(restored.state(), registry);
        PlaybackEngine engine;
        engine.prepare(48000.0);
        engine.publishSnapshot(&snapshot);
        std::array<float, 32U> left{};
        std::array<float, 32U> right{};
        expect(engine.enqueue({AudioCommandType::triggerPad, 1U, 1U, 127.0F}));
        engine.processBlock(left.data(), right.data(), left.size());
        expect(std::any_of(left.begin(), left.end(),
                           [](const auto sample) { return std::abs(sample) > 0.00001F; }));
        expect(std::all_of(left.begin(), left.end(),
                           [](const auto sample) { return std::isfinite(sample); }));
        expectEquals(static_cast<int>(engine.activeVoiceCount()), 0);

        auto reverseState = restored.state();
        reverseState.banks[0].pads[1].layers[0].playback.reverseEnabled = true;
        snapshot = makePlaybackSnapshot(reverseState, registry);
        engine.publishSnapshot(&snapshot);
        expect(engine.enqueue({AudioCommandType::triggerPad, 1U, 2U, 127.0F}));
        engine.processBlock(left.data(), right.data(), left.size());
        expect(std::all_of(left.begin(), left.end(),
                           [](const auto sample) { return std::isfinite(sample); }));
        expectEquals(static_cast<int>(engine.activeVoiceCount()), 0);

        auto oneFrameState = restored.state();
        auto& oneFrame = oneFrameState.banks[0].pads[1].layers[0];
        oneFrame.playback.startFrame = 3U;
        oneFrame.playback.endFrame = 4U;
        oneFrame.playback.loopStartFrame = 3U;
        oneFrame.playback.loopEndFrame = 4U;
        oneFrame.tuningCents = 1200.0F;
        snapshot = makePlaybackSnapshot(oneFrameState, registry);
        engine.publishSnapshot(&snapshot);
        expect(engine.enqueue({AudioCommandType::triggerPad, 1U, 3U, 127.0F}));
        engine.processBlock(left.data(), right.data(), left.size());
        expect(std::all_of(left.begin(), left.end(),
                           [](const auto sample) { return std::isfinite(sample); }));
        expectEquals(static_cast<int>(engine.activeVoiceCount()), 0);
    }
};

static Milestone3PersistenceTests milestone3PersistenceTests;
} // namespace padflow
