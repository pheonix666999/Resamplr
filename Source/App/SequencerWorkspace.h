#pragma once

#include "App/ApplicationController.h"
#include "Audio/AudioRuntime.h"
#include "Input/InputRouter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <functional>

namespace padflow {
class SequencerWorkspace final : public juce::Component, private juce::Timer {
  public:
    SequencerWorkspace(ApplicationController& controller, AudioRuntime& runtime,
                       InputRouter& input);
    ~SequencerWorkspace() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void refresh();
    [[nodiscard]] bool toggleStep(std::size_t lane, std::size_t step);
    [[nodiscard]] bool setSelectedEventVelocity(std::uint8_t velocity);
    [[nodiscard]] bool setSelectedEventProbability(ProbabilityQ32 probability);
    [[nodiscard]] bool setSelectedEventRatchet(std::uint8_t count, MusicalDuration spacing);
    [[nodiscard]] bool setSelectedEventNudge(MicroOffsetQ16 nudge);
    [[nodiscard]] bool stepRecordPad(std::size_t lane, std::uint8_t velocity);
    [[nodiscard]] bool beginLiveRecording(PatternRecordMode mode);
    [[nodiscard]] bool finishLiveRecording();
    void cancelLiveRecording() noexcept;
    [[nodiscard]] std::int64_t displayedPlayheadFrame() const noexcept;

    std::function<void()> onClose;
    std::function<void()> onProjectChanged;

  private:
    class StepGrid final : public juce::Component {
      public:
        explicit StepGrid(SequencerWorkspace& owner) noexcept;
        void paint(juce::Graphics& graphics) override;
        void mouseDown(const juce::MouseEvent& event) override;

      private:
        SequencerWorkspace& owner_;
    };

    void timerCallback() override;
    [[nodiscard]] const Pattern* selectedPattern() const noexcept;
    [[nodiscard]] const SequenceEvent* selectedEvent() const noexcept;
    void applyResult(juce::Result result);
    void updateSelectedEventFromControls();

    ApplicationController& controller_;
    AudioRuntime& runtime_;
    InputRouter& input_;
    PatternRecorder recorder_;
    juce::TextButton closeButton_{"Back to Sampler"};
    juce::TextButton playButton_{"Play"};
    juce::TextButton stopButton_{"Stop"};
    juce::TextButton recordButton_{"Record"};
    juce::TextButton panicButton_{"Panic"};
    juce::ToggleButton loopButton_{"Loop"};
    juce::ToggleButton metronomeButton_{"Metronome"};
    juce::ComboBox countInBox_;
    juce::Slider tempoSlider_;
    juce::ComboBox patternBox_;
    juce::TextEditor patternName_;
    juce::TextButton addPatternButton_{"New"};
    juce::TextButton duplicatePatternButton_{"Duplicate"};
    juce::TextButton deletePatternButton_{"Delete"};
    juce::TextButton clearPatternButton_{"Clear"};
    juce::Viewport gridViewport_;
    StepGrid grid_;
    juce::Slider velocitySlider_;
    juce::Slider probabilitySlider_;
    juce::Slider ratchetSlider_;
    juce::Slider nudgeSlider_;
    juce::Label statusLabel_;
    std::int64_t displayedPlayheadFrame_{0};
    bool refreshing_{false};
};
} // namespace padflow
