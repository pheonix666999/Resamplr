#include "App/SamplerView.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <memory>

namespace padflow {
namespace {
bool writeUiFixture(const juce::File& file, const float amplitude) {
    juce::WavAudioFormat format;
    auto stream = file.createOutputStream();
    if (stream == nullptr || !stream->openedOk())
        return false;
    std::unique_ptr<juce::OutputStream> output{std::move(stream)};
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(48000.0)
                             .withNumChannels(1)
                             .withBitsPerSample(24);
    auto writer = format.createWriterFor(output, options);
    if (writer == nullptr)
        return false;
    juce::AudioBuffer<float> buffer{1, 256};
    for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        buffer.setSample(0, frame, amplitude * std::sin(static_cast<float>(frame) * 0.1F));
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

bool awaitUiImport(SamplerView& view, ApplicationController& controller,
                   const std::uint64_t originalRevision) {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        juce::Thread::sleep(2);
        view.processPendingJobs();
        if (controller.project().revision() > originalRevision &&
            controller.project().pad(0U).layers[0].assetUuid.isNotEmpty())
            return true;
    }
    return false;
}
} // namespace

class Milestone1UiTests final : public juce::UnitTest {
  public:
    Milestone1UiTests() : juce::UnitTest("Milestone 1 sampler UI", "PadFlow") {}

    void runTest() override {
        ApplicationController controller;
        controller.createEmptyProject("UI Test", "ui-project");
        BackgroundJobSystem jobs{8U, 1U};
        SampleAssetRegistry assets{8U * 1024U * 1024U};
        AudioRuntime runtime;
        runtime.engine().prepare(48000.0);
        runtime.preview().prepare(48000.0);
        PlaybackStatePublisher publisher{runtime.engine(), assets};
        InputRouter input{controller, runtime.engine()};
        SamplePreviewController preview{controller, runtime.preview()};
        SamplerView view{controller, jobs, assets, runtime, publisher, input, preview};
        view.setBounds(0, 0, 1180, 760);

        beginTest("UIHEADLESS-M1-001 main sampler view exposes accessible named controls");
        expectEquals(view.visiblePadCount(), padsPerBank);
        for (const auto& id : {"new-project", "open-project", "save-project", "audio-settings",
                               "midi-settings", "layer-selector", "pad-name", "keyboard-mapping"})
            expect(view.findChildWithID(id) != nullptr, juce::String{"Missing control: "} + id);
        for (std::size_t pad = 0; pad < padsPerBank; ++pad) {
            const auto* component =
                view.findChildWithID("pad-" + juce::String{static_cast<int>(pad)});
            expect(component != nullptr);
            if (component != nullptr)
                expect(component->getTitle().isNotEmpty());
        }

        beginTest("UIHEADLESS-M1-002 and UIHEADLESS-M1-003 visit every bank and pad");
        for (std::size_t bank = 0; bank < padBankCount; ++bank) {
            expect(view.selectBank(bank));
            expectEquals(static_cast<int>(controller.project().state().ui.selectedBank),
                         static_cast<int>(bank));
            for (std::size_t pad = 0; pad < padsPerBank; ++pad) {
                const auto global = bank * padsPerBank + pad;
                expect(view.selectPad(global));
                expectEquals(static_cast<int>(view.selectedGlobalPad()), static_cast<int>(global));
            }
        }
        expect(!view.selectBank(padBankCount));
        expect(!view.selectPad(totalPadCount));

        beginTest("UIHEADLESS-M1-004 and UIHEADLESS-M1-010 generated import and sequential drop");
        const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getNonexistentChildFile("padflow-ui-headless", {}, true);
        expect(directory.createDirectory());
        const auto first = directory.getChildFile("first.wav");
        const auto second = directory.getChildFile("second.wav");
        expect(writeUiFixture(first, 0.5F));
        expect(writeUiFixture(second, 0.25F));
        expect(view.selectPad(0U));
        juce::StringArray files;
        files.add(first.getFullPathName());
        files.add(second.getFullPathName());
        const auto beforeImport = controller.project().revision();
        expect(view.queueImportFiles(files, 0U, true));
        expect(awaitUiImport(view, controller, beforeImport));
        for (int attempt = 0;
             attempt < 1000 && controller.project().pad(1U).layers[0].assetUuid.isEmpty();
             ++attempt) {
            juce::Thread::sleep(2);
            view.processPendingJobs();
        }
        expect(controller.project().pad(0U).layers[0].assetUuid.isNotEmpty());
        expect(controller.project().pad(1U).layers[0].assetUuid.isNotEmpty());
        expectEquals(static_cast<int>(assets.uniqueAssetCount()), 2);

        beginTest("UIHEADLESS-M1-005 loaded A1 triggers finite non-silence");
        expect(view.selectBank(0U));
        expect(input.mouseDown(0U));
        std::array<float, 128U> left{};
        std::array<float, 128U> right{};
        runtime.engine().processBlock(left.data(), right.data(), left.size());
        bool nonSilent = false;
        for (std::size_t frame = 0; frame < left.size(); ++frame) {
            expect(std::isfinite(left[frame]) && std::isfinite(right[frame]));
            nonSilent = nonSilent || std::abs(left[frame]) > 0.00001F;
        }
        expect(nonSilent);
        juce::ignoreUnused(input.mouseUp(0U));

        beginTest("UIHEADLESS-M1-007 audio-disabled model remains functional");
        expect(controller.renamePad(0U, "Headless Kick").wasOk());
        expectEquals(controller.project().pad(0U).name, juce::String{"Headless Kick"});

        beginTest("UIHEADLESS-M1-008 and UIHEADLESS-M1-009 mappings update without hardware");
        expect(controller.setPadMappings(0U, 48U, "K").wasOk());
        MidiSettings midi;
        midi.channelFilter = 4U;
        midi.preferredInputIdentifier = "synthetic-midi";
        expect(controller.setMidiSettings(midi).wasOk());
        input.refreshFromProject();
        expect(input.keyDown('K', false));
        expect(input.keyUp('K'));
        expect(!input.handleMidi(juce::MidiMessage::noteOn(3, 48, juce::uint8{100U})));
        expect(input.handleMidi(juce::MidiMessage::noteOn(4, 48, juce::uint8{100U})));

        beginTest("REGRESSION-M1-003 live snapshots retire only after audio acknowledgement");
        input.panic();
        runtime.engine().processBlock(left.data(), right.data(), left.size());
        publisher.publish(controller.project().state());
        publisher.publish(controller.project().state());
        expect(publisher.retainedSnapshotCount() >= 2U);
        runtime.engine().processBlock(left.data(), right.data(), left.size());
        expect(publisher.collectAcknowledged() > 0U);
        expectEquals(static_cast<int>(publisher.retainedSnapshotCount()), 1);

        beginTest("REGRESSION-M1-005 active voices retain immutable sample ownership");
        const auto activeAssetUuid = controller.project().pad(0U).layers[0].assetUuid;
        const std::weak_ptr<const SampleAsset> activeAsset = assets.find(activeAssetUuid);
        expect(!activeAsset.expired());
        expect(input.mouseDown(0U));
        runtime.engine().processBlock(left.data(), right.data(), 1U);
        expect(runtime.engine().activeVoiceCount() > 0U);
        expect(controller.clearPad(0U).wasOk());
        publisher.publish(controller.project().state());
        assets.clear();
        runtime.engine().processBlock(left.data(), right.data(), 1U);
        juce::ignoreUnused(publisher.collectAcknowledged());
        expect(!activeAsset.expired());
        input.panic();
        runtime.engine().processBlock(left.data(), right.data(), left.size());
        expect(publisher.collectAcknowledged() > 0U);
        expect(activeAsset.expired());

        beginTest("UIHEADLESS-M1-011 minimum layout keeps named controls inside bounds");
        for (const auto& id :
             {"new-project", "save-project", "pad-name", "layer-selector", "pad-0", "pad-15"}) {
            const auto* component = view.findChildWithID(id);
            expect(component != nullptr);
            if (component != nullptr)
                expect(view.getLocalBounds().contains(component->getBounds()));
        }

        input.panic();
        runtime.engine().processBlock(left.data(), right.data(), left.size());
        jobs.shutdown();
        publisher.clearWhenAudioIsStopped();
        assets.clear();
        expect(directory.deleteRecursively());
    }
};

static Milestone1UiTests milestone1UiTests;
} // namespace padflow
