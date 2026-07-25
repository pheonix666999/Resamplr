#pragma once

#include "Model/Project.h"
#include "Utilities/BackgroundJobSystem.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace padflow {
struct AssignmentPlan;
struct AssignmentCommitReport;

class ApplicationController final {
  public:
    ApplicationController();

    void createEmptyProject(juce::String name = "Untitled", juce::String fixedUuid = {});
    [[nodiscard]] juce::Result restoreProject(Project project);
    [[nodiscard]] std::size_t refreshExternalAssetAvailability();
    [[nodiscard]] const Project& project() const noexcept;
    [[nodiscard]] bool isCurrentJobTarget(const JobSpec& spec) const noexcept;
    [[nodiscard]] juce::Result renamePad(std::size_t globalIndex, juce::String name);
    [[nodiscard]] juce::Result recolourPad(std::size_t globalIndex, std::uint32_t colourArgb);
    [[nodiscard]] juce::Result setPadParameters(std::size_t globalIndex, PadParameters parameters);
    [[nodiscard]] juce::Result setLayer(std::size_t globalIndex, std::size_t layerIndex,
                                        SampleLayer layer);
    [[nodiscard]] juce::Result setLayerTrim(std::size_t globalIndex, std::size_t layerIndex,
                                            std::uint64_t startFrame, std::uint64_t endFrame);
    [[nodiscard]] juce::Result setLayerLoop(std::size_t globalIndex, std::size_t layerIndex,
                                            std::uint64_t startFrame, std::uint64_t endFrame);
    [[nodiscard]] juce::Result setLayerLoopEnabled(std::size_t globalIndex, std::size_t layerIndex,
                                                   bool enabled);
    [[nodiscard]] juce::Result setLayerReverseEnabled(std::size_t globalIndex,
                                                      std::size_t layerIndex, bool enabled);
    [[nodiscard]] juce::Result setLayerZeroCrossingSnap(std::size_t globalIndex,
                                                        std::size_t layerIndex, bool enabled);
    [[nodiscard]] juce::Result resetLayerTrim(std::size_t globalIndex, std::size_t layerIndex);
    [[nodiscard]] juce::Result resetLayerLoop(std::size_t globalIndex, std::size_t layerIndex);
    [[nodiscard]] juce::Result setPadMappings(std::size_t globalIndex, std::uint8_t midiNote,
                                              juce::String keyboardKey);
    [[nodiscard]] juce::Result setAudioSettings(AudioSettings settings);
    [[nodiscard]] juce::Result setMidiSettings(MidiSettings settings);
    [[nodiscard]] juce::Result setRecordingPreferences(RecordingPreferences preferences);
    [[nodiscard]] juce::Result setUiState(ProjectUiState state);
    [[nodiscard]] juce::Result clearPad(std::size_t globalIndex);
    [[nodiscard]] juce::Result copyPad(std::size_t globalIndex);
    [[nodiscard]] juce::Result pastePad(std::size_t globalIndex);
    [[nodiscard]] juce::Result duplicatePad(std::size_t sourceGlobalIndex,
                                            std::size_t destinationGlobalIndex);
    [[nodiscard]] juce::Result commitImportedLayer(const JobSpec& target, std::size_t globalIndex,
                                                   std::size_t layerIndex,
                                                   ExternalAssetReference asset);
    [[nodiscard]] juce::Result commitDerivedLayer(const JobSpec& target, std::size_t globalIndex,
                                                  std::size_t layerIndex,
                                                  const juce::String& expectedSourceAssetUuid,
                                                  ExternalAssetReference derivedAsset,
                                                  DerivedAssetRecord provenance,
                                                  SamplePlaybackSettings playback);
    [[nodiscard]] juce::Result commitRecordedLayer(const JobSpec& target, std::size_t globalIndex,
                                                   std::size_t layerIndex,
                                                   const juce::String& expectedLayerUuid,
                                                   ExternalAssetReference recordedAsset,
                                                   RecordedAssetRecord provenance);
    [[nodiscard]] juce::Result commitSliceAssignment(const AssignmentPlan& plan,
                                                     AssignmentCommitReport& report);
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

  private:
    struct ProjectEdit final {
        ProjectState before;
        ProjectState after;
        juce::String description;
    };

    [[nodiscard]] juce::Result commitPadEdit(std::size_t globalIndex, Pad replacement,
                                             juce::String description);
    [[nodiscard]] juce::Result commitProjectEdit(ProjectState replacement,
                                                 juce::String description);
    [[nodiscard]] const ExternalAssetReference*
    assetForLayer(std::size_t globalIndex, std::size_t layerIndex) const noexcept;
    [[nodiscard]] juce::Result commitLayerPlayback(std::size_t globalIndex, std::size_t layerIndex,
                                                   SamplePlaybackSettings playback,
                                                   juce::String description);

    Project project_;
    std::optional<Pad> clipboard_;
    std::vector<ProjectEdit> undoHistory_;
    std::vector<ProjectEdit> redoHistory_;
};
} // namespace padflow
