#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>

namespace padflow {
class Project final {
  public:
    static Project createEmpty(juce::String projectName = "Untitled", juce::String fixedUuid = {});

    [[nodiscard]] int schemaVersion() const noexcept;
    [[nodiscard]] const juce::String& uuid() const noexcept;
    [[nodiscard]] const juce::String& name() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;

    void setName(juce::String newName);
    void restoreRevision(std::uint64_t restoredRevision) noexcept;

  private:
    Project(juce::String projectName, juce::String projectUuid);

    juce::String projectName_;
    juce::String projectUuid_;
    std::uint64_t revision_{0};
};
} // namespace padflow
