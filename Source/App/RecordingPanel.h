#pragma once

#include "Audio/CaptureWriter.h"
#include "Model/PadModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <functional>

namespace padflow {
class RecordingPanel final : public juce::Component {
  public:
    struct Configuration final {
        std::uint32_t channels{1U};
        CaptureMode mode{CaptureMode::manual};
        float thresholdDecibels{-24.0F};
        std::uint32_t preRollMilliseconds{0U};
        std::size_t globalPadIndex{0U};
        std::size_t layerIndex{0U};
        bool autoAssign{true};
        juce::String fileName{"Recording.wav"};
    };

    RecordingPanel();

    void paint(juce::Graphics& graphics) override;
    void resized() override;

    void setProject(const ProjectState& state);
    void setInputDevice(juce::String deviceName, bool available);
    void setPreferences(const RecordingPreferences& preferences);
    void setCaptureStatus(const CaptureStatus& status, double sampleRate,
                          juce::String resultMessage = {});
    void setDecodePending(bool pending);
    void setAssignmentReady(bool ready);
    [[nodiscard]] Configuration configuration() const;

    std::function<void()> onArm;
    std::function<void()> onStart;
    std::function<void()> onStop;
    std::function<void()> onCancel;
    std::function<void()> onAssign;
    std::function<void()> onClose;

  private:
    static juce::String stateName(CaptureState state);
    void configureControls();
    void refreshControlEnablement(CaptureState state);

    juce::Label titleLabel_;
    juce::Label inputDeviceLabel_;
    juce::Label stateLabel_;
    juce::Label elapsedLabel_;
    juce::Label overflowLabel_;
    juce::Label resultLabel_;
    juce::Slider inputMeter_;
    juce::ComboBox channelBox_;
    juce::ComboBox modeBox_;
    juce::Slider thresholdSlider_;
    juce::Slider preRollSlider_;
    juce::ComboBox bankBox_;
    juce::ComboBox padBox_;
    juce::ComboBox layerBox_;
    juce::ToggleButton autoAssignToggle_{"Auto-assign completed take"};
    juce::TextEditor fileNameEditor_;
    juce::TextButton armButton_{"Arm"};
    juce::TextButton startButton_{"Record"};
    juce::TextButton stopButton_{"Stop"};
    juce::TextButton cancelButton_{"Cancel"};
    juce::TextButton assignButton_{"Assign take"};
    juce::TextButton closeButton_{"Back to pads"};
    bool inputAvailable_{false};
    bool decodePending_{false};
    bool assignmentReady_{false};
};
} // namespace padflow
