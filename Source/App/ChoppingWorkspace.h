#pragma once

#include "App/ApplicationController.h"
#include "Audio/SliceAuditionController.h"
#include "Chopping/AssignmentPlan.h"
#include "Chopping/ChoppingSession.h"
#include "Chopping/LazyMarkerCapture.h"
#include "Chopping/TransientAnalysis.h"
#include "Sampling/SampleAsset.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace padflow {
class ChoppingWorkspace final : public juce::Component {
  public:
    ChoppingWorkspace(ApplicationController& controller, BackgroundJobSystem& jobs,
                      SampleAssetRegistry& assets, PreviewPlayer& previewPlayer);
    ~ChoppingWorkspace() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

    [[nodiscard]] juce::Result beginForLayer(std::size_t globalPadIndex, std::size_t layerIndex);
    void setMode(SliceAlgorithm mode);
    [[nodiscard]] SliceAlgorithm mode() const noexcept;
    [[nodiscard]] juce::Result generateEqual(std::int64_t count);
    [[nodiscard]] juce::Result generateFixed(double displayLength, SliceDisplayUnit unit,
                                             SliceRemainderPolicy remainder);
    [[nodiscard]] bool submitTransientAnalysis(TransientAnalysisParameters parameters);
    [[nodiscard]] bool handleCompletedJob(const JobResult& result);
    [[nodiscard]] juce::Result addMarker(std::int64_t frame);
    [[nodiscard]] juce::Result deleteMarker(std::int64_t frame);
    [[nodiscard]] juce::Result moveMarker(std::int64_t frame, std::int64_t replacement);
    [[nodiscard]] bool undoSessionEdit();
    [[nodiscard]] bool redoSessionEdit();
    [[nodiscard]] bool selectSlice(std::size_t index);
    [[nodiscard]] bool selectPreviousSlice();
    [[nodiscard]] bool selectNextSlice();
    [[nodiscard]] bool auditionSelected();
    [[nodiscard]] bool auditionAll();
    void stopAudition();
    [[nodiscard]] bool startLazy(LazyCaptureSettings settings);
    [[nodiscard]] bool captureLazyMarker(LazyMarkerSource source);
    [[nodiscard]] LazyDrainResult drainLazyMarkers();
    void stopLazy();
    [[nodiscard]] juce::Result previewAssignment(AssignmentDestinationMode destinationMode,
                                                 std::size_t startingGlobalPad,
                                                 std::size_t startingLayer,
                                                 bool continueAcrossPads);
    [[nodiscard]] juce::Result resolveAllConflicts(AssignmentConflictDecision decision);
    [[nodiscard]] juce::Result resolveConflict(std::size_t destinationIndex,
                                               AssignmentConflictDecision decision);
    [[nodiscard]] juce::Result commitAssignment(AssignmentCommitReport& report);
    void cancel();
    void service();

    [[nodiscard]] const ChoppingSession& session() const noexcept;
    [[nodiscard]] const std::optional<AssignmentPlan>& assignmentPlan() const noexcept;
    [[nodiscard]] bool analysisPending() const noexcept;
    [[nodiscard]] bool lazyActive() const noexcept;
    [[nodiscard]] LazyMarkerCapture& lazyMarkerCapture() noexcept;
    [[nodiscard]] juce::String statusText() const;

    std::function<void(const SliceSet*, std::size_t)> onSlicesChanged;
    std::function<void()> onProjectCommitted;
    std::function<void()> onCancel;

  private:
    void configureControls();
    void refresh();
    void publishSlices();
    void showResult(const juce::Result& result, juce::String success);
    [[nodiscard]] const ExternalAssetReference* sourceReference() const noexcept;
    [[nodiscard]] std::shared_ptr<const SampleAsset> sourceAsset() const;
    [[nodiscard]] std::int64_t selectedBoundary() const noexcept;
    [[nodiscard]] std::int64_t snapMarker(std::int64_t frame) const noexcept;

    ApplicationController& controller_;
    BackgroundJobSystem& jobs_;
    SampleAssetRegistry& assets_;
    SliceAuditionController audition_;
    ChoppingSession session_;
    LazyMarkerCapture lazyCapture_;
    std::optional<LazyCaptureSettings> lazySettings_;
    std::optional<JobHandle> analysisJob_;
    std::optional<AssignmentPlan> assignmentPlan_;
    SliceAlgorithm mode_{SliceAlgorithm::equal};
    juce::String status_{"Choose a loaded layer, then open Chop"};

    juce::Label title_;
    juce::ComboBox modeBox_;
    juce::Slider sliceCount_;
    juce::Slider fixedLength_;
    juce::ComboBox fixedUnit_;
    juce::ComboBox remainderPolicy_;
    juce::Slider sensitivity_;
    juce::Slider minimumDuration_;
    juce::Slider attackLookBack_;
    juce::TextButton analyseButton_{"Analyse"};
    juce::TextButton addMarkerButton_{"Add Marker"};
    juce::TextButton deleteMarkerButton_{"Delete Marker"};
    juce::TextButton clearMarkersButton_{"Clear"};
    juce::TextButton undoSessionButton_{"Undo Marker"};
    juce::TextButton redoSessionButton_{"Redo Marker"};
    juce::ToggleButton markerZeroSnap_{"Zero-crossing snap"};
    juce::Slider markerGridFrames_;
    juce::TextButton previousSliceButton_{"Previous"};
    juce::TextButton nextSliceButton_{"Next"};
    juce::Label sliceReadout_;
    juce::TextButton auditionSelectedButton_{"Audition Slice"};
    juce::TextButton auditionAllButton_{"Audition All"};
    juce::TextButton stopAuditionButton_{"Stop"};
    juce::TextButton lazyStartButton_{"Start Lazy"};
    juce::TextButton placeLazyMarkerButton_{"Place Marker"};
    juce::TextButton lazyStopButton_{"Stop Lazy"};
    juce::ComboBox destinationBank_;
    juce::Slider destinationPad_;
    juce::ComboBox destinationMode_;
    juce::Slider destinationLayer_;
    juce::ToggleButton continueAcrossPads_{"Continue layers across pads"};
    juce::ComboBox overwritePolicy_;
    juce::Slider conflictIndex_;
    juce::TextButton replaceConflictButton_{"Replace Selected"};
    juce::TextButton skipConflictButton_{"Skip Selected"};
    juce::ToggleButton stayInEditor_{"Stay in editor"};
    juce::TextButton previewAssignmentButton_{"Preview Assignment"};
    juce::TextButton commitAssignmentButton_{"Commit Assignment"};
    juce::TextButton cancelButton_{"Cancel"};
    juce::Label assignmentReadout_;
    juce::Label statusLabel_;
};
} // namespace padflow
