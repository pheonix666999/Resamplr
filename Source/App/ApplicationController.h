#pragma once

#include "Model/Project.h"
#include "Utilities/BackgroundJobSystem.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace padflow {
class ApplicationController final {
  public:
    ApplicationController();

    void createEmptyProject(juce::String name = "Untitled", juce::String fixedUuid = {});
    [[nodiscard]] juce::Result restoreProject(Project project);
    [[nodiscard]] const Project& project() const noexcept;
    [[nodiscard]] bool isCurrentJobTarget(const JobSpec& spec) const noexcept;
    [[nodiscard]] juce::Result renamePad(std::size_t globalIndex, juce::String name);
    [[nodiscard]] juce::Result recolourPad(std::size_t globalIndex, std::uint32_t colourArgb);
    [[nodiscard]] juce::Result setPadParameters(std::size_t globalIndex, PadParameters parameters);
    [[nodiscard]] juce::Result setLayer(std::size_t globalIndex, std::size_t layerIndex,
                                        SampleLayer layer);
    [[nodiscard]] juce::Result setPadMappings(std::size_t globalIndex, std::uint8_t midiNote,
                                              juce::String keyboardKey);
    [[nodiscard]] juce::Result setAudioSettings(AudioSettings settings);
    [[nodiscard]] juce::Result setMidiSettings(MidiSettings settings);
    [[nodiscard]] juce::Result setUiState(ProjectUiState state);
    [[nodiscard]] juce::Result clearPad(std::size_t globalIndex);
    [[nodiscard]] juce::Result copyPad(std::size_t globalIndex);
    [[nodiscard]] juce::Result pastePad(std::size_t globalIndex);
    [[nodiscard]] juce::Result duplicatePad(std::size_t sourceGlobalIndex,
                                            std::size_t destinationGlobalIndex);
    [[nodiscard]] juce::Result commitImportedLayer(const JobSpec& target, std::size_t globalIndex,
                                                   std::size_t layerIndex,
                                                   ExternalAssetReference asset);
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

    Project project_;
    std::optional<Pad> clipboard_;
    std::vector<ProjectEdit> undoHistory_;
    std::vector<ProjectEdit> redoHistory_;
};
} // namespace padflow
