#pragma once

#include "Model/Project.h"
#include "Utilities/BackgroundJobSystem.h"

namespace padflow {
class ApplicationController final {
  public:
    ApplicationController();

    void createEmptyProject(juce::String name = "Untitled", juce::String fixedUuid = {});
    [[nodiscard]] const Project& project() const noexcept;
    [[nodiscard]] bool isCurrentJobTarget(const JobSpec& spec) const noexcept;

  private:
    Project project_;
};
} // namespace padflow
