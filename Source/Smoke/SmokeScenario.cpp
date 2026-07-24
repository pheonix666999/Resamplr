#include "SmokeScenario.h"

#include "App/ApplicationController.h"
#include "Audio/CaptureWriter.h"
#include "Audio/PlaybackStatePublisher.h"
#include "Input/InputRouter.h"
#include "Sampling/DerivedAsset.h"
#include "Sampling/RecordedAsset.h"
#include "Sampling/SampleImporter.h"
#include "Sampling/WaveformCache.h"
#include "Serialization/ProjectSerializer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

namespace padflow {
namespace {
bool writeSyntheticSample(const juce::File& file) {
    juce::WavAudioFormat format;
    auto stream = file.createOutputStream();
    if (stream == nullptr || !stream->openedOk())
        return false;
    std::unique_ptr<juce::OutputStream> output{std::move(stream)};
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(48000.0)
                             .withNumChannels(2)
                             .withBitsPerSample(24);
    auto writer = format.createWriterFor(output, options);
    if (writer == nullptr)
        return false;

    juce::AudioBuffer<float> buffer{2, 2048};
    for (int frame = 0; frame < buffer.getNumSamples(); ++frame) {
        const auto sample = 0.45F * std::sin(static_cast<float>(frame) * 0.075F);
        buffer.setSample(0, frame, sample);
        buffer.setSample(1, frame, sample * 0.75F);
    }
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

std::shared_ptr<const JobResult> awaitCompletion(BackgroundJobSystem& jobs) {
    std::shared_ptr<const JobResult> result;
    for (int attempt = 0; attempt < 1000 && result == nullptr; ++attempt) {
        juce::Thread::sleep(2);
        result = jobs.tryPopCompleted();
    }
    return result;
}

CaptureStatus awaitCapture(CaptureSession& capture) {
    for (int attempt = 0; attempt < 5000; ++attempt) {
        const auto status = capture.status();
        if (status.state == CaptureState::completed || status.state == CaptureState::failed ||
            status.state == CaptureState::cancelled)
            return status;
        juce::Thread::sleep(1);
    }
    return capture.status();
}

const ExternalAssetReference* findReference(const ProjectState& state,
                                            const juce::String& assetUuid) {
    const auto found =
        std::find_if(state.assets.begin(), state.assets.end(),
                     [&](const auto& reference) { return reference.uuid == assetUuid; });
    return found == state.assets.end() ? nullptr : std::addressof(*found);
}

bool resolveAllAssets(const ApplicationController& controller, SampleAssetRegistry& registry) {
    for (const auto& reference : controller.project().state().assets) {
        if (reference.missing)
            continue;
        const SampleImportRequest request{JobSpec{"padflow-resolve", reference.uuid,
                                                  controller.project().revision(), 0,
                                                  JobKind::sampleResolve},
                                          juce::File{reference.originalPath},
                                          reference.uuid,
                                          0U,
                                          0U,
                                          registry.budgetBytes()};
        CancellationToken token;
        JobProgress progress;
        const auto decoded = SampleImporter::decode(request, token, progress);
        const auto* payload =
            decoded != nullptr
                ? static_cast<const SampleImportPayload*>(decoded->immutablePayload.get())
                : nullptr;
        if (decoded == nullptr || !decoded->succeeded || payload == nullptr ||
            payload->asset == nullptr || !registry.publish(payload->asset))
            return false;
    }
    return true;
}

bool renderFiniteSignal(PlaybackEngine& engine, const std::size_t frames,
                        const bool requireSignal) {
    std::array<float, 256U> left{};
    std::array<float, 256U> right{};
    bool nonSilent = false;
    std::size_t rendered = 0U;
    while (rendered < frames) {
        const auto count = std::min(left.size(), frames - rendered);
        engine.processBlock(left.data(), right.data(), count);
        for (std::size_t frame = 0; frame < count; ++frame) {
            if (!std::isfinite(left[frame]) || !std::isfinite(right[frame]))
                return false;
            nonSilent =
                nonSilent || std::abs(left[frame]) > 0.00001F || std::abs(right[frame]) > 0.00001F;
        }
        rendered += count;
    }
    return !requireSignal || nonSilent;
}

SampleImportRequest makeImportRequest(const ApplicationController& controller,
                                      const juce::File& source, juce::String assetUuid) {
    return {
        JobSpec{controller.project().uuid(), controller.project().pad(0U).uuid,
                controller.project().revision(), 0, JobKind::sampleImport},
        source,
        std::move(assetUuid),
        0U,
        0U,
        defaultMilestone1DecodedBudgetBytes,
    };
}
} // namespace

SmokeResult runSmokeScenario() {
    auto temporaryDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getNonexistentChildFile("padflow-m2-smoke", {}, true);
    if (!temporaryDirectory.createDirectory())
        return {false, "SMOKE failure: could not create temporary directory"};
    const auto cleanup = juce::ScopeGuard([&] { temporaryDirectory.deleteRecursively(); });
    const auto sampleFile = temporaryDirectory.getChildFile("synthetic.wav");
    const auto projectFile = temporaryDirectory.getChildFile("populated.padflow");
    if (!writeSyntheticSample(sampleFile))
        return {false, "SMOKE failure: could not generate synthetic WAV"};
    juce::MemoryBlock originalSourceBytes;
    if (!sampleFile.loadFileAsData(originalSourceBytes))
        return {false, "SMOKE failure: could not fingerprint synthetic source"};

    ApplicationController controller;
    controller.createEmptyProject("Populated Smoke", "00000000-0000-4000-8000-000000000101");
    BackgroundJobSystem jobs{8U, 1U};
    SampleAssetRegistry assets{16U * 1024U * 1024U};
    const auto importHandle =
        SampleImporter::submit(jobs, makeImportRequest(controller, sampleFile, "smoke-asset"));
    if (!importHandle.has_value())
        return {false, "SMOKE failure: asynchronous import queue rejected the sample"};
    const auto imported = awaitCompletion(jobs);
    if (imported == nullptr || !imported->succeeded ||
        SampleImporter::commit(*imported, controller, assets).failed())
        return {false, "SMOKE failure: sample import/assignment failed"};

    const auto sourceAsset = assets.find(controller.project().pad(0U).layers[0].assetUuid);
    if (sourceAsset == nullptr)
        return {false, "SMOKE failure: imported immutable source is unavailable"};
    CancellationToken waveformToken;
    JobProgress waveformProgress;
    juce::String waveformError;
    const auto waveform =
        WaveformCache::generate(*sourceAsset, waveformToken, waveformProgress, waveformError);
    WaveformCacheRegistry waveformRegistry;
    if (waveform == nullptr || !waveformRegistry.publish(waveform))
        return {false, "SMOKE failure: waveform cache generation failed: " + waveformError};

    if (controller.setLayerTrim(0U, 0U, 128U, 1920U).failed() ||
        controller.setLayerLoop(0U, 0U, 256U, 512U).failed() ||
        controller.setLayerLoopEnabled(0U, 0U, true).failed() ||
        controller.setLayerReverseEnabled(0U, 0U, true).failed())
        return {false, "SMOKE failure: trim/reverse/loop editing failed"};

    PlaybackEngine editedEngine;
    editedEngine.prepare(48000.0);
    PlaybackStatePublisher editedPublisher{editedEngine, assets};
    editedPublisher.publish(controller.project().state());
    InputRouter editedInput{controller, editedEngine};
    if (!editedInput.mouseDown(0U) || !renderFiniteSignal(editedEngine, 2048U, true) ||
        editedEngine.activeVoiceCount() == 0U)
        return {false, "SMOKE failure: reverse loop playback did not remain finite and active"};
    editedInput.panic();
    juce::ignoreUnused(renderFiniteSignal(editedEngine, 64U, false));
    editedPublisher.clearWhenAudioIsStopped();

    const auto* sourceReference =
        findReference(controller.project().state(), sourceAsset->metadata().assetUuid);
    if (sourceReference == nullptr)
        return {false, "SMOKE failure: source metadata is unavailable"};
    auto submitDerived = [&](const DerivedAssetOperation operation) {
        const auto globalPad = std::size_t{0U};
        const auto layerIndex = std::size_t{0U};
        const auto& layer = controller.project().pad(globalPad).layers[layerIndex];
        const auto immutable = assets.find(layer.assetUuid);
        const auto* reference = findReference(controller.project().state(), layer.assetUuid);
        if (immutable == nullptr || reference == nullptr)
            return std::shared_ptr<const JobResult>{};
        DerivedAssetRequest request{
            JobSpec{controller.project().uuid(), controller.project().pad(globalPad).uuid,
                    controller.project().revision(), 0, JobKind::derivedAsset},
            immutable,
            *reference,
            resolveSamplePlaybackSettings(layer, reference->frameCount),
            operation,
            -1.0F,
            0U,
            temporaryDirectory.getChildFile("Derived"),
            "Assets/Derived",
            globalPad,
            layerIndex};
        if (!DerivedAssetRenderer::submit(jobs, std::move(request)).has_value())
            return std::shared_ptr<const JobResult>{};
        return awaitCompletion(jobs);
    };

    const auto normalized = submitDerived(DerivedAssetOperation::normalize);
    if (normalized == nullptr || !normalized->succeeded ||
        DerivedAssetRenderer::commit(*normalized, controller, assets).failed())
        return {false, "SMOKE failure: normalize render/assignment failed"};
    const auto cropped = submitDerived(DerivedAssetOperation::crop);
    if (cropped == nullptr || !cropped->succeeded ||
        DerivedAssetRenderer::commit(*cropped, controller, assets).failed())
        return {false, "SMOKE failure: crop render/assignment failed"};
    juce::MemoryBlock currentSourceBytes;
    if (!sampleFile.loadFileAsData(currentSourceBytes) || currentSourceBytes != originalSourceBytes)
        return {false, "SMOKE failure: derived operations changed source bytes"};

    auto ui = controller.project().state().ui;
    ui.selectedBank = 0U;
    ui.selectedPad = 0U;
    ui.fixedTriggerVelocity = 100U;
    if (controller.setUiState(ui).failed() || controller.setPadMappings(0U, 48U, "K").failed())
        return {false, "SMOKE failure: selection or input mapping failed"};
    auto parameters = controller.project().pad(0U).parameters;
    parameters.gainDecibels = -3.0F;
    parameters.pan = -0.2F;
    parameters.coarseSemitones = 2;
    parameters.fineCents = 12.5F;
    parameters.envelope = {0.0F, 0.01F, 0.8F, 0.01F};
    parameters.playbackMode = PlaybackMode::oneShot;
    if (controller.setPadParameters(0U, parameters).failed())
        return {false, "SMOKE failure: gain/pan/pitch/ADSR edit failed"};

    PlaybackEngine engine;
    engine.prepare(48000.0);
    PlaybackStatePublisher publisher{engine, assets};
    publisher.publish(controller.project().state());
    InputRouter input{controller, engine};
    if (!input.mouseDown(0U) || !renderFiniteSignal(engine, 512U, true) || !input.mouseUp(0U))
        return {false, "SMOKE failure: one-shot mouse playback was invalid"};
    // REGRESSION-M2-007: a looped one-shot intentionally survives mouse-up, so clear it before
    // measuring the lifetime of the following Gate voice.
    input.panic();
    if (!renderFiniteSignal(engine, 256U, false) || engine.activeVoiceCount() != 0U)
        return {false, "SMOKE failure: looped one-shot did not clear before Gate validation"};

    parameters.playbackMode = PlaybackMode::gate;
    if (controller.setPadParameters(0U, parameters).failed())
        return {false, "SMOKE failure: Gate edit failed"};
    publisher.publish(controller.project().state());
    if (!input.mouseDown(0U) || !renderFiniteSignal(engine, 256U, true) || !input.mouseUp(0U) ||
        !renderFiniteSignal(engine, 2048U, false) || engine.activeVoiceCount() != 0U)
        return {false, "SMOKE failure: Gate release left invalid or active output"};

    parameters.playbackMode = PlaybackMode::toggle;
    if (controller.setPadParameters(0U, parameters).failed())
        return {false, "SMOKE failure: Toggle edit failed"};
    publisher.publish(controller.project().state());
    if (!input.mouseDown(0U) || !renderFiniteSignal(engine, 128U, true) || !input.mouseUp(0U) ||
        !input.mouseDown(0U) || !input.mouseUp(0U) || !renderFiniteSignal(engine, 2048U, false) ||
        engine.activeVoiceCount() != 0U)
        return {false, "SMOKE failure: Toggle start/stop was invalid"};

    parameters.playbackMode = PlaybackMode::gate;
    if (controller.setPadParameters(0U, parameters).failed())
        return {false, "SMOKE failure: input mode reset failed"};
    publisher.publish(controller.project().state());
    input.refreshFromProject();
    if (!input.keyDown('K', false) || !renderFiniteSignal(engine, 128U, true) || !input.keyUp('K'))
        return {false, "SMOKE failure: keyboard route was invalid"};
    MidiSettings midi;
    midi.channelFilter = 2U;
    midi.preferredInputIdentifier = "synthetic-midi";
    if (controller.setMidiSettings(midi).failed())
        return {false, "SMOKE failure: MIDI settings edit failed"};
    input.refreshFromProject();
    if (!input.handleMidi(juce::MidiMessage::noteOn(2, 48, juce::uint8{110U})) ||
        !renderFiniteSignal(engine, 128U, true) ||
        !input.handleMidi(juce::MidiMessage::noteOff(2, 48)))
        return {false, "SMOKE failure: synthetic MIDI note route was invalid"};
    input.panic();
    if (!renderFiniteSignal(engine, 256U, false))
        return {false, "SMOKE failure: panic produced non-finite output"};

    const auto editedAssetUuid = controller.project().pad(0U).layers[0].assetUuid;
    const auto save = ProjectSerializer::save(controller.project(), projectFile);
    if (!save.succeeded)
        return {false, "SMOKE failure: " + save.message};
    auto loaded = Project::createEmpty();
    const auto load = ProjectSerializer::load(projectFile, loaded);
    if (load.failed() || loaded.state() != controller.project().state() ||
        loaded.pad(0U).layers[0].assetUuid != editedAssetUuid ||
        loaded.state().derivedAssets.size() != 2U ||
        !loaded.pad(0U).layers[0].playback.loopEnabled ||
        !loaded.pad(0U).layers[0].playback.reverseEnabled || loaded.pad(0U).keyboardKey != "K" ||
        loaded.pad(0U).midiNote != 48U)
        return {false, "SMOKE failure: populated schema-v1 round trip failed"};

    ApplicationController restoredController;
    if (restoredController.restoreProject(std::move(loaded)).failed())
        return {false, "SMOKE failure: restored controller rejected the project"};
    SampleAssetRegistry restoredAssets{16U * 1024U * 1024U};
    if (!resolveAllAssets(restoredController, restoredAssets))
        return {false, "SMOKE failure: restored project assets could not be resolved"};

    const auto captureFile = temporaryDirectory.getChildFile("mock-recording.wav");
    CaptureSession capture;
    CaptureSpec captureSpec;
    captureSpec.destination = captureFile;
    captureSpec.sampleRate = 1000.0;
    captureSpec.channels = 1U;
    captureSpec.maximumFramesPerBlock = 16U;
    captureSpec.fifoBlockCount = 250U;
    captureSpec.mode = CaptureMode::manual;
    captureSpec.sessionUuid = "smoke-recording-session";
    captureSpec.target = CaptureTarget{restoredController.project().uuid(),
                                       restoredController.project().pad(1U).uuid,
                                       restoredController.project().pad(1U).layers[0].uuid,
                                       restoredController.project().revision()};
    if (!capture.prepare(captureSpec) || !capture.startManual())
        return {false, "SMOKE failure: mocked capture could not arm/start"};
    std::array<float, 64U> capturedInput{};
    for (std::size_t frame = 0U; frame < capturedInput.size(); ++frame)
        capturedInput[frame] = 0.4F * std::sin(static_cast<float>(frame) * 0.11F);
    const std::array<const float*, 1U> capturedChannels{capturedInput.data()};
    capture.processInput(capturedChannels.data(), 1U,
                         static_cast<std::uint32_t>(capturedInput.size()));
    capture.requestStop();
    const auto captureStatus = awaitCapture(capture);
    if (captureStatus.state != CaptureState::completed || captureStatus.incomplete ||
        captureStatus.framesWritten != capturedInput.size() ||
        !capture.completedFile().existsAsFile())
        return {false, "SMOKE failure: mocked capture WAV finalization failed"};

    const auto captureTarget = capture.completedTarget();
    const RecordedAssetRequest recordedRequest{
        JobSpec{captureTarget.projectUuid, captureTarget.padUuid, captureTarget.projectRevision, 0,
                JobKind::recordedAsset},
        capture.completedFile(),
        captureSpec.sessionUuid,
        "smoke-recorded-asset",
        "Assets/Recorded/" + capture.completedFile().getFileName(),
        "mock-input",
        captureTarget.layerUuid,
        1U,
        0U,
        restoredAssets.budgetBytes()};
    CancellationToken recordedToken;
    JobProgress recordedProgress;
    const auto recorded =
        RecordedAssetPublisher::decode(recordedRequest, recordedToken, recordedProgress);
    if (recorded == nullptr || !recorded->succeeded ||
        RecordedAssetPublisher::commit(*recorded, restoredController, restoredAssets).failed())
        return {false, "SMOKE failure: completed recording could not assign to A2"};
    if (!restoredController.undo() ||
        !restoredController.project().pad(1U).layers[0].assetUuid.isEmpty() ||
        !restoredController.redo() ||
        restoredController.project().pad(1U).layers[0].assetUuid != "smoke-recorded-asset")
        return {false, "SMOKE failure: recording assignment undo/redo failed"};

    PlaybackEngine restoredEngine;
    restoredEngine.prepare(48000.0);
    PlaybackStatePublisher restoredPublisher{restoredEngine, restoredAssets};
    restoredPublisher.publish(restoredController.project().state());
    InputRouter restoredInput{restoredController, restoredEngine};
    if (!restoredInput.keyDown('K', false) || !renderFiniteSignal(restoredEngine, 512U, true) ||
        !restoredInput.keyUp('K'))
        return {false, "SMOKE failure: edited A1 did not retrigger"};
    if (!restoredInput.mouseDown(1U) || !renderFiniteSignal(restoredEngine, 128U, true) ||
        !restoredInput.mouseUp(1U))
        return {false, "SMOKE failure: recorded A2 did not retrigger"};

    const auto finalSave = ProjectSerializer::save(restoredController.project(), projectFile);
    auto finalLoaded = Project::createEmpty();
    if (!finalSave.succeeded || ProjectSerializer::load(projectFile, finalLoaded).failed() ||
        finalLoaded.state() != restoredController.project().state() ||
        finalLoaded.state().recordedAssets.size() != 1U)
        return {false, "SMOKE failure: final Milestone 2 semantic round trip failed"};

    restoredInput.panic();
    renderFiniteSignal(restoredEngine, 256U, false);
    publisher.clearWhenAudioIsStopped();
    restoredPublisher.clearWhenAudioIsStopped();
    capture.shutdown();
    jobs.shutdown();
    assets.clear();
    restoredAssets.clear();
    juce::ignoreUnused(cleanup);
    return {true,
            "SMOKE success: import, waveform cache, trim/reverse/loop, normalize/crop, schema-v1 "
            "round trip, mocked WAV capture, A2 assignment, retrigger, undo/redo, finite render, "
            "and temporary cleanup passed"};
}
} // namespace padflow
