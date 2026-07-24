#pragma once

#include "App/ApplicationController.h"
#include "Audio/AudioRuntime.h"
#include "Audio/PlaybackStatePublisher.h"
#include "Input/InputRouter.h"
#include "Sampling/SamplePreviewController.h"
#include "Serialization/ProjectSerializer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>

namespace padflow {
class SamplerView final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          private juce::KeyListener,
                          private juce::TextEditor::Listener,
                          private juce::Timer {
  public:
    SamplerView(ApplicationController& controller, BackgroundJobSystem& jobs,
                SampleAssetRegistry& assets, AudioRuntime& runtime,
                PlaybackStatePublisher& publisher, InputRouter& input,
                SamplePreviewController& preview);
    ~SamplerView() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    [[nodiscard]] bool selectBank(std::size_t bankIndex);
    [[nodiscard]] bool selectPad(std::size_t globalPadIndex);
    [[nodiscard]] std::size_t selectedGlobalPad() const noexcept;
    [[nodiscard]] std::size_t visiblePadCount() const noexcept;
    [[nodiscard]] bool queueImportFiles(const juce::StringArray& files,
                                        std::size_t firstGlobalPadIndex, bool overwriteConfirmed);
    void processPendingJobs();

  private:
    class PadButton final : public juce::TextButton {
      public:
        std::function<void(const juce::MouseEvent&)> onPadMouseDown;
        std::function<void(const juce::MouseEvent&)> onPadMouseUp;

        void mouseDown(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;
    };

    struct QueuedImport final {
        juce::File file;
        std::size_t globalPadIndex{0U};
        std::size_t layerIndex{0U};
    };

    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* originatingComponent) override;
    void textEditorReturnKeyPressed(juce::TextEditor& editor) override;
    void textEditorFocusLost(juce::TextEditor& editor) override;

    void configureControls();
    void configureEditorControl(juce::Slider& slider, juce::String name, double minimum,
                                double maximum, double interval);
    void refreshAll();
    void refreshPads();
    void refreshEditor();
    void refreshStatus();
    void publishModel(bool markModified);
    void applyParameterControls();
    void applyMappings();
    void submitNextImport();
    void handleCompletedJob(const JobResult& result);
    void showImportChooser(std::size_t globalPadIndex);
    void showSaveChooser();
    void showOpenChooser();
    void showPadMenu(std::size_t globalPadIndex);
    void showAudioSettings();
    void showMidiSettings();
    void auditionSelectedLayer();
    void clearSelectedLayer();
    void confirmAndQueueFiles(juce::StringArray files, std::size_t firstGlobalPadIndex);
    [[nodiscard]] int padAtPosition(juce::Point<int> position) const noexcept;
    [[nodiscard]] static bool isSupportedAudioFile(const juce::File& file);
    [[nodiscard]] juce::String selectedSampleName() const;
    [[nodiscard]] std::size_t selectedLayerIndex() const noexcept;
    void setOperationMessage(juce::String message, bool isError);

    ApplicationController& controller_;
    BackgroundJobSystem& jobs_;
    SampleAssetRegistry& assets_;
    AudioRuntime& runtime_;
    PlaybackStatePublisher& publisher_;
    InputRouter& input_;
    SamplePreviewController& preview_;

    juce::Label productLabel_;
    juce::Label projectLabel_;
    juce::Label modifiedLabel_;
    juce::TextButton newButton_{"New"};
    juce::TextButton openButton_{"Open"};
    juce::TextButton saveButton_{"Save"};
    juce::TextButton undoButton_{"Undo"};
    juce::TextButton redoButton_{"Redo"};
    juce::TextButton audioButton_{"Audio Settings"};
    juce::TextButton midiButton_{"MIDI Settings"};
    juce::Label cpuLabel_;
    juce::Label audioStateLabel_;

    std::array<juce::TextButton, padBankCount> bankButtons_;
    std::array<PadButton, padsPerBank> padButtons_;

    juce::Label selectedPadLabel_;
    juce::TextEditor padNameEditor_;
    juce::ComboBox layerSelector_;
    juce::Label sampleNameLabel_;
    juce::TextButton importButton_{"Import / Replace"};
    juce::TextButton clearLayerButton_{"Clear Layer"};
    juce::TextButton clearPadButton_{"Clear Pad"};
    juce::TextButton auditionButton_{"Audition"};
    juce::Slider gainSlider_;
    juce::Slider panSlider_;
    juce::Slider coarseSlider_;
    juce::Slider fineSlider_;
    juce::Slider attackSlider_;
    juce::Slider decaySlider_;
    juce::Slider sustainSlider_;
    juce::Slider releaseSlider_;
    juce::Slider chokeSlider_;
    juce::Slider voicesSlider_;
    juce::Slider midiNoteSlider_;
    juce::ComboBox playbackModeBox_;
    juce::ComboBox polyphonyBox_;
    juce::TextEditor keyboardEditor_;

    juce::Label deviceStatusLabel_;
    juce::Label midiStatusLabel_;
    juce::Label memoryStatusLabel_;
    juce::Label operationStatusLabel_;

    std::deque<QueuedImport> importQueue_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::array<bool, 256U> heldUiKeys_{};
    juce::String operationMessage_{"Ready"};
    std::uint64_t lastSeenRevision_{0U};
    bool importActive_{false};
    bool refreshing_{false};
    bool modified_{false};
    bool lastOperationWasError_{false};
};
} // namespace padflow
