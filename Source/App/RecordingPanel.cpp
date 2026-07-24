#include "RecordingPanel.h"

#include <algorithm>
#include <cmath>

namespace padflow {
namespace {
constexpr auto panelColour = 0xff222a33U;
constexpr auto raisedColour = 0xff2b3540U;
constexpr auto borderColour = 0xff3b4957U;
constexpr auto textColour = 0xffd8e1e8U;
constexpr auto mutedTextColour = 0xff91a0abU;
constexpr auto tealColour = 0xff50c8bbU;
constexpr auto amberColour = 0xffe1aa55U;
constexpr auto coralColour = 0xffe27868U;

void styleButton(juce::Button& button) {
    button.setColour(juce::TextButton::buttonColourId, juce::Colour{raisedColour});
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colour{tealColour}.darker(0.35F));
    button.setColour(juce::TextButton::textColourOffId, juce::Colour{textColour});
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void styleLabel(juce::Label& label) {
    label.setColour(juce::Label::textColourId, juce::Colour{textColour});
    label.setJustificationType(juce::Justification::centredLeft);
}
} // namespace

RecordingPanel::RecordingPanel() {
    setTitle("Input recording panel");
    setDescription("Manual or threshold audio-input capture with explicit destination");
    setComponentID("recording-panel");
    configureControls();
    setCaptureStatus({}, 0.0);
}

void RecordingPanel::configureControls() {
    titleLabel_.setText("INPUT RECORDING", juce::dontSendNotification);
    titleLabel_.setFont(juce::FontOptions{22.0F, juce::Font::bold});
    titleLabel_.setColour(juce::Label::textColourId, juce::Colour{tealColour});
    titleLabel_.setComponentID("recording-title");
    addAndMakeVisible(titleLabel_);

    for (auto* label :
         {&inputDeviceLabel_, &stateLabel_, &elapsedLabel_, &overflowLabel_, &resultLabel_}) {
        styleLabel(*label);
        addAndMakeVisible(*label);
    }
    inputDeviceLabel_.setComponentID("recording-input-device");
    stateLabel_.setComponentID("recording-state");
    elapsedLabel_.setComponentID("recording-elapsed");
    overflowLabel_.setComponentID("recording-overflow");
    resultLabel_.setComponentID("recording-result");
    resultLabel_.setColour(juce::Label::textColourId, juce::Colour{mutedTextColour});

    inputMeter_.setSliderStyle(juce::Slider::LinearBar);
    inputMeter_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
    inputMeter_.setRange(-60.0, 0.0, 0.1);
    inputMeter_.setValue(-60.0, juce::dontSendNotification);
    inputMeter_.setSuffix(" dBFS");
    inputMeter_.setColour(juce::Slider::trackColourId, juce::Colour{tealColour});
    inputMeter_.setColour(juce::Slider::backgroundColourId, juce::Colour{raisedColour});
    inputMeter_.setComponentID("recording-input-meter");
    inputMeter_.setTitle("Live input peak meter");
    inputMeter_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(inputMeter_);

    channelBox_.addItem("Mono input", 1);
    channelBox_.addItem("Stereo input", 2);
    channelBox_.setComponentID("recording-channels");
    channelBox_.setTitle("Input channel layout");
    addAndMakeVisible(channelBox_);

    modeBox_.addItem("Manual", 1);
    modeBox_.addItem("Threshold", 2);
    modeBox_.setComponentID("recording-mode");
    modeBox_.setTitle("Recording mode");
    modeBox_.onChange = [this] {
        thresholdSlider_.setEnabled(modeBox_.getSelectedId() == 2 && armButton_.isEnabled());
    };
    addAndMakeVisible(modeBox_);

    for (auto* slider : {&thresholdSlider_, &preRollSlider_}) {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 22);
        slider->setColour(juce::Slider::trackColourId, juce::Colour{tealColour});
        slider->setColour(juce::Slider::backgroundColourId, juce::Colour{raisedColour});
        addAndMakeVisible(*slider);
    }
    thresholdSlider_.setRange(-96.0, 0.0, 0.5);
    thresholdSlider_.setSuffix(" dBFS");
    thresholdSlider_.setComponentID("recording-threshold");
    thresholdSlider_.setTitle("Threshold trigger level");
    preRollSlider_.setRange(0.0, 2000.0, 10.0);
    preRollSlider_.setSuffix(" ms");
    preRollSlider_.setComponentID("recording-preroll");
    preRollSlider_.setTitle("Threshold pre-roll duration");

    for (std::size_t bank = 0U; bank < padBankCount; ++bank)
        bankBox_.addItem("Bank " + juce::String::charToString(
                                       static_cast<juce::juce_wchar>('A' + static_cast<int>(bank))),
                         static_cast<int>(bank + 1U));
    for (std::size_t pad = 0U; pad < padsPerBank; ++pad)
        padBox_.addItem("Pad " + juce::String{static_cast<int>(pad + 1U)},
                        static_cast<int>(pad + 1U));
    for (std::size_t layer = 0U; layer < minimumLayersPerPad; ++layer)
        layerBox_.addItem("Layer " + juce::String{static_cast<int>(layer + 1U)},
                          static_cast<int>(layer + 1U));
    bankBox_.setComponentID("recording-bank");
    padBox_.setComponentID("recording-pad");
    layerBox_.setComponentID("recording-layer");
    bankBox_.setTitle("Recording destination bank");
    padBox_.setTitle("Recording destination pad");
    layerBox_.setTitle("Recording destination layer");
    addAndMakeVisible(bankBox_);
    addAndMakeVisible(padBox_);
    addAndMakeVisible(layerBox_);

    autoAssignToggle_.setComponentID("recording-auto-assign");
    autoAssignToggle_.setTitle("Assign a valid completed take automatically");
    autoAssignToggle_.setColour(juce::ToggleButton::textColourId, juce::Colour{textColour});
    autoAssignToggle_.setColour(juce::ToggleButton::tickColourId, juce::Colour{tealColour});
    addAndMakeVisible(autoAssignToggle_);

    fileNameEditor_.setComponentID("recording-file-name");
    fileNameEditor_.setTitle("Recorded WAV file name");
    fileNameEditor_.setText("Recording.wav");
    fileNameEditor_.setColour(juce::TextEditor::backgroundColourId, juce::Colour{raisedColour});
    fileNameEditor_.setColour(juce::TextEditor::textColourId, juce::Colour{textColour});
    fileNameEditor_.setColour(juce::TextEditor::outlineColourId, juce::Colour{borderColour});
    addAndMakeVisible(fileNameEditor_);

    for (auto* button : {&armButton_, &startButton_, &stopButton_, &cancelButton_, &assignButton_,
                         &closeButton_}) {
        styleButton(*button);
        addAndMakeVisible(*button);
        button->setTitle(button->getButtonText());
    }
    armButton_.setComponentID("recording-arm");
    startButton_.setComponentID("recording-start");
    stopButton_.setComponentID("recording-stop");
    cancelButton_.setComponentID("recording-cancel");
    assignButton_.setComponentID("recording-assign");
    closeButton_.setComponentID("recording-close");
    armButton_.onClick = [this] {
        if (onArm)
            onArm();
    };
    startButton_.onClick = [this] {
        if (onStart)
            onStart();
    };
    stopButton_.onClick = [this] {
        if (onStop)
            onStop();
    };
    cancelButton_.onClick = [this] {
        if (onCancel)
            onCancel();
    };
    assignButton_.onClick = [this] {
        if (onAssign)
            onAssign();
    };
    closeButton_.onClick = [this] {
        if (onClose)
            onClose();
    };
}

void RecordingPanel::paint(juce::Graphics& graphics) {
    graphics.setColour(juce::Colour{panelColour});
    graphics.fillRoundedRectangle(getLocalBounds().toFloat(), 10.0F);
    graphics.setColour(juce::Colour{borderColour});
    graphics.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5F), 10.0F, 1.0F);
}

void RecordingPanel::resized() {
    auto area = getLocalBounds().reduced(22);
    auto header = area.removeFromTop(38);
    titleLabel_.setBounds(header.removeFromLeft(240));
    closeButton_.setBounds(header.removeFromRight(120).reduced(2));
    area.removeFromTop(8);
    inputDeviceLabel_.setBounds(area.removeFromTop(28));
    inputMeter_.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);

    auto row = area.removeFromTop(32);
    channelBox_.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2));
    modeBox_.setBounds(row.reduced(2));
    row = area.removeFromTop(34);
    thresholdSlider_.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2));
    preRollSlider_.setBounds(row.reduced(2));
    area.removeFromTop(8);

    row = area.removeFromTop(34);
    bankBox_.setBounds(row.removeFromLeft(row.getWidth() / 3).reduced(2));
    padBox_.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(2));
    layerBox_.setBounds(row.reduced(2));
    fileNameEditor_.setBounds(area.removeFromTop(30).reduced(2));
    autoAssignToggle_.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);

    row = area.removeFromTop(38);
    const auto buttonWidth = row.getWidth() / 5;
    armButton_.setBounds(row.removeFromLeft(buttonWidth).reduced(2));
    startButton_.setBounds(row.removeFromLeft(buttonWidth).reduced(2));
    stopButton_.setBounds(row.removeFromLeft(buttonWidth).reduced(2));
    cancelButton_.setBounds(row.removeFromLeft(buttonWidth).reduced(2));
    assignButton_.setBounds(row.reduced(2));
    area.removeFromTop(10);

    stateLabel_.setBounds(area.removeFromTop(30));
    elapsedLabel_.setBounds(area.removeFromTop(28));
    overflowLabel_.setBounds(area.removeFromTop(28));
    resultLabel_.setBounds(area.removeFromTop(52));
}

void RecordingPanel::setProject(const ProjectState& state) {
    bankBox_.setSelectedId(static_cast<int>(state.ui.selectedBank) + 1, juce::dontSendNotification);
    padBox_.setSelectedId(static_cast<int>(state.ui.selectedPad) + 1, juce::dontSendNotification);
    if (layerBox_.getSelectedId() == 0)
        layerBox_.setSelectedId(1, juce::dontSendNotification);
}

void RecordingPanel::setInputDevice(juce::String deviceName, const bool available) {
    inputAvailable_ = available;
    inputDeviceLabel_.setText("Input device: " +
                                  (deviceName.isNotEmpty() ? std::move(deviceName) : "Disabled"),
                              juce::dontSendNotification);
    refreshControlEnablement(CaptureState::idle);
}

void RecordingPanel::setPreferences(const RecordingPreferences& preferences) {
    channelBox_.setSelectedId(static_cast<int>(preferences.channels), juce::dontSendNotification);
    modeBox_.setSelectedId(preferences.thresholdMode ? 2 : 1, juce::dontSendNotification);
    thresholdSlider_.setValue(preferences.thresholdDecibels, juce::dontSendNotification);
    preRollSlider_.setValue(preferences.preRollMilliseconds, juce::dontSendNotification);
    autoAssignToggle_.setToggleState(preferences.autoAssign, juce::dontSendNotification);
}

void RecordingPanel::setCaptureStatus(const CaptureStatus& status, const double sampleRate,
                                      juce::String resultMessage) {
    const auto peak =
        status.inputPeak > 0.0F ? 20.0 * std::log10(static_cast<double>(status.inputPeak)) : -60.0;
    inputMeter_.setValue(std::clamp(peak, -60.0, 0.0), juce::dontSendNotification);
    stateLabel_.setText("State: " + stateName(status.state) +
                            (decodePending_ ? " / Decoding immutable take" : ""),
                        juce::dontSendNotification);
    const auto elapsed =
        sampleRate > 0.0 ? static_cast<double>(status.framesAccepted) / sampleRate : 0.0;
    elapsedLabel_.setText("Elapsed: " + juce::String{elapsed, 2} + " s  ·  " +
                              juce::String{status.framesWritten} + " frames written",
                          juce::dontSendNotification);
    overflowLabel_.setText(status.overflowCount == 0U
                               ? "Capture integrity: clean"
                               : "Capture integrity: INCOMPLETE · " +
                                     juce::String{status.overflowCount} + " overflow(s)",
                           juce::dontSendNotification);
    overflowLabel_.setColour(juce::Label::textColourId,
                             juce::Colour{status.overflowCount == 0U ? tealColour : coralColour});
    if (resultMessage.isNotEmpty())
        resultLabel_.setText(std::move(resultMessage), juce::dontSendNotification);
    refreshControlEnablement(status.state);
}

void RecordingPanel::setDecodePending(const bool pending) {
    decodePending_ = pending;
    refreshControlEnablement(CaptureState::completed);
}

void RecordingPanel::setAssignmentReady(const bool ready) {
    assignmentReady_ = ready;
    assignButton_.setEnabled(ready);
}

RecordingPanel::Configuration RecordingPanel::configuration() const {
    Configuration result;
    result.channels = static_cast<std::uint32_t>(std::max(1, channelBox_.getSelectedId()));
    result.mode = modeBox_.getSelectedId() == 2 ? CaptureMode::threshold : CaptureMode::manual;
    result.thresholdDecibels = static_cast<float>(thresholdSlider_.getValue());
    result.preRollMilliseconds =
        static_cast<std::uint32_t>(std::max(0.0, preRollSlider_.getValue()));
    const auto bank = static_cast<std::size_t>(std::max(1, bankBox_.getSelectedId()) - 1);
    const auto pad = static_cast<std::size_t>(std::max(1, padBox_.getSelectedId()) - 1);
    result.globalPadIndex = bank * padsPerBank + pad;
    result.layerIndex = static_cast<std::size_t>(std::max(1, layerBox_.getSelectedId()) - 1);
    result.autoAssign = autoAssignToggle_.getToggleState();
    result.fileName = fileNameEditor_.getText().trim();
    return result;
}

juce::String RecordingPanel::stateName(const CaptureState state) {
    switch (state) {
    case CaptureState::idle:
        return "Idle";
    case CaptureState::armed:
        return "Armed";
    case CaptureState::waitingForThreshold:
        return "Waiting for threshold";
    case CaptureState::recording:
        return "Recording";
    case CaptureState::stopping:
        return "Stopping";
    case CaptureState::finalizing:
        return "Finalizing";
    case CaptureState::completed:
        return "Completed";
    case CaptureState::cancelled:
        return "Cancelled";
    case CaptureState::failed:
        return "Failed";
    }
    return "Unknown";
}

void RecordingPanel::refreshControlEnablement(const CaptureState state) {
    const auto configurable = state == CaptureState::idle || state == CaptureState::completed ||
                              state == CaptureState::cancelled || state == CaptureState::failed;
    armButton_.setEnabled(configurable && inputAvailable_ && !decodePending_);
    startButton_.setEnabled(state == CaptureState::armed);
    stopButton_.setEnabled(state == CaptureState::recording ||
                           state == CaptureState::waitingForThreshold);
    cancelButton_.setEnabled(state == CaptureState::armed ||
                             state == CaptureState::waitingForThreshold ||
                             state == CaptureState::recording || state == CaptureState::stopping ||
                             state == CaptureState::finalizing);
    assignButton_.setEnabled(assignmentReady_);
    channelBox_.setEnabled(configurable);
    modeBox_.setEnabled(configurable);
    thresholdSlider_.setEnabled(configurable && modeBox_.getSelectedId() == 2);
    preRollSlider_.setEnabled(configurable && modeBox_.getSelectedId() == 2);
    bankBox_.setEnabled(configurable);
    padBox_.setEnabled(configurable);
    layerBox_.setEnabled(configurable);
    autoAssignToggle_.setEnabled(configurable);
    fileNameEditor_.setEnabled(configurable);
}
} // namespace padflow
