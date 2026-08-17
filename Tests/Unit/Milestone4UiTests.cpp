#include "App/SequencerWorkspace.h"
#include "Sequencing/SequencerStatePublisher.h"

#include <juce_core/juce_core.h>

namespace padflow {
namespace {
juce::Component* findSequencerControl(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* match = findSequencerControl(*child, id))
            return match;
    return nullptr;
}

class Milestone4UiTests final : public juce::UnitTest {
  public:
    Milestone4UiTests() : juce::UnitTest("Milestone 4 sequencer UI", "PadFlow") {}

    void runTest() override {
        ApplicationController controller;
        controller.createEmptyProject("Sequencer UI", "sequencer-ui-project");
        AudioRuntime runtime;
        runtime.engine().prepare(48'000.0);
        InputRouter input{controller, runtime.engine()};
        SequencerWorkspace workspace{controller, runtime, input};
        workspace.setBounds(0, 0, 1180, 760);
        const auto screenshotDirectory =
            juce::SystemStats::getEnvironmentVariable("PADFLOW_SCREENSHOT_DIR", {});
        const auto writeScreenshot = [&](const juce::String& fileName) {
            if (screenshotDirectory.isEmpty())
                return true;
            const juce::File directory{screenshotDirectory};
            if (!directory.createDirectory())
                return false;
            const auto image =
                workspace.createComponentSnapshot(workspace.getLocalBounds(), true, 1.0F);
            auto stream = directory.getChildFile(fileName).createOutputStream();
            return image.isValid() && stream != nullptr && stream->openedOk() &&
                   juce::PNGImageFormat{}.writeImageToStream(image, *stream);
        };

        beginTest("UIHEADLESS-M4-001..002 constructs transport and grid controls without hardware");
        for (const auto& id :
             {"sequencer-workspace", "sequencer-close", "transport-play", "transport-stop",
              "transport-record", "transport-panic", "transport-loop", "transport-metronome",
              "transport-count-in", "transport-bpm", "pattern-selector", "pattern-name",
              "sequencer-step-grid", "event-velocity", "event-probability", "event-ratchet",
              "event-nudge"})
            expect(findSequencerControl(workspace, id) != nullptr,
                   juce::String{"Missing control: "} + id);

        beginTest("UIHEADLESS-M4-004..008 creates and edits a selected grid event");
        expect(workspace.toggleStep(0U, 0U));
        expectEquals(controller.project().state().sequencer.patterns.patterns.front().events.size(),
                     std::size_t{1U});
        expect(workspace.setSelectedEventVelocity(88U));
        expect(workspace.setSelectedEventProbability(probabilityQ32Maximum / 2U));
        expect(workspace.setSelectedEventRatchet(2U, {120, 0U}));
        expect(workspace.setSelectedEventNudge(MicroOffsetQ16::fromWholeTicks(-10)));
        const auto& edited =
            controller.project().state().sequencer.patterns.patterns.front().events.front();
        expectEquals(static_cast<int>(edited.velocity), 88);
        expectEquals(static_cast<int>(edited.ratchetCount), 2);
        expectEquals(edited.microOffset.rawValue, MicroOffsetQ16::fromWholeTicks(-10).rawValue);
        expect(writeScreenshot("padflow-sequencer-event-editing.png"));

        beginTest("UIHEADLESS-M4-003 pattern create, rename, duplicate, delete and select");
        expect(controller.createPattern("Second").wasOk());
        const auto secondUuid = controller.project().state().sequencer.patterns.selectedPatternUuid;
        expect(controller.renameSelectedPattern("Second Beat").wasOk());
        expect(controller.duplicateSelectedPattern().wasOk());
        expect(controller.deleteSelectedPattern().wasOk());
        expect(controller.selectPattern(secondUuid).wasOk());
        workspace.refresh();

        beginTest("UIHEADLESS-M4-009 reads the atomic transport playhead");
        workspace.refresh();
        expect(workspace.displayedPlayheadFrame() >= 0);

        beginTest("UIHEADLESS-M4-010 performs step recording");
        expect(workspace.stepRecordPad(5U, 104U));
        const auto* selected =
            findPattern(controller.project().state().sequencer.patterns,
                        controller.project().state().sequencer.patterns.selectedPatternUuid);
        expect(selected != nullptr && !selected->events.empty());
        expect(writeScreenshot("padflow-sequencer-grid.png"));

        beginTest("UIHEADLESS-M4-011 simulates live recording with count-in state");
        auto preferences = controller.project().state().sequencer.transport;
        preferences.countInBars = 1U;
        expect(controller.setTransportPreferences(preferences).wasOk());
        SequencerStatePublisher publisher{runtime.scheduler(), runtime.transport()};
        expect(publisher.publish(controller.project().state(), 48'000U).wasOk());
        workspace.refresh();
        expect(workspace.beginLiveRecording(PatternRecordMode::overdub));
        runtime.transport().beginBlock();
        expect(runtime.transport().snapshot().state == TransportState::countIn);
        expect(writeScreenshot("padflow-sequencer-live-count-in.png"));
        workspace.cancelLiveRecording();
        runtime.transport().stopAndPanicWhenQuiescent();
        publisher.clearWhenAudioIsStopped();

        workspace.cancelLiveRecording();
        runtime.close();
    }
};

static Milestone4UiTests milestone4UiTests;
} // namespace
} // namespace padflow
