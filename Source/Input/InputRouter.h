#pragma once

#include "App/ApplicationController.h"
#include "Audio/PlaybackEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace padflow {
class InputRouter final : public juce::MidiInputCallback {
  public:
    InputRouter(ApplicationController& controller, PlaybackEngine& engine) noexcept;

    void refreshFromProject() noexcept;
    [[nodiscard]] std::size_t flushMidiCommands() noexcept;
    [[nodiscard]] std::uint64_t midiIngressOverflowCount() const noexcept;
    [[nodiscard]] bool mouseDown(std::size_t padInActiveBank);
    [[nodiscard]] bool mouseUp(std::size_t padInActiveBank);
    void mouseCaptureLost() noexcept;
    [[nodiscard]] bool keyDown(int keyCode, bool textEntryHasFocus);
    [[nodiscard]] bool keyUp(int keyCode);
    [[nodiscard]] bool handleMidi(const juce::MidiMessage& message);
    [[nodiscard]] bool triggerPad(std::size_t globalPadIndex, std::uint32_t sourceId,
                                  std::uint8_t velocity);
    [[nodiscard]] bool releaseSource(std::uint32_t sourceId);
    void panic() noexcept;
    void midiDeviceDisconnected() noexcept;
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

  private:
    [[nodiscard]] std::size_t activeBankOffset() const noexcept;
    [[nodiscard]] int findKeyboardPad(int keyCode) const noexcept;
    [[nodiscard]] int findMidiPad(int note) const noexcept;
    [[nodiscard]] bool makeMidiCommand(const juce::MidiMessage& message,
                                       AudioCommand& command) const noexcept;
    [[nodiscard]] bool markKeyHeld(int keyCode) noexcept;
    void clearKeyHeld(int keyCode) noexcept;

    ApplicationController& controller_;
    PlaybackEngine& engine_;
    std::array<int, 32U> heldKeyCodes_{};
    std::array<bool, padsPerBank> heldMousePads_{};
    std::array<std::atomic<std::uint8_t>, totalPadCount> midiNotes_{};
    std::atomic<std::uint8_t> selectedBank_{0U};
    std::atomic<std::uint8_t> midiChannelFilter_{0U};
    SpscQueue<AudioCommand, 256U> midiCommands_;
    std::atomic<std::uint64_t> midiIngressOverflows_{0U};
    AudioCommand deferredMidiCommand_;
    bool hasDeferredMidiCommand_{false};
};
} // namespace padflow
