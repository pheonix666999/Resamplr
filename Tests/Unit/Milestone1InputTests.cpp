#include "Input/InputRouter.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace padflow {
class Milestone1InputTests final : public juce::UnitTest {
  public:
    Milestone1InputTests() : juce::UnitTest("Milestone 1 input routing", "PadFlow") {}

    void runTest() override {
        ApplicationController controller;
        controller.createEmptyProject("Input", "input-project");
        PlaybackEngine engine;
        engine.prepare(48000.0);
        std::array<float, 256U> pcm{};
        pcm.fill(0.5F);
        PlaybackSnapshot snapshot;
        for (auto& pad : snapshot.pads) {
            pad.layers[0].enabled = true;
            pad.layers[0].asset = {pcm.data(), static_cast<std::uint64_t>(pcm.size()), 1U, 48000.0};
            pad.envelope = {0.0F, 0.0F, 1.0F, 0.001F};
            pad.playbackMode = PlaybackMode::gate;
        }
        engine.publishSnapshot(&snapshot);
        InputRouter router{controller, engine};
        std::array<float, 64U> left{};
        std::array<float, 64U> right{};

        beginTest("INPUT-M1-001 and INPUT-M1-002 mouse select/trigger/release/capture loss");
        expect(router.mouseDown(0U));
        engine.processBlock(left.data(), right.data(), left.size());
        expect(engine.activeVoiceCount() > 0U);
        expectEquals(static_cast<int>(controller.project().state().ui.selectedPad), 0);
        expect(router.mouseUp(0U));
        engine.processBlock(left.data(), right.data(), left.size());
        expect(router.mouseDown(1U));
        router.mouseCaptureLost();
        engine.processBlock(left.data(), right.data(), left.size());

        beginTest("INPUT-M1-003, INPUT-M1-004, and INPUT-M1-011 keyboard banks/repeat");
        engine.panic();
        expect(router.keyDown('1', false));
        expect(!router.keyDown('1', false));
        expect(router.keyUp('1'));
        expect(!router.keyUp('1'));
        expect(!router.keyDown('Q', true));
        auto ui = controller.project().state().ui;
        ui.selectedBank = 1U;
        expect(controller.setUiState(ui).wasOk());
        router.refreshFromProject();
        expect(router.keyDown('1', false));
        engine.processBlock(left.data(), right.data(), left.size());
        expect(engine.activeVoiceCount() > 0U);
        expect(router.keyUp('1'));

        beginTest("INPUT-M1-005 through INPUT-M1-010 MIDI velocity, zero-off, and filter");
        ui.selectedBank = 0U;
        expect(controller.setUiState(ui).wasOk());
        MidiSettings midi;
        midi.channelFilter = 2U;
        midi.preferredInputIdentifier = "test-midi";
        expect(controller.setMidiSettings(midi).wasOk());
        router.refreshFromProject();
        expect(!router.handleMidi(juce::MidiMessage::noteOn(1, 36, juce::uint8{100U})));
        expect(router.handleMidi(juce::MidiMessage::noteOn(2, 36, juce::uint8{100U})));
        engine.processBlock(left.data(), right.data(), left.size());
        bool nonSilent = false;
        for (const auto sample : left) {
            expect(std::isfinite(sample));
            nonSilent = nonSilent || std::abs(sample) > 0.00001F;
        }
        expect(nonSilent);
        expect(router.handleMidi(juce::MidiMessage::noteOn(2, 36, juce::uint8{0U})));
        expect(router.handleMidi(juce::MidiMessage::noteOff(2, 36)));
        router.midiDeviceDisconnected();
        engine.processBlock(left.data(), right.data(), left.size());
        expectEquals(static_cast<int>(engine.activeVoiceCount()), 0);

        beginTest("MIDI callback crosses a bounded SPSC ingress before the engine queue");
        router.handleIncomingMidiMessage(nullptr,
                                         juce::MidiMessage::noteOn(2, 36, juce::uint8{100U}));
        expectEquals(static_cast<int>(router.flushMidiCommands()), 1);
        engine.processBlock(left.data(), right.data(), left.size());
        expect(engine.activeVoiceCount() > 0U);

        beginTest("REGRESSION-M1-001 MIDI callback ingress overflow is observable");
        const auto overflowsBefore = router.midiIngressOverflowCount();
        for (std::size_t index = 0; index < 512U; ++index)
            router.handleIncomingMidiMessage(nullptr,
                                             juce::MidiMessage::noteOn(2, 36, juce::uint8{100U}));
        expect(router.midiIngressOverflowCount() > overflowsBefore);

        beginTest("THREAD-M1-003 bounded input path reports command overflow");
        bool overflowObserved = false;
        for (std::uint32_t index = 0U; index < 2048U; ++index)
            if (!router.triggerPad(0U, index, 100U)) {
                overflowObserved = true;
                break;
            }
        expect(overflowObserved);
        router.panic();
    }
};

static Milestone1InputTests milestone1InputTests;
} // namespace padflow
