#include "Project.h"

#include "App/ProductInfo.h"

#include <stdexcept>
#include <utility>

namespace padflow {
Project Project::createEmpty(juce::String projectName, juce::String fixedUuid) {
    if (fixedUuid.isEmpty())
        fixedUuid = juce::Uuid().toString();

    return Project{std::move(projectName), std::move(fixedUuid)};
}

Project::Project(juce::String projectName, juce::String projectUuid)
    : state_(makeDefaultProjectState(projectUuid, std::move(projectName))) {}

int Project::schemaVersion() const noexcept {
    return product::schemaVersion;
}

const juce::String& Project::uuid() const noexcept {
    return state_.projectUuid;
}

const juce::String& Project::name() const noexcept {
    return state_.projectName;
}

std::uint64_t Project::revision() const noexcept {
    return revision_;
}

const ProjectState& Project::state() const noexcept {
    return state_;
}

const PadBank& Project::bank(const std::size_t index) const {
    return state_.banks.at(index);
}

const Pad& Project::pad(const std::size_t globalIndex) const {
    if (globalIndex >= totalPadCount)
        throw std::out_of_range("Pad index is outside 0..63");
    return state_.banks[globalIndex / padsPerBank].pads[globalIndex % padsPerBank];
}

const Pad* Project::findPadByUuid(const juce::String& uuid) const noexcept {
    for (const auto& bankEntry : state_.banks)
        for (const auto& padEntry : bankEntry.pads)
            if (padEntry.uuid == uuid)
                return &padEntry;
    return nullptr;
}

void Project::setName(juce::String newName) {
    newName = newName.trim();
    if (newName.isNotEmpty() && newName.length() <= 128 && newName != state_.projectName) {
        state_.projectName = std::move(newName);
        ++revision_;
    }
}

juce::Result Project::replacePad(const std::size_t globalIndex, Pad replacement) {
    if (globalIndex >= totalPadCount)
        return juce::Result::fail("Pad index is outside 0..63");

    auto candidate = state_;
    candidate.banks[globalIndex / padsPerBank].pads[globalIndex % padsPerBank] =
        std::move(replacement);
    if (const auto validation = validateProjectState(candidate); validation.failed())
        return validation;

    state_ = std::move(candidate);
    ++revision_;
    return juce::Result::ok();
}

juce::Result Project::restoreState(ProjectState restoredState,
                                   const std::uint64_t restoredRevision) {
    if (const auto validation = validateProjectState(restoredState); validation.failed())
        return validation;
    state_ = std::move(restoredState);
    revision_ = restoredRevision;
    return juce::Result::ok();
}

void Project::restoreRevision(const std::uint64_t restoredRevision) noexcept {
    revision_ = restoredRevision;
}
} // namespace padflow
