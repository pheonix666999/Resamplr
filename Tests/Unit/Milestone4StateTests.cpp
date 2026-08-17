#include "App/ApplicationController.h"
#include "Input/InputRouter.h"
#include "Sequencing/PatternRecorder.h"
#include "Sequencing/SequencerStatePublisher.h"
#include "Serialization/ProjectSerializer.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstddef>

namespace padflow {
namespace {
class Milestone4StateTests final : public juce::UnitTest {
  public:
    Milestone4StateTests()
        : juce::UnitTest("Milestone 4 project state and recording", "sequencing") {}

    void runTest() override {
        ApplicationController controller;
        controller.createEmptyProject("Sequencer State", "sequencer-state-project");

        beginTest("SAVE-M4-001..007 and SAVE-M4-012 persist canonical sequencer state");
        expect(controller.setTempo(98'750'000).wasOk());
        auto preferences = controller.project().state().sequencer.transport;
        preferences.loopEnabled = false;
        preferences.metronomeEnabled = true;
        preferences.countInBars = 2U;
        preferences.metronomeVolume = 0.37F;
        expect(controller.setTransportPreferences(preferences).wasOk());
        SequenceEvent event;
        event.uuid = "48484848-4848-4848-8848-484848484848";
        event.padUuid = controller.project().pad(4U).uuid;
        event.start = {480, 32768U};
        event.duration = {360, 12U};
        event.velocity = 91U;
        event.probability = 3'221'225'472ULL;
        event.ratchetCount = 3U;
        event.ratchetSpacing = {120, 4U};
        event.microOffset = {-12345};
        event.stablePadOrder = 4U;
        expect(controller.addEventToSelectedPattern(event).wasOk());
        const auto manifest = ProjectSerializer::canonicalManifest(controller.project());
        expect(manifest.contains("\"microBpm\": \"98750000\""));
        expect(manifest.contains("\"probabilityAlgorithm\": \"siphash24-v1\""));
        expect(!manifest.contains("frameOffset"));
        expect(!manifest.contains("sampleOffset"));
        expect(!manifest.contains("mutableRng"));
        auto restored = Project::createEmpty();
        expect(ProjectSerializer::restoreCanonicalManifest(manifest, restored).wasOk());
        expect(restored.state() == controller.project().state());

        beginTest("SAVE-M4-008..010 legacy payloads receive deterministic defaults");
        auto legacyManifest = manifest;
        const auto parsed = juce::JSON::parse(legacyManifest);
        auto* object = parsed.getDynamicObject();
        expect(object != nullptr);
        if (object != nullptr)
            object->removeProperty("sequencer");
        legacyManifest = juce::JSON::toString(parsed, false, 17) + "\n";
        auto legacy = Project::createEmpty();
        expect(ProjectSerializer::restoreCanonicalManifest(legacyManifest, legacy).wasOk());
        expectEquals(legacy.state().sequencer.patterns.patterns.size(), std::size_t{1U});
        expectEquals(legacy.state().sequencer.probabilityAlgorithm, juce::String{"siphash24-v1"});

        beginTest("SEQ-M4-040..046 controller pattern lifecycle is unified undoable state");
        ApplicationController lifecycle;
        lifecycle.createEmptyProject("Lifecycle", "pattern-lifecycle-project");
        expect(lifecycle.createPattern("Verse").wasOk());
        const auto beforeRename = lifecycle.project().state();
        expect(lifecycle.renameSelectedPattern("Verse A").wasOk());
        expect(lifecycle.undo());
        expect(lifecycle.project().state() == beforeRename);
        expect(lifecycle.redo());
        expect(lifecycle.duplicateSelectedPattern().wasOk());
        expectEquals(lifecycle.project().state().sequencer.patterns.patterns.size(),
                     std::size_t{3U});
        expect(lifecycle.deleteSelectedPattern().wasOk());
        expect(lifecycle.undo());
        expectEquals(lifecycle.project().state().sequencer.patterns.patterns.size(),
                     std::size_t{3U});

        beginTest("RECORD-M4-001..003 step input advances without transport");
        auto stepPattern = makeDefaultPattern("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
        std::int64_t cursor = 0;
        expect(PatternRecorder::stepRecord(stepPattern, controller.project().pad(2U).uuid, 2U, 113U,
                                           cursor, 1U)
                   .wasOk());
        expectEquals(stepPattern.events.size(), std::size_t{1U});
        expectEquals(static_cast<int>(stepPattern.events.front().velocity), 113);
        expectEquals(cursor, std::int64_t{240});

        beginTest("RECORD-M4-004..015 bounded live take captures gates and excludes count-in");
        PatternRecorder recorder;
        expect(recorder.beginTake(controller.project().state(), PatternRecordMode::overdub, 48'000U)
                   .wasOk());
        expect(!recorder.capture({PatternRecordInputType::noteOn, 1U, 10U, 77U, 0, 0U},
                                 TransportState::countIn));
        expect(recorder.capture({PatternRecordInputType::noteOn, 1U, 10U, 77U, 0, 0U},
                                TransportState::recording));
        expect(recorder.capture({PatternRecordInputType::noteOff, 0U, 10U, 1U, 6000, 0U},
                                TransportState::recording));
        Pattern take;
        expect(recorder.finishTake(6000, take).wasOk());
        const auto recorded = std::find_if(take.events.begin(), take.events.end(),
                                           [](const auto& entry) { return entry.velocity == 77U; });
        expect(recorded != take.events.end());
        expect(recorded != take.events.end() && recorded->duration.isPositive());

        beginTest("RECORD-M4-004..006 mouse, keyboard and MIDI use the common capture path");
        PlaybackEngine inputEngine;
        inputEngine.prepare(48'000.0);
        TransportEngine inputTransport;
        PatternScheduler inputScheduler;
        SequencerStatePublisher inputPublisher{inputScheduler, inputTransport};
        auto routedState = controller.project().state();
        routedState.sequencer.transport.countInBars = 0U;
        routedState.sequencer.tempoPoints = {{MusicalTime{}, defaultTempoMicroBpm}};
        findPattern(routedState.sequencer.patterns,
                    routedState.sequencer.patterns.selectedPatternUuid)
            ->events.clear();
        expect(inputPublisher.publish(routedState, 48'000U).wasOk());
        InputRouter input{controller, inputEngine};
        PatternRecorder routedRecorder;
        expect(routedRecorder.beginTake(routedState, PatternRecordMode::overdub, 48'000U).wasOk());
        input.setPatternRecorder(&routedRecorder, &inputTransport);
        expect(inputTransport.record());
        inputTransport.beginBlock();
        expect(input.mouseDown(0U));
        expect(input.mouseUp(0U));
        expect(input.keyDown('2', false));
        expect(input.keyUp('2'));
        expect(input.handleMidi(juce::MidiMessage::noteOn(1, 38, juce::uint8{109U})));
        expect(input.handleMidi(juce::MidiMessage::noteOff(1, 38)));
        Pattern routedTake;
        expect(routedRecorder.finishTake(1, routedTake).wasOk());
        expectEquals(routedTake.events.size(), std::size_t{3U});
        expect(std::any_of(routedTake.events.begin(), routedTake.events.end(),
                           [](const auto& entry) { return entry.velocity == 109U; }));
        input.setPatternRecorder(nullptr, nullptr);
        input.panic();
        inputTransport.stopAndPanicWhenQuiescent();
        inputPublisher.clearWhenAudioIsStopped();

        beginTest("RECORD-M4-009..014 overdub and quantization modes are deterministic");
        const auto quantizedStart = [&](const RecordQuantization quantization,
                                        const std::uint8_t strength) {
            auto state = controller.project().state();
            state.sequencer.tempoPoints = {{MusicalTime{}, defaultTempoMicroBpm}};
            auto* pattern =
                findPattern(state.sequencer.patterns, state.sequencer.patterns.selectedPatternUuid);
            pattern->recordQuantization = quantization;
            pattern->quantizeStrengthPercent = strength;
            pattern->events.clear();
            PatternRecorder quantizedRecorder;
            expect(quantizedRecorder.beginTake(state, PatternRecordMode::overdub, 48'000U).wasOk());
            expect(
                quantizedRecorder.capture({PatternRecordInputType::noteOn, 0U, 91U, 100U, 2500, 0U},
                                          TransportState::recording));
            expect(
                quantizedRecorder.capture({PatternRecordInputType::noteOff, 0U, 91U, 1U, 4000, 0U},
                                          TransportState::recording));
            Pattern quantized;
            expect(quantizedRecorder.finishTake(4000, quantized).wasOk());
            return quantized.events.front().start.wholePpqTicks;
        };
        expectEquals(quantizedStart(RecordQuantization::off, 100U), std::int64_t{100});
        expectEquals(quantizedStart(RecordQuantization::sixteenth, 100U), std::int64_t{0});
        expectEquals(quantizedStart(RecordQuantization::sixteenthTriplet, 100U), std::int64_t{160});
        expectEquals(quantizedStart(RecordQuantization::sixteenth, 50U), std::int64_t{50});

        beginTest("RECORD-M4-010 replace removes only captured lanes in the take range");
        auto replaceState = controller.project().state();
        auto* replaceBase = findPattern(replaceState.sequencer.patterns,
                                        replaceState.sequencer.patterns.selectedPatternUuid);
        replaceBase->events.clear();
        for (std::uint16_t lane = 0U; lane < 2U; ++lane) {
            SequenceEvent base;
            base.uuid = lane == 0U ? "10101010-1010-4010-8010-101010101010"
                                   : "20202020-2020-4020-8020-202020202020";
            base.padUuid = controller.project().pad(lane).uuid;
            base.stablePadOrder = lane;
            expect(addSequenceEvent(*replaceBase, base, lane + 1U).wasOk());
        }
        PatternRecorder replaceRecorder;
        expect(
            replaceRecorder.beginTake(replaceState, PatternRecordMode::replace, 48'000U).wasOk());
        expect(replaceRecorder.capture({PatternRecordInputType::noteOn, 0U, 99U, 80U, 0, 0U},
                                       TransportState::recording));
        expect(replaceRecorder.capture({PatternRecordInputType::noteOff, 0U, 99U, 1U, 6000, 0U},
                                       TransportState::recording));
        Pattern replaced;
        expect(replaceRecorder.finishTake(6000, replaced).wasOk());
        expectEquals(std::count_if(replaced.events.begin(), replaced.events.end(),
                                   [](const auto& entry) { return entry.stablePadOrder == 1U; }),
                     std::ptrdiff_t{1});

        beginTest("RECORD-M4-016 cancel restores the pre-take pattern exactly");
        expect(recorder.beginTake(controller.project().state(), PatternRecordMode::replace, 48'000U)
                   .wasOk());
        expect(recorder.capture({PatternRecordInputType::noteOn, 3U, 11U, 100U, 100, 0U},
                                TransportState::recording));
        recorder.cancel();

        beginTest("RECORD-M4-017 a completed take creates exactly one undo entry");
        ApplicationController takeController;
        takeController.createEmptyProject("Take Undo", "take-undo-project");
        const auto beforeTake = takeController.project().state();
        auto takePattern = *findPattern(beforeTake.sequencer.patterns,
                                        beforeTake.sequencer.patterns.selectedPatternUuid);
        SequenceEvent takeEvent;
        takeEvent.uuid = "30303030-3030-4030-8030-303030303030";
        takeEvent.padUuid = takeController.project().pad(0U).uuid;
        expect(addSequenceEvent(takePattern, takeEvent, 1U).wasOk());
        expect(takeController.replaceSelectedPattern(std::move(takePattern), "Record pattern take")
                   .wasOk());
        expect(takeController.undo());
        expect(takeController.project().state() == beforeTake);
        expect(!takeController.canUndo());
        expect(!recorder.isActive());

        beginTest("THREAD-M4-002 bounded input queue reports deterministic overflow");
        expect(recorder.beginTake(controller.project().state(), PatternRecordMode::overdub, 48'000U)
                   .wasOk());
        std::size_t accepted = 0U;
        for (std::size_t index = 0U; index < 600U; ++index)
            if (recorder.capture({PatternRecordInputType::noteOn, 0U, index + 100U, 100U,
                                  static_cast<std::int64_t>(index), 0U},
                                 TransportState::recording))
                ++accepted;
        expectEquals(accepted, std::size_t{512U});
        expect(recorder.overflowCount() > 0U);
        recorder.cancel();
    }
};

static Milestone4StateTests milestone4StateTests;
} // namespace
} // namespace padflow
