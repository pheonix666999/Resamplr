#include "InputRouter.h"

#include <algorithm>
#include <cmath>

namespace padflow {
namespace {
constexpr std::uint32_t mouseSourceBase = 0x10000U;
constexpr std::uint32_t keyboardSourceBase = 0x20000U;
constexpr std::uint32_t midiSourceBase = 0x30000U;
constexpr int emptyKeyCode = -1;
} // namespace

InputRouter::InputRouter(ApplicationController& controller, PlaybackEngine& engine) noexcept
    : controller_(controller), engine_(engine) {
    heldKeyCodes_.fill(emptyKeyCode);
    refreshFromProject();
}

void InputRouter::refreshFromProject() noexcept {
    const auto& project = controller_.project();
    for (std::size_t index = 0; index < totalPadCount; ++index)
        midiNotes_[index].store(project.pad(index).midiNote, std::memory_order_relaxed);
    selectedBank_.store(project.state().ui.selectedBank, std::memory_order_release);
    midiChannelFilter_.store(project.state().midi.channelFilter, std::memory_order_release);
}

std::size_t InputRouter::flushMidiCommands() noexcept {
    std::size_t flushed = 0U;
    if (hasDeferredMidiCommand_) {
        if (!engine_.enqueue(deferredMidiCommand_))
            return flushed;
        hasDeferredMidiCommand_ = false;
        ++flushed;
    }

    AudioCommand command;
    while (midiCommands_.tryPop(command)) {
        if (!engine_.enqueue(command)) {
            deferredMidiCommand_ = command;
            hasDeferredMidiCommand_ = true;
            break;
        }
        ++flushed;
    }
    return flushed;
}

std::uint64_t InputRouter::midiIngressOverflowCount() const noexcept {
    return midiIngressOverflows_.load(std::memory_order_acquire);
}

std::size_t InputRouter::activeBankOffset() const noexcept {
    return static_cast<std::size_t>(controller_.project().state().ui.selectedBank) * padsPerBank;
}

bool InputRouter::mouseDown(const std::size_t padInActiveBank) {
    if (padInActiveBank >= padsPerBank)
        return false;
    auto ui = controller_.project().state().ui;
    ui.selectedPad = static_cast<std::uint8_t>(padInActiveBank);
    if (controller_.setUiState(ui).failed())
        return false;
    heldMousePads_[padInActiveBank] = true;
    return triggerPad(activeBankOffset() + padInActiveBank,
                      mouseSourceBase + static_cast<std::uint32_t>(padInActiveBank),
                      ui.fixedTriggerVelocity);
}

bool InputRouter::mouseUp(const std::size_t padInActiveBank) {
    if (padInActiveBank >= padsPerBank || !heldMousePads_[padInActiveBank])
        return false;
    heldMousePads_[padInActiveBank] = false;
    return releaseSource(mouseSourceBase + static_cast<std::uint32_t>(padInActiveBank));
}

void InputRouter::mouseCaptureLost() noexcept {
    for (std::size_t index = 0; index < heldMousePads_.size(); ++index)
        if (heldMousePads_[index]) {
            heldMousePads_[index] = false;
            juce::ignoreUnused(releaseSource(mouseSourceBase + static_cast<std::uint32_t>(index)));
        }
}

int InputRouter::findKeyboardPad(const int keyCode) const noexcept {
    const auto keyText = juce::KeyPress{keyCode}.getTextDescription().toUpperCase();
    const auto offset = activeBankOffset();
    for (std::size_t index = 0; index < padsPerBank; ++index)
        if (controller_.project().pad(offset + index).keyboardKey.toUpperCase() == keyText)
            return static_cast<int>(index);
    return -1;
}

int InputRouter::findMidiPad(const int note) const noexcept {
    const auto offset =
        static_cast<std::size_t>(selectedBank_.load(std::memory_order_acquire)) * padsPerBank;
    for (std::size_t index = 0; index < padsPerBank; ++index)
        if (static_cast<int>(midiNotes_[offset + index].load(std::memory_order_relaxed)) == note)
            return static_cast<int>(index);
    return -1;
}

bool InputRouter::makeMidiCommand(const juce::MidiMessage& message,
                                  AudioCommand& command) const noexcept {
    if (!message.isNoteOnOrOff())
        return false;
    const auto filter = midiChannelFilter_.load(std::memory_order_acquire);
    if (filter != 0U && message.getChannel() != static_cast<int>(filter))
        return false;
    const auto localPad = findMidiPad(message.getNoteNumber());
    if (localPad < 0)
        return false;
    const auto source = midiSourceBase | (static_cast<std::uint32_t>(message.getChannel()) << 8U) |
                        static_cast<std::uint32_t>(message.getNoteNumber());
    if (message.isNoteOff() || message.getVelocity() == 0U) {
        command = {AudioCommandType::releaseSource, 0U, source, 0.0F};
        return true;
    }
    const auto midiVelocity = static_cast<int>(message.getVelocity());
    const auto velocity = static_cast<std::uint8_t>(std::clamp(midiVelocity, 1, 127));
    const auto bankOffset =
        static_cast<std::size_t>(selectedBank_.load(std::memory_order_acquire)) * padsPerBank;
    command = {AudioCommandType::triggerPad,
               static_cast<std::uint32_t>(bankOffset + static_cast<std::size_t>(localPad)), source,
               static_cast<float>(velocity)};
    return true;
}

bool InputRouter::markKeyHeld(const int keyCode) noexcept {
    if (std::find(heldKeyCodes_.begin(), heldKeyCodes_.end(), keyCode) != heldKeyCodes_.end())
        return false;
    const auto empty = std::find(heldKeyCodes_.begin(), heldKeyCodes_.end(), emptyKeyCode);
    if (empty == heldKeyCodes_.end())
        return false;
    *empty = keyCode;
    return true;
}

void InputRouter::clearKeyHeld(const int keyCode) noexcept {
    const auto entry = std::find(heldKeyCodes_.begin(), heldKeyCodes_.end(), keyCode);
    if (entry != heldKeyCodes_.end())
        *entry = emptyKeyCode;
}

bool InputRouter::keyDown(const int keyCode, const bool textEntryHasFocus) {
    if (textEntryHasFocus)
        return false;
    const auto localPad = findKeyboardPad(keyCode);
    if (localPad < 0 || !markKeyHeld(keyCode))
        return false;
    const auto velocity = controller_.project().state().ui.fixedTriggerVelocity;
    return triggerPad(activeBankOffset() + static_cast<std::size_t>(localPad),
                      keyboardSourceBase + static_cast<std::uint32_t>(keyCode), velocity);
}

bool InputRouter::keyUp(const int keyCode) {
    const auto held = std::find(heldKeyCodes_.begin(), heldKeyCodes_.end(), keyCode);
    if (held == heldKeyCodes_.end())
        return false;
    clearKeyHeld(keyCode);
    return releaseSource(keyboardSourceBase + static_cast<std::uint32_t>(keyCode));
}

bool InputRouter::handleMidi(const juce::MidiMessage& message) {
    AudioCommand command;
    return makeMidiCommand(message, command) && engine_.enqueue(command);
}

bool InputRouter::triggerPad(const std::size_t globalPadIndex, const std::uint32_t sourceId,
                             const std::uint8_t velocity) {
    if (globalPadIndex >= totalPadCount)
        return false;
    return engine_.enqueue(AudioCommand{AudioCommandType::triggerPad,
                                        static_cast<std::uint32_t>(globalPadIndex), sourceId,
                                        static_cast<float>(velocity)});
}

bool InputRouter::releaseSource(const std::uint32_t sourceId) {
    return engine_.enqueue(AudioCommand{AudioCommandType::releaseSource, 0U, sourceId, 0.0F});
}

void InputRouter::panic() noexcept {
    heldKeyCodes_.fill(emptyKeyCode);
    heldMousePads_.fill(false);
    juce::ignoreUnused(engine_.enqueue(AudioCommand{AudioCommandType::panic, 0U, 0U, 0.0F}));
}

void InputRouter::midiDeviceDisconnected() noexcept {
    panic();
}

void InputRouter::handleIncomingMidiMessage(juce::MidiInput* const source,
                                            const juce::MidiMessage& message) {
    juce::ignoreUnused(source);
    AudioCommand command;
    if (makeMidiCommand(message, command) && !midiCommands_.tryPush(command))
        midiIngressOverflows_.fetch_add(1U, std::memory_order_relaxed);
}
} // namespace padflow
