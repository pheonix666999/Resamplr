#include "App/SamplerView.h"
#include "App/WaveformEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
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

juce::Component* findDescendantWithId(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren()) {
        if (auto* match = findDescendantWithId(*child, id))
            return match;
    }
    return nullptr;
}

bool writeUiEvidence(juce::Component& component, const juce::File& file) {
    const auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 1.0F);
    auto stream = file.createOutputStream();
    return image.isValid() && stream != nullptr && stream->openedOk() &&
           juce::PNGImageFormat{}.writeImageToStream(image, *stream);
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

        beginTest("UIHEADLESS-M2-001 and REGRESSION-M2-005 find nested controls headlessly");
        for (const auto& id :
             {"waveform-editor",        "waveform-info",         "waveform-audition",
              "waveform-stop",          "waveform-loop",         "waveform-reverse",
              "waveform-zero-crossing", "waveform-fit",          "waveform-fit-selection",
              "waveform-reset-trim",    "waveform-reset-loop",   "waveform-process",
              "recording-panel-toggle", "recording-panel",       "recording-input-device",
              "recording-input-meter",  "recording-mode",        "recording-threshold",
              "recording-preroll",      "recording-bank",        "recording-pad",
              "recording-layer",        "recording-auto-assign", "recording-file-name",
              "recording-arm",          "recording-start",       "recording-stop",
              "recording-cancel",       "recording-assign"}) {
            const auto* component = findDescendantWithId(view, id);
            expect(component != nullptr, juce::String{"Missing control: "} + id);
            if (component != nullptr && component->getParentComponent() == &view)
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
            dynamic_cast<const WaveformEditor*>(findDescendantWithId(view, "waveform-editor"));
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
                dynamic_cast<const WaveformEditor*>(findDescendantWithId(view, "waveform-editor"));
            if (liveEditor != nullptr && liveEditor->hasWaveform())
                break;
        }
        const auto* analysedEditor =
            dynamic_cast<const WaveformEditor*>(findDescendantWithId(view, "waveform-editor"));
        expect(analysedEditor != nullptr && analysedEditor->hasWaveform());
        const auto evidenceDirectoryPath =
            juce::SystemStats::getEnvironmentVariable("PADFLOW_SCREENSHOT_DIR", {});
        const auto evidenceDirectory = juce::File{evidenceDirectoryPath};
        if (evidenceDirectoryPath.isNotEmpty()) {
            expect(evidenceDirectory.createDirectory());
            expect(writeUiEvidence(view,
                                   evidenceDirectory.getChildFile("padflow-waveform-editor.png")));
        }

        beginTest("UIHEADLESS-M2-007 derived operation reports and commits");
        const auto sourceAssetUuid = controller.project().pad(0U).layers[0].assetUuid;
        expect(view.submitDerivedOperation(DerivedAssetOperation::normalize));
        for (int attempt = 0; attempt < 2000; ++attempt) {
            juce::Thread::sleep(2);
            view.processPendingJobs();
            if (controller.project().pad(0U).layers[0].assetUuid != sourceAssetUuid)
                break;
        }
        expect(controller.project().pad(0U).layers[0].assetUuid != sourceAssetUuid);
        expectEquals(static_cast<int>(controller.project().state().derivedAssets.size()), 1);

        beginTest("UIHEADLESS-M2-008 recording controls traverse explicit states");
        view.setRecordingPanelVisible(true);
        const auto recordingFile = directory.getChildFile("ui-recording.wav");
        expect(view.armRecording(recordingFile, 1000.0, 16U));
        const auto* recordingState =
            dynamic_cast<const juce::Label*>(findDescendantWithId(view, "recording-state"));
        expect(recordingState != nullptr);
        if (recordingState != nullptr)
            expect(recordingState->getText().contains("Armed"));
        if (evidenceDirectoryPath.isNotEmpty())
            expect(writeUiEvidence(view,
                                   evidenceDirectory.getChildFile("padflow-recording-panel.png")));
        expect(view.startRecording());
        view.processPendingJobs();
        if (recordingState != nullptr)
            expect(recordingState->getText().contains("Recording"));

        std::array<float, 64U> recordedInput{};
        for (std::size_t frame = 0U; frame < recordedInput.size(); ++frame)
            recordedInput[frame] = 0.5F * std::sin(static_cast<float>(frame) * 0.12F);
        const std::array<const float*, 1U> recordedChannels{recordedInput.data()};
        view.processMockRecordingInput(recordedChannels.data(), 1U,
                                       static_cast<std::uint32_t>(recordedInput.size()));
        view.stopRecording();

        beginTest("UIHEADLESS-M2-009 completed recording assigns and retriggers");
        const auto derivedAssetUuid = controller.project().pad(0U).layers[0].assetUuid;
        for (int attempt = 0; attempt < 3000; ++attempt) {
            juce::Thread::sleep(2);
            view.processPendingJobs();
            if (controller.project().pad(0U).layers[0].assetUuid != derivedAssetUuid)
                break;
        }
        expect(controller.project().pad(0U).layers[0].assetUuid != derivedAssetUuid);
        expectEquals(static_cast<int>(controller.project().state().recordedAssets.size()), 1);
        expect(recordingFile.existsAsFile());
        std::array<float, 128U> left{};
        std::array<float, 128U> right{};
        runtime.engine().processBlock(left.data(), right.data(), left.size());
        expect(input.mouseDown(0U));
        runtime.engine().processBlock(left.data(), right.data(), left.size());
        expect(std::any_of(left.begin(), left.end(), [](const auto sampleValue) {
            return std::abs(sampleValue) > 0.00001F;
        }));
        juce::ignoreUnused(input.mouseUp(0U));

        beginTest("REGRESSION-M2-003 late import completion has typed safe routing");
        const auto oldProjectWorkingDirectory =
            juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile("PadFlow")
                .getChildFile("Projects")
                .getChildFile(controller.project().uuid());
        const auto blocker = jobs.submit(
            JobSpec{"regression-blocker", "regression-blocker", 0U, 10, JobKind::generic},
            [](const CancellationToken&, JobProgress&) {
                juce::Thread::sleep(50);
                return std::make_shared<const JobResult>(JobResult{
                    JobSpec{"regression-blocker", "regression-blocker", 0U, 10, JobKind::generic},
                    true,
                    "Blocker complete",
                    {}});
            });
        expect(blocker.has_value());
        juce::StringArray lateFiles;
        lateFiles.add(sample.getFullPathName());
        expect(view.queueImportFiles(lateFiles, 1U, true));
        auto* newProjectButton =
            dynamic_cast<juce::Button*>(findDescendantWithId(view, "new-project"));
        expect(newProjectButton != nullptr);
        if (newProjectButton != nullptr && newProjectButton->onClick)
            newProjectButton->onClick();
        for (int attempt = 0; attempt < 200; ++attempt) {
            juce::Thread::sleep(2);
            view.processPendingJobs();
        }
        expect(controller.project().pad(1U).layers[0].assetUuid.isEmpty());

        input.panic();
        jobs.shutdown();
        publisher.clearWhenAudioIsStopped();
        assets.clear();
        if (oldProjectWorkingDirectory.isAChildOf(
                juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)))
            juce::ignoreUnused(oldProjectWorkingDirectory.deleteRecursively());
        expect(directory.deleteRecursively());
    }
};

static Milestone2UiTests milestone2UiTests;
} // namespace padflow
