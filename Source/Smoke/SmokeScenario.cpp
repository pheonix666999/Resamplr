#include "SmokeScenario.h"

#include "App/ApplicationController.h"
#include "Audio/CaptureWriter.h"
#include "Audio/PlaybackStatePublisher.h"
#include "Audio/SliceAuditionController.h"
#include "Chopping/AssignmentPlan.h"
#include "Chopping/ChoppingSession.h"
#include "Chopping/LazyMarkerCapture.h"
#include "Chopping/TransientAnalysis.h"
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
    buffer.clear();
    for (int frame = 0; frame < buffer.getNumSamples(); ++frame) {
        const auto bed = 0.025F * std::sin(static_cast<float>(frame) * 0.037F);
        buffer.setSample(0, frame, bed);
        buffer.setSample(1, frame, bed * 0.7F);
    }
    for (const auto onset : {384, 832, 1280, 1728}) {
        for (int offset = 0; offset < 64 && onset + offset < buffer.getNumSamples(); ++offset) {
            const auto decay = std::exp(-static_cast<float>(offset) * 0.065F);
            buffer.addSample(0, onset + offset, decay * 0.9F);
            buffer.addSample(1, onset + offset, decay * -0.65F);
        }
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

    auto occupiedLayer = controller.project().pad(0U).layers[0U];
    occupiedLayer.uuid = controller.project().pad(1U).layers[0U].uuid;
    if (controller.setLayer(1U, 0U, occupiedLayer).failed())
        return {false, "SMOKE failure: occupied chopping destination setup failed"};
    const auto destinationBaseline = controller.project().state();
    const auto sourceRevision = controller.project().revision();
    ChoppingSession chopping;
    const ChoppingSessionTarget choppingTarget{"smoke-chopping-session",
                                               controller.project().uuid(),
                                               controller.project().pad(0U).uuid,
                                               controller.project().pad(0U).layers[0U].uuid,
                                               sourceRevision,
                                               sourceAsset->metadata().assetUuid,
                                               sourceReference->contentFingerprint,
                                               128,
                                               1920};
    if (chopping.begin(choppingTarget).failed() || chopping.regenerateEqual(4).failed())
        return {false, "SMOKE failure: four-slice equal chopping failed"};
    const std::array<std::int64_t, 5U> equalBoundaries{128, 576, 1024, 1472, 1920};
    const auto& equalSlices = chopping.provisionalSliceSet()->slices;
    if (equalSlices.size() != 4U)
        return {false, "SMOKE failure: equal chopping returned the wrong slice count"};
    for (std::size_t index = 0U; index < equalSlices.size(); ++index)
        if (equalSlices[index].startFrame != equalBoundaries[index] ||
            equalSlices[index].endFrame != equalBoundaries[index + 1U])
            return {false, "SMOKE failure: equal chopping boundaries were not exact"};

    PreviewPlayer slicePlayer;
    slicePlayer.prepare(48000.0);
    SliceAuditionController sliceAudition{slicePlayer};
    for (const auto& slice : equalSlices) {
        if (!sliceAudition.startSelected(sourceAsset, slice))
            return {false, "SMOKE failure: selected-slice audition command was rejected"};
        std::array<float, 512U> sliceLeft{};
        std::array<float, 512U> sliceRight{};
        slicePlayer.processAdd(sliceLeft.data(), sliceRight.data(), sliceLeft.size());
        if (!std::all_of(sliceLeft.begin(), sliceLeft.end(),
                         [](const auto value) { return std::isfinite(value); }) ||
            !std::any_of(sliceLeft.begin(), sliceLeft.end(),
                         [](const auto value) { return std::abs(value) > 0.00001F; }))
            return {false, "SMOKE failure: slice audition was silent or non-finite"};
        sliceAudition.service();
    }

    if (chopping.regenerateFixed(500, SliceRemainderPolicy::include, SliceDisplayUnit::frames)
            .failed()) {
        return {false, "SMOKE failure: fixed-length chopping failed"};
    }
    const auto& fixedSlices = chopping.provisionalSliceSet()->slices;
    if (fixedSlices.size() != 4U || fixedSlices.back().startFrame != 1628 ||
        fixedSlices.back().endFrame != 1920)
        return {false, "SMOKE failure: fixed-length remainder slice was incorrect"};

    const TransientAnalysisRequest transientRequest{
        {controller.project().uuid(), sourceAsset->metadata().assetUuid, sourceRevision, 0,
         JobKind::transientAnalysis},
        {makeStableUuid("smoke-chopping-session:slice-set"), sourceAsset->metadata().assetUuid,
         sourceReference->contentFingerprint, controller.project().pad(0U).layers[0U].uuid, 128,
         1920, 1, SliceRemainderPolicy::include, SliceDisplayUnit::frames},
        {0.5F, 128, 0, 0.0F},
        sourceAsset};
    CancellationToken transientToken;
    JobProgress transientProgress;
    const auto transient =
        TransientAnalysis::analyse(transientRequest, transientToken, transientProgress);
    CancellationToken repeatTransientToken;
    JobProgress repeatTransientProgress;
    const auto repeatTransient =
        TransientAnalysis::analyse(transientRequest, repeatTransientToken, repeatTransientProgress);
    const auto* transientPayload =
        transient != nullptr
            ? static_cast<const TransientAnalysisPayload*>(transient->immutablePayload.get())
            : nullptr;
    const auto* repeatTransientPayload =
        repeatTransient != nullptr
            ? static_cast<const TransientAnalysisPayload*>(repeatTransient->immutablePayload.get())
            : nullptr;
    if (transient == nullptr || repeatTransient == nullptr || !transient->succeeded ||
        !repeatTransient->succeeded || transientPayload == nullptr ||
        repeatTransientPayload == nullptr ||
        transientPayload->sliceSet != repeatTransientPayload->sliceSet ||
        transientPayload->sliceSet.slices.size() < 4U ||
        chopping.acceptTransientResult(*transient).failed())
        return {false, "SMOKE failure: deterministic transient chopping failed"};

    const auto firstTransientBoundary = chopping.provisionalSliceSet()->slices.front().endFrame;
    if (chopping.moveMarker(firstTransientBoundary, firstTransientBoundary + 5).failed())
        return {false, "SMOKE failure: manual transient-marker adjustment failed"};

    LazyMarkerCapture lazyCapture;
    if (lazyCapture.start({128, 1920, 32, 16}).failed() ||
        !lazyCapture.captureFromAudioThread(704, LazyMarkerSource::mouse) ||
        !lazyCapture.captureFromAudioThread(1155, LazyMarkerSource::keyboard) ||
        !lazyCapture.captureFromAudioThread(1602, LazyMarkerSource::midi)) {
        return {false, "SMOKE failure: simulated lazy marker capture failed"};
    }
    lazyCapture.stop();
    const auto lazyDrain = lazyCapture.drainToSession(chopping);
    if (lazyDrain.accepted != 3U || lazyDrain.rejected != 0U ||
        chopping.markCurrentSetLazy(32, 16).failed())
        return {false, "SMOKE failure: lazy markers did not commit provisionally"};

    AssignmentRequest assignmentRequest{choppingTarget.sessionUuid,
                                        choppingTarget.projectUuid,
                                        sourceRevision,
                                        choppingTarget.targetPadUuid,
                                        choppingTarget.targetLayerUuid,
                                        choppingTarget.sourceAssetUuid,
                                        choppingTarget.sourceFingerprint,
                                        *chopping.provisionalSliceSet(),
                                        AssignmentDestinationMode::consecutivePads,
                                        1U,
                                        0U,
                                        false,
                                        false};
    AssignmentPlan assignmentPlan;
    if (buildAssignmentPlan(controller.project().state(), assignmentRequest, assignmentPlan)
            .failed() ||
        assignmentPlan.destinations.empty() || !assignmentPlan.destinations.front().occupied ||
        !assignmentPlan.hasUnresolvedConflicts())
        return {false, "SMOKE failure: occupied assignment preview was incomplete"};
    for (std::size_t index = 0U; index < assignmentPlan.destinations.size(); ++index) {
        if (!assignmentPlan.destinations[index].occupied)
            continue;
        AssignmentPlan resolved;
        if (setAssignmentDecision(assignmentPlan, index, AssignmentConflictDecision::replace,
                                  resolved)
                .failed())
            return {false, "SMOKE failure: overwrite decision could not be resolved"};
        assignmentPlan = std::move(resolved);
    }
    AssignmentCommitReport assignmentReport;
    if (controller.commitSliceAssignment(assignmentPlan, assignmentReport).failed() ||
        assignmentReport.assignedSliceUuids.size() != assignmentPlan.destinations.size())
        return {false, "SMOKE failure: transactional slice assignment failed"};

    // REGRESSION-M3-004: the shared scenario already keeps three full fixed voice pools alive.
    // Keep this additional integration engine off the smaller default Windows process stack.
    auto choppedEngine = std::make_unique<PlaybackEngine>();
    choppedEngine->prepare(48000.0);
    PlaybackStatePublisher choppedPublisher{*choppedEngine, assets};
    choppedPublisher.publish(controller.project().state());
    InputRouter choppedInput{controller, *choppedEngine};
    for (const auto& destination : assignmentPlan.destinations) {
        if (!choppedInput.mouseDown(destination.globalPadIndex % padsPerBank) ||
            !renderFiniteSignal(*choppedEngine, 512U, true) ||
            !choppedInput.mouseUp(destination.globalPadIndex % padsPerBank))
            return {false, "SMOKE failure: assigned slice did not trigger finite non-silence"};
        choppedInput.panic();
        juce::ignoreUnused(renderFiniteSignal(*choppedEngine, 128U, false));
    }
    if (!controller.undo() || controller.project().state() != destinationBaseline)
        return {false, "SMOKE failure: slice assignment undo did not restore every destination"};
    if (!controller.redo() || controller.project().state().sliceSets.size() != 1U)
        return {false, "SMOKE failure: slice assignment redo was not deterministic"};
    const auto committedChoppingState = controller.project().state();
    ChoppingSession cancelledChopping;
    auto cancelTarget = choppingTarget;
    cancelTarget.sessionUuid = "smoke-cancelled-chopping-session";
    cancelTarget.targetRevision = controller.project().revision();
    if (cancelledChopping.begin(cancelTarget).failed() ||
        cancelledChopping.regenerateEqual(3).failed())
        return {false, "SMOKE failure: second provisional chopping session could not start"};
    cancelledChopping.cancel();
    if (controller.project().state() != committedChoppingState)
        return {false, "SMOKE failure: cancelled chopping session mutated the project"};
    choppedPublisher.clearWhenAudioIsStopped();

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
                                       restoredController.project().pad(9U).uuid,
                                       restoredController.project().pad(9U).layers[0].uuid,
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
        9U,
        0U,
        restoredAssets.budgetBytes()};
    CancellationToken recordedToken;
    JobProgress recordedProgress;
    const auto recorded =
        RecordedAssetPublisher::decode(recordedRequest, recordedToken, recordedProgress);
    if (recorded == nullptr || !recorded->succeeded ||
        RecordedAssetPublisher::commit(*recorded, restoredController, restoredAssets).failed())
        return {false, "SMOKE failure: completed recording could not assign to A10"};
    if (!restoredController.undo() ||
        !restoredController.project().pad(9U).layers[0].assetUuid.isEmpty() ||
        !restoredController.redo() ||
        restoredController.project().pad(9U).layers[0].assetUuid != "smoke-recorded-asset")
        return {false, "SMOKE failure: recording assignment undo/redo failed"};

    PlaybackEngine restoredEngine;
    restoredEngine.prepare(48000.0);
    PlaybackStatePublisher restoredPublisher{restoredEngine, restoredAssets};
    restoredPublisher.publish(restoredController.project().state());
    InputRouter restoredInput{restoredController, restoredEngine};
    if (!restoredInput.keyDown('K', false) || !renderFiniteSignal(restoredEngine, 512U, true) ||
        !restoredInput.keyUp('K'))
        return {false, "SMOKE failure: edited A1 did not retrigger"};
    if (!restoredInput.mouseDown(9U) || !renderFiniteSignal(restoredEngine, 128U, true) ||
        !restoredInput.mouseUp(9U))
        return {false, "SMOKE failure: recorded A10 did not retrigger"};
    if (!restoredInput.mouseDown(1U) || !renderFiniteSignal(restoredEngine, 512U, true) ||
        !restoredInput.mouseUp(1U))
        return {false, "SMOKE failure: restored chopped A2 did not retrigger"};

    const auto finalSave = ProjectSerializer::save(restoredController.project(), projectFile);
    auto finalLoaded = Project::createEmpty();
    if (!finalSave.succeeded || ProjectSerializer::load(projectFile, finalLoaded).failed() ||
        finalLoaded.state() != restoredController.project().state() ||
        finalLoaded.state().recordedAssets.size() != 1U ||
        finalLoaded.state().sliceSets.size() != 1U)
        return {false, "SMOKE failure: final Milestone 3 semantic round trip failed"};

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
            "SMOKE success: import, waveform cache, trim/reverse/loop, equal/fixed/transient/"
            "manual/lazy chopping, bounded audition, occupied preview, transactional assignment, "
            "normalize/crop, schema-v1 round trip, mocked WAV capture, retrigger, undo/redo, "
            "finite render, cancellation, and temporary cleanup passed"};
}
} // namespace padflow
