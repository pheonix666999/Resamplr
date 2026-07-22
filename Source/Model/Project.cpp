#include "Project.h"

#include "App/ProductInfo.h"

#include <utility>

namespace padflow {
Project Project::createEmpty(juce::String projectName, juce::String fixedUuid) {
    if (fixedUuid.isEmpty())
        fixedUuid = juce::Uuid().toString();

    return Project{std::move(projectName), std::move(fixedUuid)};
}

Project::Project(juce::String projectName, juce::String projectUuid)
    : projectName_(std::move(projectName)), projectUuid_(std::move(projectUuid)) {}

int Project::schemaVersion() const noexcept {
    return product::schemaVersion;
}

const juce::String& Project::uuid() const noexcept {
    return projectUuid_;
}

const juce::String& Project::name() const noexcept {
    return projectName_;
}

std::uint64_t Project::revision() const noexcept {
    return revision_;
}

void Project::setName(juce::String newName) {
    if (newName != projectName_) {
        projectName_ = std::move(newName);
        ++revision_;
    }
}

void Project::restoreRevision(const std::uint64_t restoredRevision) noexcept {
    revision_ = restoredRevision;
}
} // namespace padflow
