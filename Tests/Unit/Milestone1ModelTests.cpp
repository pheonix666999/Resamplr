#include "App/ApplicationController.h"
#include "Model/PadModel.h"
#include "Model/Project.h"
#include "Serialization/ProjectSerializer.h"

#include <juce_core/juce_core.h>

#include <set>
#include <string>

namespace padflow {
class Milestone1ModelTests final : public juce::UnitTest {
  public:
    Milestone1ModelTests() : juce::UnitTest("Milestone 1 model", "PadFlow") {}

    void runTest() override {
        beginTest("MODEL-M1-001 through MODEL-M1-003 fixed banks, pads, and UUIDs");
        const auto project = Project::createEmpty("Model", "model-project");
        const auto repeated = Project::createEmpty("Model", "model-project");
        std::set<std::string> padUuids;
        expectEquals(static_cast<int>(project.state().banks.size()),
                     static_cast<int>(padBankCount));
        for (std::size_t bankIndex = 0; bankIndex < padBankCount; ++bankIndex) {
            const auto& bank = project.bank(bankIndex);
            expectEquals(bank.name, juce::String::charToString(static_cast<juce::juce_wchar>(
                                        'A' + static_cast<int>(bankIndex))));
            expectEquals(static_cast<int>(bank.pads.size()), static_cast<int>(padsPerBank));
            for (std::size_t padIndex = 0; padIndex < padsPerBank; ++padIndex) {
                const auto globalIndex = toGlobalPadIndex(bankIndex, padIndex);
                const auto& pad = project.pad(globalIndex);
                expect(pad.uuid.isNotEmpty());
                expect(padUuids.insert(pad.uuid.toStdString()).second);
                expectEquals(pad.uuid, repeated.pad(globalIndex).uuid);
            }
        }
        expectEquals(static_cast<int>(padUuids.size()), static_cast<int>(totalPadCount));

        beginTest("LAYER-M1-001 through LAYER-M1-005 layer defaults and validation");
        const auto& firstPad = project.pad(0U);
        expectEquals(static_cast<int>(firstPad.layers.size()),
                     static_cast<int>(minimumLayersPerPad));
        auto boundaryLayer = firstPad.layers[0];
        boundaryLayer.assetUuid = "asset";
        boundaryLayer.enabled = true;
        boundaryLayer.velocityMinimum = 1U;
        boundaryLayer.velocityMaximum = 127U;
        expect(validateLayer(boundaryLayer).wasOk());
        boundaryLayer.velocityMinimum = 100U;
        boundaryLayer.velocityMaximum = 99U;
        expect(validateLayer(boundaryLayer).failed());
        auto emptyLayer = firstPad.layers[1];
        emptyLayer.enabled = false;
        emptyLayer.assetUuid.clear();
        expect(validateLayer(emptyLayer).wasOk());

        beginTest("MODEL-M1-004 copy/paste preserves destination identity and PCM reference");
        ApplicationController controller;
        controller.createEmptyProject("Controller", "controller-project");
        auto sourceLayer = controller.project().pad(0U).layers[0];
        sourceLayer.assetUuid = "shared-asset";
        sourceLayer.enabled = true;
        expect(controller.setLayer(0U, 0U, sourceLayer).wasOk());
        expect(controller.renamePad(0U, "Kick").wasOk());
        const auto destinationUuid = controller.project().pad(1U).uuid;
        const auto destinationLayerUuid = controller.project().pad(1U).layers[0].uuid;
        const auto destinationMidi = controller.project().pad(1U).midiNote;
        const auto destinationKey = controller.project().pad(1U).keyboardKey;
        expect(controller.copyPad(0U).wasOk());
        expect(controller.pastePad(1U).wasOk());
        const auto& pasted = controller.project().pad(1U);
        expectEquals(pasted.uuid, destinationUuid);
        expectEquals(pasted.layers[0].uuid, destinationLayerUuid);
        expectEquals(pasted.layers[0].assetUuid, juce::String{"shared-asset"});
        expectEquals(static_cast<int>(pasted.midiNote), static_cast<int>(destinationMidi));
        expectEquals(pasted.keyboardKey, destinationKey);

        beginTest("MODEL-M1-005 duplicate creates new pad and layer identities");
        const auto previousDestinationUuid = controller.project().pad(2U).uuid;
        expect(controller.duplicatePad(0U, 2U).wasOk());
        const auto& duplicated = controller.project().pad(2U);
        expect(duplicated.uuid != controller.project().pad(0U).uuid);
        expect(duplicated.uuid != previousDestinationUuid);
        for (std::size_t layerIndex = 0; layerIndex < minimumLayersPerPad; ++layerIndex)
            expect(duplicated.layers[layerIndex].uuid !=
                   controller.project().pad(0U).layers[layerIndex].uuid);
        expectEquals(duplicated.layers[0].assetUuid, juce::String{"shared-asset"});

        beginTest("MODEL-M1-006 parameter command undo and redo");
        const auto originalParameters = controller.project().pad(3U).parameters;
        auto changedParameters = originalParameters;
        changedParameters.gainDecibels = -9.0F;
        changedParameters.pan = 0.25F;
        changedParameters.coarseSemitones = 7;
        expect(controller.setPadParameters(3U, changedParameters).wasOk());
        expect(controller.canUndo());
        expect(controller.project().pad(3U).parameters == changedParameters);
        expect(controller.undo());
        expect(controller.project().pad(3U).parameters == originalParameters);
        expect(controller.canRedo());
        expect(controller.redo());
        expect(controller.project().pad(3U).parameters == changedParameters);

        beginTest("REGRESSION-M1-004 persistent settings use unified project undo");
        const auto originalAudioSettings = controller.project().state().audio;
        auto changedAudioSettings = originalAudioSettings;
        changedAudioSettings.outputDeviceIdentifier = "synthetic-output";
        changedAudioSettings.sampleRate = 48000.0;
        changedAudioSettings.bufferSize = 256U;
        expect(controller.setAudioSettings(changedAudioSettings).wasOk());
        expect(controller.project().state().audio == changedAudioSettings);
        expect(controller.undo());
        expect(controller.project().state().audio == originalAudioSettings);
        expect(controller.redo());
        expect(controller.project().state().audio == changedAudioSettings);

        beginTest("MODEL-M1-007 clear pad preserves identity and mappings");
        const auto clearUuid = controller.project().pad(0U).uuid;
        const auto clearLayerUuid = controller.project().pad(0U).layers[0].uuid;
        const auto clearMidi = controller.project().pad(0U).midiNote;
        const auto clearKey = controller.project().pad(0U).keyboardKey;
        expect(controller.clearPad(0U).wasOk());
        const auto& cleared = controller.project().pad(0U);
        expectEquals(cleared.uuid, clearUuid);
        expectEquals(cleared.layers[0].uuid, clearLayerUuid);
        expect(cleared.layers[0].assetUuid.isEmpty());
        expect(!cleared.layers[0].enabled);
        expectEquals(static_cast<int>(cleared.midiNote), static_cast<int>(clearMidi));
        expectEquals(cleared.keyboardKey, clearKey);

        beginTest("MODEL-M1-008 rename and recolour validation");
        const auto beforeInvalidName = controller.project().pad(4U);
        expect(controller.renamePad(4U, "  Snare  ").wasOk());
        expectEquals(controller.project().pad(4U).name, juce::String{"Snare"});
        expect(controller.renamePad(4U, " ").failed());
        expectEquals(controller.project().pad(4U).name, juce::String{"Snare"});
        expect(controller.recolourPad(4U, 0xffd9865bU).wasOk());
        expectEquals(static_cast<juce::int64>(controller.project().pad(4U).colourArgb),
                     static_cast<juce::int64>(0xffd9865bU));
        expect(controller.recolourPad(4U, 0x00000000U).failed());
        expect(beforeInvalidName.uuid == controller.project().pad(4U).uuid);

        beginTest("SAVE-M1-001 through SAVE-M1-008 complete semantic round trip");
        auto persisted = Project::createEmpty("Persistence", "persistence-project");
        auto persistedState = persisted.state();
        auto& persistedPad = persistedState.banks[0].pads[0];
        persistedPad.name = "Layered Kick";
        persistedPad.colourArgb = 0xff123456U;
        persistedPad.parameters.gainDecibels = -7.25F;
        persistedPad.parameters.pan = -0.375F;
        persistedPad.parameters.coarseSemitones = -12;
        persistedPad.parameters.fineCents = 23.5F;
        persistedPad.parameters.playbackMode = PlaybackMode::toggle;
        persistedPad.parameters.polyphonyMode = PolyphonyMode::mono;
        persistedPad.parameters.chokeGroup = 4U;
        persistedPad.parameters.envelope = {0.25F, 0.5F, 0.75F, 1.25F};
        persistedPad.parameters.maximumVoices = 3U;
        auto& persistedLayer = persistedPad.layers[2];
        persistedLayer.assetUuid = "persistence-asset";
        persistedLayer.enabled = true;
        persistedLayer.velocityMinimum = 41U;
        persistedLayer.velocityMaximum = 97U;
        persistedLayer.gainDecibels = -4.5F;
        persistedLayer.pan = 0.625F;
        persistedLayer.tuningCents = -215.25F;
        persistedState.assets.push_back(ExternalAssetReference{
            "persistence-asset",
            "missing/source.wav",
            "source.wav",
            "WAV",
            "sha256:001122",
            123456U,
            -12345,
            2U,
            48000.0,
            96000U,
            768000U,
            true,
        });
        persistedState.midi.preferredInputIdentifier = "stable-midi-device";
        persistedState.midi.channelFilter = 10U;
        persistedState.audio.outputDeviceIdentifier = "stable-output-device";
        persistedState.audio.inputDeviceIdentifier = "stable-input-device";
        persistedState.audio.outputChannelMask = 3U;
        persistedState.audio.inputChannelMask = 1U;
        persistedState.audio.sampleRate = 48000.0;
        persistedState.audio.bufferSize = 256U;
        persistedState.ui.selectedBank = 3U;
        persistedState.ui.selectedPad = 12U;
        persistedState.ui.fixedTriggerVelocity = 87U;
        persistedState.ui.previewVolume = 0.45F;
        persistedState.ui.windowX = 111;
        persistedState.ui.windowY = 222;
        persistedState.ui.windowWidth = 1440;
        persistedState.ui.windowHeight = 900;
        expect(persisted.restoreState(persistedState, 42U).wasOk());

        const auto persistenceManifest = ProjectSerializer::canonicalManifest(persisted);
        expectEquals(persistenceManifest, ProjectSerializer::canonicalManifest(persisted));
        auto restored = Project::createEmpty("Unchanged", "unchanged-project");
        expect(ProjectSerializer::restoreCanonicalManifest(persistenceManifest, restored).wasOk());
        expect(restored.state() == persisted.state());
        expectEquals(static_cast<juce::int64>(restored.revision()), juce::int64{42});

        const auto persistenceDirectory =
            juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getNonexistentChildFile("padflow-m1-persistence", {}, true);
        expect(persistenceDirectory.createDirectory());
        const auto persistenceFile = persistenceDirectory.getChildFile("state.padflow");
        const auto persistenceSave = ProjectSerializer::save(persisted, persistenceFile);
        expect(persistenceSave.succeeded, persistenceSave.message);
        auto archiveRestored = Project::createEmpty();
        const auto persistenceLoad = ProjectSerializer::load(persistenceFile, archiveRestored);
        expect(persistenceLoad.wasOk(), persistenceLoad.getErrorMessage());
        expect(archiveRestored.state() == persisted.state());
        expectEquals(static_cast<juce::int64>(archiveRestored.revision()), juce::int64{42});
        persistenceDirectory.deleteRecursively();

        auto invalidManifest = persistenceManifest;
        invalidManifest =
            invalidManifest.replace("\"fixedTriggerVelocity\": 87", "\"fixedTriggerVelocity\": 0");
        expect(invalidManifest != persistenceManifest);
        const auto stateBeforeInvalidLoad = restored.state();
        const auto revisionBeforeInvalidLoad = restored.revision();
        expect(ProjectSerializer::restoreCanonicalManifest(invalidManifest, restored).failed());
        expect(restored.state() == stateBeforeInvalidLoad);
        expectEquals(static_cast<juce::int64>(restored.revision()),
                     static_cast<juce::int64>(revisionBeforeInvalidLoad));

        beginTest("SAVE-M1-009 Milestone 0 manifests load with default model state");
        const juce::String legacyManifest{"{\n"
                                          "  \"applicationVersion\": \"0.1.0\",\n"
                                          "  \"bundleIdentifier\": \"com.padflow.audio.padflow\",\n"
                                          "  \"company\": \"PadFlow Audio\",\n"
                                          "  \"format\": \"padflow-project\",\n"
                                          "  \"product\": \"PadFlow\",\n"
                                          "  \"projectName\": \"Legacy\",\n"
                                          "  \"projectUuid\": \"legacy-project\",\n"
                                          "  \"revision\": \"7\",\n"
                                          "  \"schemaVersion\": 1\n"
                                          "}\n"};
        auto legacy = Project::createEmpty();
        expect(ProjectSerializer::restoreCanonicalManifest(legacyManifest, legacy).wasOk());
        expectEquals(legacy.uuid(), juce::String{"legacy-project"});
        expectEquals(legacy.name(), juce::String{"Legacy"});
        expectEquals(static_cast<juce::int64>(legacy.revision()), juce::int64{7});
        expectEquals(static_cast<int>(legacy.state().banks.size()), static_cast<int>(padBankCount));
    }
};

static Milestone1ModelTests milestone1ModelTests;
} // namespace padflow
