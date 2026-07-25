#include "App/SamplerView.h"
#include "App/WaveformEditor.h"
#include "Sampling/SampleImporter.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace padflow {
namespace {
bool writeChoppingFixture(const juce::File& file) {
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
    juce::AudioBuffer<float> buffer{2, 4096};
    buffer.clear();
    for (const auto onset : {512, 1280, 2176, 3200}) {
        for (int offset = 0; offset < 80 && onset + offset < buffer.getNumSamples(); ++offset) {
            const auto decay = std::exp(-static_cast<float>(offset) * 0.055F);
            buffer.setSample(0, onset + offset, decay * 0.9F);
            buffer.setSample(1, onset + offset, decay * -0.65F);
        }
    }
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

bool importFixture(ApplicationController& controller, SampleAssetRegistry& assets,
                   const juce::File& file) {
    const SampleImportRequest request{{controller.project().uuid(),
                                       controller.project().pad(0U).uuid,
                                       controller.project().revision(), 0, JobKind::sampleImport},
                                      file,
                                      "milestone3-ui-asset",
                                      0U,
                                      0U,
                                      assets.budgetBytes()};
    CancellationToken token;
    JobProgress progress;
    const auto result = SampleImporter::decode(request, token, progress);
    return result != nullptr && result->succeeded &&
           SampleImporter::commit(*result, controller, assets).wasOk();
}

juce::Component* findDescendantWithId(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* match = findDescendantWithId(*child, id))
            return match;
    return nullptr;
}

bool writeUiEvidence(juce::Component& component, const juce::File& file) {
    const auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 1.0F);
    auto stream = file.createOutputStream();
    return image.isValid() && stream != nullptr && stream->openedOk() &&
           juce::PNGImageFormat{}.writeImageToStream(image, *stream);
}
} // namespace

class Milestone3UiTests final : public juce::UnitTest {
  public:
    Milestone3UiTests() : juce::UnitTest("Milestone 3 chopping UI", "PadFlow") {}

    void runTest() override {
        const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getNonexistentChildFile("padflow-m3-ui", {}, true);
        expect(directory.createDirectory());
        const auto fixture = directory.getChildFile("chopping-ui.wav");
        expect(writeChoppingFixture(fixture));

        ApplicationController controller;
        controller.createEmptyProject("Chopping UI Test", "chopping-ui-project");
        BackgroundJobSystem jobs{16U, 1U};
        SampleAssetRegistry assets{16U * 1024U * 1024U};
        expect(importFixture(controller, assets, fixture));
        expect(controller.setLayerTrim(0U, 0U, 128U, 3968U).wasOk());
        auto occupiedLayer = controller.project().pad(0U).layers[0U];
        occupiedLayer.uuid = controller.project().pad(1U).layers[0U].uuid;
        expect(controller.setLayer(1U, 0U, occupiedLayer).wasOk());

        AudioRuntime runtime;
        runtime.engine().prepare(48000.0);
        runtime.preview().prepare(48000.0);
        PlaybackStatePublisher publisher{runtime.engine(), assets};
        InputRouter input{controller, runtime.engine()};
        SamplePreviewController preview{controller, runtime.preview()};
        SamplerView view{controller, jobs, assets, runtime, publisher, input, preview};
        view.setBounds(0, 0, 1180, 760);

        beginTest("UIHEADLESS-M3-001 constructs an accessible chopping workspace");
        expect(view.setChoppingWorkspaceVisible(true));
        for (const auto& id : {"chopping-workspace",
                               "chop-mode",
                               "chop-slice-count",
                               "chop-fixed-length",
                               "chop-fixed-unit",
                               "chop-remainder-policy",
                               "chop-transient-sensitivity",
                               "chop-minimum-duration",
                               "chop-attack-lookback",
                               "chop-analyse",
                               "chop-add-marker",
                               "chop-delete-marker",
                               "chop-clear-markers",
                               "chop-previous-slice",
                               "chop-next-slice",
                               "chop-marker-zero-snap",
                               "chop-marker-grid",
                               "chop-slice-readout",
                               "chop-audition-selected",
                               "chop-audition-all",
                               "chop-audition-stop",
                               "chop-lazy-start",
                               "chop-lazy-marker",
                               "chop-lazy-stop",
                               "chop-destination-bank",
                               "chop-destination-pad",
                               "chop-destination-mode",
                               "chop-destination-layer",
                               "chop-overwrite-policy",
                               "chop-conflict-index",
                               "chop-replace-conflict",
                               "chop-skip-conflict",
                               "chop-continue-layers",
                               "chop-stay-editor",
                               "chop-preview-assignment",
                               "chop-commit-assignment",
                               "chop-cancel"})
            expect(findDescendantWithId(view, id) != nullptr,
                   juce::String{"Missing control: "} + id);

        auto& workspace = view.choppingWorkspace();
        const auto projectBeforeProvisional = controller.project().state();
        const auto revisionBeforeProvisional = controller.project().revision();
        const auto evidencePath =
            juce::SystemStats::getEnvironmentVariable("PADFLOW_SCREENSHOT_DIR", {});
        const auto evidenceDirectory = juce::File{evidencePath};
        if (evidencePath.isNotEmpty())
            expect(evidenceDirectory.createDirectory());

        beginTest("UIHEADLESS-M3-002 and UIHEADLESS-M3-003 switch modes and generate equal slices");
        for (const auto mode :
             {SliceAlgorithm::equal, SliceAlgorithm::fixedLength, SliceAlgorithm::transient,
              SliceAlgorithm::manual, SliceAlgorithm::lazy}) {
            workspace.setMode(mode);
            expectEquals(static_cast<int>(workspace.mode()), static_cast<int>(mode));
        }
        expect(workspace.generateEqual(4).wasOk());
        expectEquals(static_cast<int>(workspace.session().provisionalSliceSet()->slices.size()), 4);
        expectEquals(static_cast<juce::int64>(controller.project().revision()),
                     static_cast<juce::int64>(revisionBeforeProvisional));
        if (evidencePath.isNotEmpty())
            expect(writeUiEvidence(view, evidenceDirectory.getChildFile("padflow-chop-equal.png")));

        beginTest("UIHEADLESS-M3-004 generates both fixed remainder policies provisionally");
        expect(
            workspace.generateFixed(700.0, SliceDisplayUnit::frames, SliceRemainderPolicy::include)
                .wasOk());
        expectEquals(static_cast<int>(workspace.session().provisionalSliceSet()->slices.size()), 6);
        expect(
            workspace.generateFixed(700.0, SliceDisplayUnit::frames, SliceRemainderPolicy::discard)
                .wasOk());
        expectEquals(static_cast<int>(workspace.session().provisionalSliceSet()->slices.size()), 5);

        beginTest("UIHEADLESS-M3-005 publishes asynchronous transient analysis");
        expect(workspace.submitTransientAnalysis({0.5F, 128, 0, 0.0F}));
        expect(workspace.analysisPending());
        for (int attempt = 0; attempt < 2000 && workspace.analysisPending(); ++attempt) {
            juce::Thread::sleep(1);
            view.processPendingJobs();
        }
        expect(!workspace.analysisPending());
        expect(workspace.session().provisionalSliceSet().has_value());
        expect(workspace.session().provisionalSliceSet()->slices.size() > 1U);
        if (evidencePath.isNotEmpty())
            expect(writeUiEvidence(view,
                                   evidenceDirectory.getChildFile("padflow-chop-transient.png")));

        beginTest("UIHEADLESS-M3-006 manual edit and session undo update provisional slices");
        const auto transientCount = workspace.session().provisionalSliceSet()->slices.size();
        expect(workspace.addMarker(900).wasOk());
        expectEquals(static_cast<int>(workspace.session().provisionalSliceSet()->slices.size()),
                     static_cast<int>(transientCount + 1U));
        expect(workspace.moveMarker(900, 920).wasOk());
        expect(workspace.deleteMarker(920).wasOk());
        expect(workspace.undoSessionEdit());
        expectEquals(static_cast<int>(workspace.session().provisionalSliceSet()->slices.size()),
                     static_cast<int>(transientCount + 1U));

        beginTest("Slice audition uses bounded preview without project mutation");
        expect(workspace.auditionSelected());
        std::array<float, 256U> left{};
        std::array<float, 256U> right{};
        runtime.preview().processAdd(left.data(), right.data(), left.size());
        expect(std::all_of(left.begin(), left.end(),
                           [](const auto value) { return std::isfinite(value); }));
        workspace.stopAudition();
        runtime.preview().processAdd(left.data(), right.data(), left.size());

        beginTest("UIHEADLESS-M3-007 drains bounded lazy marker events");
        expect(workspace.startLazy({128, 3968, 32, 0}));
        left.fill(0.0F);
        right.fill(0.0F);
        runtime.preview().processAdd(left.data(), right.data(), left.size());
        expect(workspace.captureLazyMarker(LazyMarkerSource::mouse));
        runtime.preview().processAdd(left.data(), right.data(), left.size());
        expect(workspace.captureLazyMarker(LazyMarkerSource::keyboard));
        runtime.preview().processAdd(left.data(), right.data(), left.size());
        expect(input.handleMidi(juce::MidiMessage::noteOn(1, 36, juce::uint8{100U})));
        const auto drained = workspace.drainLazyMarkers();
        expectEquals(static_cast<int>(drained.accepted), 3);
        if (evidencePath.isNotEmpty())
            expect(writeUiEvidence(view, evidenceDirectory.getChildFile("padflow-chop-lazy.png")));
        workspace.stopLazy();
        runtime.preview().processAdd(left.data(), right.data(), left.size());

        beginTest("UIHEADLESS-M3-008 previews every occupied assignment without mutation");
        expect(
            workspace.previewAssignment(AssignmentDestinationMode::consecutivePads, 1U, 0U, false)
                .wasOk());
        expect(workspace.assignmentPlan().has_value());
        expect(workspace.assignmentPlan()->destinations.front().occupied);
        expect(workspace.assignmentPlan()->hasUnresolvedConflicts());
        expect(workspace.resolveConflict(0U, AssignmentConflictDecision::skip).wasOk());
        expect(!workspace.assignmentPlan()->hasUnresolvedConflicts());
        expect(
            workspace.previewAssignment(AssignmentDestinationMode::consecutivePads, 1U, 0U, false)
                .wasOk());
        expectEquals(static_cast<juce::int64>(controller.project().revision()),
                     static_cast<juce::int64>(revisionBeforeProvisional));
        if (evidencePath.isNotEmpty())
            expect(writeUiEvidence(view,
                                   evidenceDirectory.getChildFile("padflow-chop-assignment.png")));

        beginTest("UIHEADLESS-M3-009 cancellation has zero project mutation");
        workspace.cancel();
        expect(controller.project().state() == projectBeforeProvisional);
        expectEquals(static_cast<juce::int64>(controller.project().revision()),
                     static_cast<juce::int64>(revisionBeforeProvisional));

        beginTest("UIHEADLESS-M3-010 confirmed assignment commits once and unified undo restores");
        expect(view.setChoppingWorkspaceVisible(true));
        auto& commitWorkspace = view.choppingWorkspace();
        expect(commitWorkspace.generateEqual(4).wasOk());
        expect(commitWorkspace
                   .previewAssignment(AssignmentDestinationMode::consecutivePads, 1U, 0U, false)
                   .wasOk());
        expect(commitWorkspace.resolveAllConflicts(AssignmentConflictDecision::replace).wasOk());
        AssignmentCommitReport report;
        const auto revisionBeforeCommit = controller.project().revision();
        expect(commitWorkspace.commitAssignment(report).wasOk());
        expectEquals(static_cast<int>(report.assignedSliceUuids.size()), 4);
        expectEquals(static_cast<juce::int64>(controller.project().revision()),
                     static_cast<juce::int64>(revisionBeforeCommit + 1U));
        expect(controller.undo());
        expect(controller.project().state() == projectBeforeProvisional);
        expect(controller.redo());
        expectEquals(static_cast<int>(controller.project().state().sliceSets.size()), 1);

        input.panic();
        runtime.preview().panicWhenQuiescent();
        publisher.clearWhenAudioIsStopped();
        jobs.shutdown();
        assets.clear();
        expect(directory.deleteRecursively());
    }
};

static Milestone3UiTests milestone3UiTests;
} // namespace padflow
