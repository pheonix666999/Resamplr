#pragma once

#include "PadModel.h"

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>

namespace padflow {
class Project final {
  public:
    static Project createEmpty(juce::String projectName = "Untitled", juce::String fixedUuid = {});

    [[nodiscard]] int schemaVersion() const noexcept;
    [[nodiscard]] const juce::String& uuid() const noexcept;
    [[nodiscard]] const juce::String& name() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const ProjectState& state() const noexcept;
    [[nodiscard]] const PadBank& bank(std::size_t index) const;
    [[nodiscard]] const Pad& pad(std::size_t globalIndex) const;
    [[nodiscard]] const Pad* findPadByUuid(const juce::String& uuid) const noexcept;

    void setName(juce::String newName);
    [[nodiscard]] juce::Result replacePad(std::size_t globalIndex, Pad replacement);
    [[nodiscard]] juce::Result restoreState(ProjectState restoredState,
                                            std::uint64_t restoredRevision);
    void restoreRevision(std::uint64_t restoredRevision) noexcept;

  private:
    Project(juce::String projectName, juce::String projectUuid);

    ProjectState state_;
    std::uint64_t revision_{0};
};
} // namespace padflow
