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
    [[nodiscard]] const Project& project() const noexcept;
    [[nodiscard]] bool isCurrentJobTarget(const JobSpec& spec) const noexcept;
    [[nodiscard]] juce::Result renamePad(std::size_t globalIndex, juce::String name);
    [[nodiscard]] juce::Result recolourPad(std::size_t globalIndex, std::uint32_t colourArgb);
    [[nodiscard]] juce::Result setPadParameters(std::size_t globalIndex, PadParameters parameters);
    [[nodiscard]] juce::Result setLayer(std::size_t globalIndex, std::size_t layerIndex,
                                        SampleLayer layer);
    [[nodiscard]] juce::Result clearPad(std::size_t globalIndex);
    [[nodiscard]] juce::Result copyPad(std::size_t globalIndex);
    [[nodiscard]] juce::Result pastePad(std::size_t globalIndex);
    [[nodiscard]] juce::Result duplicatePad(std::size_t sourceGlobalIndex,
                                            std::size_t destinationGlobalIndex);
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

  private:
    struct PadEdit final {
        std::size_t globalIndex{0U};
        Pad before;
        Pad after;
        juce::String description;
    };

    [[nodiscard]] juce::Result commitPadEdit(std::size_t globalIndex, Pad replacement,
                                             juce::String description);

    Project project_;
    std::optional<Pad> clipboard_;
    std::vector<PadEdit> undoHistory_;
    std::vector<PadEdit> redoHistory_;
};
} // namespace padflow
