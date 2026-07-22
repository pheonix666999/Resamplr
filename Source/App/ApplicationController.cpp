#include "ApplicationController.h"

#include <utility>

namespace padflow {
ApplicationController::ApplicationController() : project_(Project::createEmpty()) {}

void ApplicationController::createEmptyProject(juce::String name, juce::String fixedUuid) {
    project_ = Project::createEmpty(std::move(name), std::move(fixedUuid));
}

const Project& ApplicationController::project() const noexcept {
    return project_;
}

bool ApplicationController::isCurrentJobTarget(const JobSpec& spec) const noexcept {
    return spec.ownerUuid == project_.uuid() && spec.targetRevision == project_.revision();
}
} // namespace padflow
