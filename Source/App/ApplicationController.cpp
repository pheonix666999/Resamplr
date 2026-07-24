#include "ApplicationController.h"

#include <algorithm>
#include <utility>

namespace padflow {
ApplicationController::ApplicationController() : project_(Project::createEmpty()) {}

void ApplicationController::createEmptyProject(juce::String name, juce::String fixedUuid) {
    project_ = Project::createEmpty(std::move(name), std::move(fixedUuid));
    clipboard_.reset();
    undoHistory_.clear();
    redoHistory_.clear();
}

const Project& ApplicationController::project() const noexcept {
    return project_;
}

bool ApplicationController::isCurrentJobTarget(const JobSpec& spec) const noexcept {
    const auto targetExists =
        spec.targetUuid == project_.uuid() || project_.findPadByUuid(spec.targetUuid) != nullptr;
    return spec.ownerUuid == project_.uuid() && targetExists &&
           spec.targetRevision == project_.revision();
}

juce::Result ApplicationController::renamePad(const std::size_t globalIndex, juce::String name) {
    if (globalIndex >= totalPadCount)
        return juce::Result::fail("Pad index is outside 0..63");
    auto replacement = project_.pad(globalIndex);
    replacement.name = name.trim();
    return commitPadEdit(globalIndex, std::move(replacement), "Rename pad");
}

juce::Result ApplicationController::recolourPad(const std::size_t globalIndex,
                                                const std::uint32_t colourArgb) {
    if (globalIndex >= totalPadCount)
        return juce::Result::fail("Pad index is outside 0..63");
    auto replacement = project_.pad(globalIndex);
    replacement.colourArgb = colourArgb;
    return commitPadEdit(globalIndex, std::move(replacement), "Change pad colour");
}

juce::Result ApplicationController::setPadParameters(const std::size_t globalIndex,
                                                     PadParameters parameters) {
    if (globalIndex >= totalPadCount)
        return juce::Result::fail("Pad index is outside 0..63");
    auto replacement = project_.pad(globalIndex);
    replacement.parameters = parameters;
    return commitPadEdit(globalIndex, std::move(replacement), "Change pad parameters");
}

juce::Result ApplicationController::setLayer(const std::size_t globalIndex,
                                             const std::size_t layerIndex, SampleLayer layer) {
    if (globalIndex >= totalPadCount)
        return juce::Result::fail("Pad index is outside 0..63");
    if (layerIndex >= minimumLayersPerPad)
        return juce::Result::fail("Layer index is outside 0..3");
    auto replacement = project_.pad(globalIndex);
    replacement.layers[layerIndex] = std::move(layer);
    return commitPadEdit(globalIndex, std::move(replacement), "Change sample layer");
}

juce::Result ApplicationController::clearPad(const std::size_t globalIndex) {
    if (globalIndex >= totalPadCount)
        return juce::Result::fail("Pad index is outside 0..63");
    return commitPadEdit(globalIndex, makeClearedPad(project_.state(), globalIndex), "Clear pad");
}

juce::Result ApplicationController::copyPad(const std::size_t globalIndex) {
    if (globalIndex >= totalPadCount)
        return juce::Result::fail("Pad index is outside 0..63");
    clipboard_ = project_.pad(globalIndex);
    return juce::Result::ok();
}

juce::Result ApplicationController::pastePad(const std::size_t globalIndex) {
    if (globalIndex >= totalPadCount)
        return juce::Result::fail("Pad index is outside 0..63");
    if (!clipboard_.has_value())
        return juce::Result::fail("No pad has been copied");

    const auto& destination = project_.pad(globalIndex);
    auto replacement = *clipboard_;
    replacement.uuid = destination.uuid;
    replacement.midiNote = destination.midiNote;
    replacement.keyboardKey = destination.keyboardKey;
    for (std::size_t index = 0; index < minimumLayersPerPad; ++index)
        replacement.layers[index].uuid = destination.layers[index].uuid;
    return commitPadEdit(globalIndex, std::move(replacement), "Paste pad");
}

juce::Result ApplicationController::duplicatePad(const std::size_t sourceGlobalIndex,
                                                 const std::size_t destinationGlobalIndex) {
    if (sourceGlobalIndex >= totalPadCount || destinationGlobalIndex >= totalPadCount)
        return juce::Result::fail("Pad index is outside 0..63");
    auto replacement = project_.pad(sourceGlobalIndex);
    regeneratePadIdentity(replacement);
    replacement.name = replacement.name.trim().substring(0, 59) + " Copy";
    replacement.midiNote = project_.pad(destinationGlobalIndex).midiNote;
    replacement.keyboardKey = project_.pad(destinationGlobalIndex).keyboardKey;
    return commitPadEdit(destinationGlobalIndex, std::move(replacement), "Duplicate pad");
}

juce::Result ApplicationController::commitImportedLayer(const JobSpec& target,
                                                        const std::size_t globalIndex,
                                                        const std::size_t layerIndex,
                                                        ExternalAssetReference asset) {
    if (!isCurrentJobTarget(target))
        return juce::Result::fail("Import completion targets stale project state");
    if (globalIndex >= totalPadCount || layerIndex >= minimumLayersPerPad)
        return juce::Result::fail("Import completion targets an invalid layer");
    if (project_.pad(globalIndex).uuid != target.targetUuid)
        return juce::Result::fail("Import completion targets a different pad");

    const auto before = project_.pad(globalIndex);
    auto candidate = project_.state();
    auto& replacement = candidate.banks[globalIndex / padsPerBank].pads[globalIndex % padsPerBank];
    replacement.layers[layerIndex].assetUuid = asset.uuid;
    replacement.layers[layerIndex].enabled = true;

    const auto existingAsset =
        std::find_if(candidate.assets.begin(), candidate.assets.end(),
                     [&](const auto& entry) { return entry.uuid == asset.uuid; });
    if (existingAsset != candidate.assets.end())
        *existingAsset = std::move(asset);
    else
        candidate.assets.push_back(std::move(asset));

    if (const auto validation = validateProjectState(candidate); validation.failed())
        return validation;
    const auto after = replacement;
    if (const auto result = project_.restoreState(std::move(candidate), project_.revision() + 1U);
        result.failed())
        return result;
    undoHistory_.push_back(PadEdit{globalIndex, before, after, "Import sample"});
    redoHistory_.clear();
    return juce::Result::ok();
}

bool ApplicationController::canUndo() const noexcept {
    return !undoHistory_.empty();
}

bool ApplicationController::canRedo() const noexcept {
    return !redoHistory_.empty();
}

bool ApplicationController::undo() {
    if (undoHistory_.empty())
        return false;

    auto edit = std::move(undoHistory_.back());
    undoHistory_.pop_back();
    if (project_.replacePad(edit.globalIndex, edit.before).failed()) {
        undoHistory_.push_back(std::move(edit));
        return false;
    }
    redoHistory_.push_back(std::move(edit));
    return true;
}

bool ApplicationController::redo() {
    if (redoHistory_.empty())
        return false;

    auto edit = std::move(redoHistory_.back());
    redoHistory_.pop_back();
    if (project_.replacePad(edit.globalIndex, edit.after).failed()) {
        redoHistory_.push_back(std::move(edit));
        return false;
    }
    undoHistory_.push_back(std::move(edit));
    return true;
}

juce::Result ApplicationController::commitPadEdit(const std::size_t globalIndex, Pad replacement,
                                                  juce::String description) {
    if (globalIndex >= totalPadCount)
        return juce::Result::fail("Pad index is outside 0..63");
    const auto before = project_.pad(globalIndex);
    if (before == replacement)
        return juce::Result::ok();
    if (const auto result = project_.replacePad(globalIndex, replacement); result.failed())
        return result;

    undoHistory_.push_back(
        PadEdit{globalIndex, before, std::move(replacement), std::move(description)});
    redoHistory_.clear();
    return juce::Result::ok();
}
} // namespace padflow
