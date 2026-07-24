#include "SmokeScenario.h"

#include "App/ApplicationController.h"
#include "Audio/PlaybackStatePublisher.h"
#include "Input/InputRouter.h"
#include "Sampling/SampleImporter.h"
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
                                  .getNonexistentChildFile("padflow-m1-smoke", {}, true);
    if (!temporaryDirectory.createDirectory())
        return {false, "SMOKE failure: could not create temporary directory"};
    const auto cleanup = juce::ScopeGuard([&] { temporaryDirectory.deleteRecursively(); });
    const auto sampleFile = temporaryDirectory.getChildFile("synthetic.wav");
    const auto projectFile = temporaryDirectory.getChildFile("populated.padflow");
    if (!writeSyntheticSample(sampleFile))
        return {false, "SMOKE failure: could not generate synthetic WAV"};

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

    const auto save = ProjectSerializer::save(controller.project(), projectFile);
    if (!save.succeeded)
        return {false, "SMOKE failure: " + save.message};
    auto loaded = Project::createEmpty();
    const auto load = ProjectSerializer::load(projectFile, loaded);
    if (load.failed() || loaded.state() != controller.project().state() ||
        loaded.pad(0U).layers[0].assetUuid != "smoke-asset" || loaded.pad(0U).keyboardKey != "K" ||
        loaded.pad(0U).midiNote != 48U)
        return {false, "SMOKE failure: populated schema-v1 round trip failed"};

    ApplicationController restoredController;
    if (restoredController.restoreProject(std::move(loaded)).failed())
        return {false, "SMOKE failure: restored controller rejected the project"};
    SampleAssetRegistry restoredAssets{16U * 1024U * 1024U};
    CancellationToken restoreToken;
    JobProgress restoreProgress;
    const auto restoredDecode =
        SampleImporter::decode(makeImportRequest(restoredController, sampleFile, "smoke-asset"),
                               restoreToken, restoreProgress);
    const auto* restoredPayload =
        restoredDecode != nullptr
            ? static_cast<const SampleImportPayload*>(restoredDecode->immutablePayload.get())
            : nullptr;
    if (restoredDecode == nullptr || !restoredDecode->succeeded || restoredPayload == nullptr ||
        restoredPayload->asset == nullptr || !restoredAssets.publish(restoredPayload->asset))
        return {false, "SMOKE failure: restored external sample could not be resolved"};

    PlaybackEngine restoredEngine;
    restoredEngine.prepare(48000.0);
    PlaybackStatePublisher restoredPublisher{restoredEngine, restoredAssets};
    restoredPublisher.publish(restoredController.project().state());
    InputRouter restoredInput{restoredController, restoredEngine};
    if (!restoredInput.keyDown('K', false) || !renderFiniteSignal(restoredEngine, 512U, true) ||
        !restoredInput.keyUp('K'))
        return {false, "SMOKE failure: restored project did not retrigger"};

    restoredInput.panic();
    renderFiniteSignal(restoredEngine, 256U, false);
    publisher.clearWhenAudioIsStopped();
    restoredPublisher.clearWhenAudioIsStopped();
    jobs.shutdown();
    assets.clear();
    restoredAssets.clear();
    juce::ignoreUnused(cleanup);
    return {true,
            "SMOKE success: generated import, parameters, mouse/keyboard/MIDI modes, populated "
            "schema-v1 save/load, external resolution, and restored playback passed"};
}
} // namespace padflow
