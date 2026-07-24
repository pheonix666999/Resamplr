#include "App/SamplerView.h"
#include "App/WaveformEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <memory>

namespace padflow {
namespace {
bool writeWaveformUiFixture(const juce::File& file) {
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
    juce::AudioBuffer<float> buffer{2, 512};
    for (int frame = 0; frame < buffer.getNumSamples(); ++frame) {
        const auto value = 0.6F * std::sin(static_cast<float>(frame) * 0.08F);
        buffer.setSample(0, frame, value);
        buffer.setSample(1, frame, -value * 0.5F);
    }
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

bool waitForUiAsset(SamplerView& view, ApplicationController& controller,
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

class Milestone2UiTests final : public juce::UnitTest {
  public:
    Milestone2UiTests() : juce::UnitTest("Milestone 2 waveform UI", "PadFlow") {}

    void runTest() override {
        ApplicationController controller;
        controller.createEmptyProject("Waveform UI Test", "waveform-ui-project");
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

        beginTest("UIHEADLESS-M2-001 constructs accessible waveform editor controls");
        for (const auto& id :
             {"waveform-editor", "waveform-info", "waveform-audition", "waveform-stop",
              "waveform-loop", "waveform-reverse", "waveform-zero-crossing", "waveform-fit",
              "waveform-fit-selection", "waveform-reset-trim", "waveform-reset-loop"}) {
            const auto* component = view.findChildWithID(id);
            expect(component != nullptr, juce::String{"Missing control: "} + id);
            if (component != nullptr)
                expect(view.getLocalBounds().contains(component->getBounds()));
        }

        const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getNonexistentChildFile("padflow-m2-ui", {}, true);
        expect(directory.createDirectory());
        const auto sample = directory.getChildFile("waveform-ui.wav");
        expect(writeWaveformUiFixture(sample));
        juce::StringArray files;
        files.add(sample.getFullPathName());
        const auto revision = controller.project().revision();
        expect(view.queueImportFiles(files, 0U, true));
        expect(waitForUiAsset(view, controller, revision));

        beginTest("UIHEADLESS-M2-002 selects an assigned sample layer");
        expect(view.selectPad(0U));
        const auto* editor =
            dynamic_cast<const WaveformEditor*>(view.findChildWithID("waveform-editor"));
        expect(editor != nullptr);
        if (editor != nullptr)
            expectEquals(static_cast<int>(editor->frameCount()), 512);

        beginTest("UIHEADLESS-M2-003 updates a valid trim through the UI facade");
        expect(view.editSelectedTrim(32U, 480U));
        const auto& trimmed = controller.project().pad(0U).layers[0].playback;
        expectEquals(static_cast<int>(trimmed.startFrame), 32);
        expectEquals(static_cast<int>(trimmed.endFrame), 480);

        beginTest("UIHEADLESS-M2-004 rejects an invalid trim without publishing it");
        const auto beforeInvalid = controller.project().revision();
        expect(!view.editSelectedTrim(480U, 32U));
        expectEquals(static_cast<juce::int64>(controller.project().revision()),
                     static_cast<juce::int64>(beforeInvalid));
        expectEquals(static_cast<int>(controller.project().pad(0U).layers[0].playback.startFrame),
                     32);

        beginTest("UIHEADLESS-M2-005 toggles a valid loop");
        expect(view.editSelectedLoop(64U, 256U));
        expect(view.setSelectedLoopEnabled(true));
        expect(controller.project().pad(0U).layers[0].playback.loopEnabled);

        beginTest("UIHEADLESS-M2-006 toggles reverse for new triggers");
        expect(view.setSelectedReverseEnabled(true));
        expect(controller.project().pad(0U).layers[0].playback.reverseEnabled);

        for (int attempt = 0; attempt < 1000; ++attempt) {
            juce::Thread::sleep(1);
            view.processPendingJobs();
            const auto* liveEditor =
                dynamic_cast<const WaveformEditor*>(view.findChildWithID("waveform-editor"));
            if (liveEditor != nullptr && liveEditor->hasWaveform())
                break;
        }
        const auto* analysedEditor =
            dynamic_cast<const WaveformEditor*>(view.findChildWithID("waveform-editor"));
        expect(analysedEditor != nullptr && analysedEditor->hasWaveform());

        input.panic();
        jobs.shutdown();
        publisher.clearWhenAudioIsStopped();
        assets.clear();
        expect(directory.deleteRecursively());
    }
};

static Milestone2UiTests milestone2UiTests;
} // namespace padflow
