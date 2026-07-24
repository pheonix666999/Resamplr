#include "ApplicationController.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace padflow {
ApplicationController::ApplicationController() : project_(Project::createEmpty()) {}

void ApplicationController::createEmptyProject(juce::String name, juce::String fixedUuid) {
    project_ = Project::createEmpty(std::move(name), std::move(fixedUuid));
    clipboard_.reset();
    undoHistory_.clear();
    redoHistory_.clear();
}

juce::Result ApplicationController::restoreProject(Project project) {
    if (const auto validation = validateProjectState(project.state()); validation.failed())
        return validation;
    project_ = std::move(project);
    clipboard_.reset();
    undoHistory_.clear();
    redoHistory_.clear();
    return juce::Result::ok();
}

std::size_t ApplicationController::refreshExternalAssetAvailability() {
    auto candidate = project_.state();
    std::size_t changed = 0U;
    for (auto& asset : candidate.assets) {
        const auto missing =
            asset.originalPath.isEmpty() || !juce::File{asset.originalPath}.existsAsFile();
        if (asset.missing != missing) {
            asset.missing = missing;
            ++changed;
        }
    }
    if (changed != 0U)
        juce::ignoreUnused(project_.restoreState(std::move(candidate), project_.revision() + 1U));
    return changed;
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
    if (!layer.playback.initialized && layer.assetUuid.isNotEmpty()) {
        const auto asset =
            std::find_if(project_.state().assets.begin(), project_.state().assets.end(),
                         [&](const auto& entry) { return entry.uuid == layer.assetUuid; });
        if (asset != project_.state().assets.end())
            layer.playback = resolveSamplePlaybackSettings(layer, asset->frameCount);
    }
    auto replacement = project_.pad(globalIndex);
    replacement.layers[layerIndex] = std::move(layer);
    return commitPadEdit(globalIndex, std::move(replacement), "Change sample layer");
}

juce::Result ApplicationController::setLayerTrim(const std::size_t globalIndex,
                                                 const std::size_t layerIndex,
                                                 const std::uint64_t startFrame,
                                                 const std::uint64_t endFrame) {
    const auto* asset = assetForLayer(globalIndex, layerIndex);
    if (asset == nullptr)
        return juce::Result::fail("Sample trim requires an assigned asset");
    if (startFrame >= endFrame || endFrame > asset->frameCount)
        return juce::Result::fail("Sample trim must be a non-empty range inside the source");
    auto playback = resolveSamplePlaybackSettings(project_.pad(globalIndex).layers[layerIndex],
                                                  asset->frameCount);
    playback.startFrame = startFrame;
    playback.endFrame = endFrame;
    const auto clampedLoopStart = std::max(playback.loopStartFrame, startFrame);
    const auto clampedLoopEnd = std::min(playback.loopEndFrame, endFrame);
    if (clampedLoopStart < clampedLoopEnd) {
        playback.loopStartFrame = clampedLoopStart;
        playback.loopEndFrame = clampedLoopEnd;
    } else {
        playback.loopStartFrame = startFrame;
        playback.loopEndFrame = endFrame;
        playback.loopEnabled = false;
    }
    return commitLayerPlayback(globalIndex, layerIndex, playback, "Change sample trim");
}

juce::Result ApplicationController::setLayerLoop(const std::size_t globalIndex,
                                                 const std::size_t layerIndex,
                                                 const std::uint64_t startFrame,
                                                 const std::uint64_t endFrame) {
    const auto* asset = assetForLayer(globalIndex, layerIndex);
    if (asset == nullptr)
        return juce::Result::fail("Sample loop requires an assigned asset");
    auto playback = resolveSamplePlaybackSettings(project_.pad(globalIndex).layers[layerIndex],
                                                  asset->frameCount);
    if (startFrame < playback.startFrame || startFrame >= endFrame || endFrame > playback.endFrame)
        return juce::Result::fail("Sample loop must be a non-empty range inside the trim");
    playback.loopStartFrame = startFrame;
    playback.loopEndFrame = endFrame;
    return commitLayerPlayback(globalIndex, layerIndex, playback, "Change sample loop");
}

juce::Result ApplicationController::setLayerLoopEnabled(const std::size_t globalIndex,
                                                        const std::size_t layerIndex,
                                                        const bool enabled) {
    const auto* asset = assetForLayer(globalIndex, layerIndex);
    if (asset == nullptr)
        return juce::Result::fail("Sample loop requires an assigned asset");
    auto playback = resolveSamplePlaybackSettings(project_.pad(globalIndex).layers[layerIndex],
                                                  asset->frameCount);
    playback.loopEnabled = enabled;
    return commitLayerPlayback(globalIndex, layerIndex, playback,
                               enabled ? "Enable sample loop" : "Disable sample loop");
}

juce::Result ApplicationController::setLayerReverseEnabled(const std::size_t globalIndex,
                                                           const std::size_t layerIndex,
                                                           const bool enabled) {
    const auto* asset = assetForLayer(globalIndex, layerIndex);
    if (asset == nullptr)
        return juce::Result::fail("Sample reverse requires an assigned asset");
    auto playback = resolveSamplePlaybackSettings(project_.pad(globalIndex).layers[layerIndex],
                                                  asset->frameCount);
    playback.reverseEnabled = enabled;
    return commitLayerPlayback(globalIndex, layerIndex, playback,
                               enabled ? "Enable sample reverse" : "Disable sample reverse");
}

juce::Result ApplicationController::setLayerZeroCrossingSnap(const std::size_t globalIndex,
                                                             const std::size_t layerIndex,
                                                             const bool enabled) {
    const auto* asset = assetForLayer(globalIndex, layerIndex);
    if (asset == nullptr)
        return juce::Result::fail("Zero-crossing snap requires an assigned asset");
    auto playback = resolveSamplePlaybackSettings(project_.pad(globalIndex).layers[layerIndex],
                                                  asset->frameCount);
    playback.zeroCrossingSnap = enabled;
    return commitLayerPlayback(globalIndex, layerIndex, playback,
                               enabled ? "Enable zero-crossing snap"
                                       : "Disable zero-crossing snap");
}

juce::Result ApplicationController::resetLayerTrim(const std::size_t globalIndex,
                                                   const std::size_t layerIndex) {
    const auto* asset = assetForLayer(globalIndex, layerIndex);
    if (asset == nullptr)
        return juce::Result::fail("Sample trim requires an assigned asset");
    auto playback = resolveSamplePlaybackSettings(project_.pad(globalIndex).layers[layerIndex],
                                                  asset->frameCount);
    playback.startFrame = 0U;
    playback.endFrame = asset->frameCount;
    return commitLayerPlayback(globalIndex, layerIndex, playback, "Reset sample trim");
}

juce::Result ApplicationController::resetLayerLoop(const std::size_t globalIndex,
                                                   const std::size_t layerIndex) {
    const auto* asset = assetForLayer(globalIndex, layerIndex);
    if (asset == nullptr)
        return juce::Result::fail("Sample loop requires an assigned asset");
    auto playback = resolveSamplePlaybackSettings(project_.pad(globalIndex).layers[layerIndex],
                                                  asset->frameCount);
    playback.loopStartFrame = playback.startFrame;
    playback.loopEndFrame = playback.endFrame;
    playback.loopEnabled = false;
    return commitLayerPlayback(globalIndex, layerIndex, playback, "Reset sample loop");
}

juce::Result ApplicationController::setPadMappings(const std::size_t globalIndex,
                                                   const std::uint8_t midiNote,
                                                   juce::String keyboardKey) {
    if (globalIndex >= totalPadCount)
        return juce::Result::fail("Pad index is outside 0..63");
    auto replacement = project_.pad(globalIndex);
    replacement.midiNote = midiNote;
    replacement.keyboardKey = keyboardKey.trim().toUpperCase();
    return commitPadEdit(globalIndex, std::move(replacement), "Change pad mappings");
}

juce::Result ApplicationController::setAudioSettings(AudioSettings settings) {
    auto candidate = project_.state();
    candidate.audio = std::move(settings);
    return commitProjectEdit(std::move(candidate), "Change audio settings");
}

juce::Result ApplicationController::setMidiSettings(MidiSettings settings) {
    auto candidate = project_.state();
    candidate.midi = std::move(settings);
    return commitProjectEdit(std::move(candidate), "Change MIDI settings");
}

juce::Result ApplicationController::setRecordingPreferences(RecordingPreferences preferences) {
    auto candidate = project_.state();
    candidate.recording = preferences;
    return commitProjectEdit(std::move(candidate), "Change recording preferences");
}

juce::Result ApplicationController::setUiState(ProjectUiState state) {
    auto candidate = project_.state();
    candidate.ui = state;
    return project_.restoreState(std::move(candidate), project_.revision() + 1U);
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

    const auto before = project_.state();
    auto candidate = project_.state();
    auto& replacement = candidate.banks[globalIndex / padsPerBank].pads[globalIndex % padsPerBank];
    replacement.layers[layerIndex].assetUuid = asset.uuid;
    replacement.layers[layerIndex].enabled = true;
    replacement.layers[layerIndex].playback =
        resolveSamplePlaybackSettings(replacement.layers[layerIndex], asset.frameCount);

    const auto existingAsset =
        std::find_if(candidate.assets.begin(), candidate.assets.end(),
                     [&](const auto& entry) { return entry.uuid == asset.uuid; });
    if (existingAsset != candidate.assets.end())
        *existingAsset = std::move(asset);
    else
        candidate.assets.push_back(std::move(asset));

    if (const auto validation = validateProjectState(candidate); validation.failed())
        return validation;
    if (const auto result = project_.restoreState(std::move(candidate), project_.revision() + 1U);
        result.failed())
        return result;
    undoHistory_.push_back(ProjectEdit{before, project_.state(), "Import sample"});
    redoHistory_.clear();
    return juce::Result::ok();
}

juce::Result ApplicationController::commitDerivedLayer(
    const JobSpec& target, const std::size_t globalIndex, const std::size_t layerIndex,
    const juce::String& expectedSourceAssetUuid, ExternalAssetReference derivedAsset,
    DerivedAssetRecord provenance, SamplePlaybackSettings playback) {
    if (!isCurrentJobTarget(target))
        return juce::Result::fail("Derived asset completion is stale");
    if (globalIndex >= totalPadCount || layerIndex >= minimumLayersPerPad)
        return juce::Result::fail("Derived asset completion targets an invalid layer");
    if (project_.pad(globalIndex).uuid != target.targetUuid)
        return juce::Result::fail("Derived asset completion targets a different pad");
    if (project_.pad(globalIndex).layers[layerIndex].assetUuid != expectedSourceAssetUuid)
        return juce::Result::fail("Derived asset source assignment changed");
    if (derivedAsset.uuid != provenance.derivedAssetUuid ||
        derivedAsset.contentFingerprint != provenance.outputFingerprint)
        return juce::Result::fail("Derived asset provenance does not match its output");
    if (const auto validation = validateSamplePlaybackSettings(playback, derivedAsset.frameCount);
        validation.failed())
        return validation;

    const auto before = project_.state();
    auto candidate = project_.state();
    auto& replacement = candidate.banks[globalIndex / padsPerBank].pads[globalIndex % padsPerBank];
    replacement.layers[layerIndex].assetUuid = derivedAsset.uuid;
    replacement.layers[layerIndex].enabled = true;
    replacement.layers[layerIndex].playback = playback;

    const auto existingAsset =
        std::find_if(candidate.assets.begin(), candidate.assets.end(),
                     [&](const auto& entry) { return entry.uuid == derivedAsset.uuid; });
    if (existingAsset != candidate.assets.end())
        *existingAsset = std::move(derivedAsset);
    else
        candidate.assets.push_back(std::move(derivedAsset));
    const auto existingProvenance = std::find_if(
        candidate.derivedAssets.begin(), candidate.derivedAssets.end(),
        [&](const auto& entry) { return entry.derivedAssetUuid == provenance.derivedAssetUuid; });
    if (existingProvenance != candidate.derivedAssets.end())
        *existingProvenance = std::move(provenance);
    else
        candidate.derivedAssets.push_back(std::move(provenance));

    if (const auto validation = validateProjectState(candidate); validation.failed())
        return validation;
    if (const auto result = project_.restoreState(std::move(candidate), project_.revision() + 1U);
        result.failed())
        return result;
    undoHistory_.push_back(
        ProjectEdit{before, project_.state(), "Replace layer with derived asset"});
    redoHistory_.clear();
    return juce::Result::ok();
}

juce::Result ApplicationController::commitRecordedLayer(const JobSpec& target,
                                                        const std::size_t globalIndex,
                                                        const std::size_t layerIndex,
                                                        const juce::String& expectedLayerUuid,
                                                        ExternalAssetReference recordedAsset,
                                                        RecordedAssetRecord provenance) {
    if (!isCurrentJobTarget(target))
        return juce::Result::fail("Recorded asset completion is stale");
    if (globalIndex >= totalPadCount || layerIndex >= minimumLayersPerPad)
        return juce::Result::fail("Recorded asset completion targets an invalid layer");
    const auto& currentPad = project_.pad(globalIndex);
    if (currentPad.uuid != target.targetUuid)
        return juce::Result::fail("Recorded asset completion targets a different pad");
    if (currentPad.layers[layerIndex].uuid != expectedLayerUuid ||
        provenance.targetLayerUuid != expectedLayerUuid ||
        provenance.targetProjectUuid != target.ownerUuid ||
        provenance.targetPadUuid != currentPad.uuid ||
        provenance.targetProjectRevision != target.targetRevision)
        return juce::Result::fail("Recorded asset destination changed");
    if (recordedAsset.uuid != provenance.recordedAssetUuid ||
        recordedAsset.contentFingerprint != provenance.contentFingerprint ||
        recordedAsset.channels != provenance.channels ||
        recordedAsset.sourceSampleRate != provenance.sampleRate ||
        recordedAsset.frameCount != provenance.frameCount)
        return juce::Result::fail("Recorded asset provenance does not match its output");

    const auto before = project_.state();
    auto candidate = project_.state();
    auto& replacement = candidate.banks[globalIndex / padsPerBank].pads[globalIndex % padsPerBank];
    replacement.layers[layerIndex].assetUuid = recordedAsset.uuid;
    replacement.layers[layerIndex].enabled = true;
    replacement.layers[layerIndex].playback =
        resolveSamplePlaybackSettings(replacement.layers[layerIndex], recordedAsset.frameCount);

    const auto existingAsset =
        std::find_if(candidate.assets.begin(), candidate.assets.end(),
                     [&](const auto& entry) { return entry.uuid == recordedAsset.uuid; });
    if (existingAsset != candidate.assets.end())
        *existingAsset = std::move(recordedAsset);
    else
        candidate.assets.push_back(std::move(recordedAsset));
    const auto existingProvenance = std::find_if(
        candidate.recordedAssets.begin(), candidate.recordedAssets.end(),
        [&](const auto& entry) { return entry.recordedAssetUuid == provenance.recordedAssetUuid; });
    if (existingProvenance != candidate.recordedAssets.end())
        *existingProvenance = std::move(provenance);
    else
        candidate.recordedAssets.push_back(std::move(provenance));

    if (const auto validation = validateProjectState(candidate); validation.failed())
        return validation;
    if (const auto result = project_.restoreState(std::move(candidate), project_.revision() + 1U);
        result.failed())
        return result;
    undoHistory_.push_back(ProjectEdit{before, project_.state(), "Assign recorded sample"});
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
    if (project_.restoreState(edit.before, project_.revision() + 1U).failed()) {
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
    if (project_.restoreState(edit.after, project_.revision() + 1U).failed()) {
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
    const auto before = project_.state();
    if (project_.pad(globalIndex) == replacement)
        return juce::Result::ok();
    if (const auto result = project_.replacePad(globalIndex, replacement); result.failed())
        return result;

    undoHistory_.push_back(ProjectEdit{before, project_.state(), std::move(description)});
    redoHistory_.clear();
    return juce::Result::ok();
}

juce::Result ApplicationController::commitProjectEdit(ProjectState replacement,
                                                      juce::String description) {
    if (project_.state() == replacement)
        return juce::Result::ok();
    if (const auto validation = validateProjectState(replacement); validation.failed())
        return validation;
    const auto before = project_.state();
    if (const auto result = project_.restoreState(std::move(replacement), project_.revision() + 1U);
        result.failed())
        return result;
    undoHistory_.push_back(ProjectEdit{before, project_.state(), std::move(description)});
    redoHistory_.clear();
    return juce::Result::ok();
}

const ExternalAssetReference*
ApplicationController::assetForLayer(const std::size_t globalIndex,
                                     const std::size_t layerIndex) const noexcept {
    if (globalIndex >= totalPadCount || layerIndex >= minimumLayersPerPad)
        return nullptr;
    const auto& layer = project_.pad(globalIndex).layers[layerIndex];
    const auto asset =
        std::find_if(project_.state().assets.begin(), project_.state().assets.end(),
                     [&](const auto& entry) { return entry.uuid == layer.assetUuid; });
    return asset == project_.state().assets.end() ? nullptr : std::addressof(*asset);
}

juce::Result ApplicationController::commitLayerPlayback(const std::size_t globalIndex,
                                                        const std::size_t layerIndex,
                                                        SamplePlaybackSettings playback,
                                                        juce::String description) {
    const auto* asset = assetForLayer(globalIndex, layerIndex);
    if (asset == nullptr)
        return juce::Result::fail("Sample playback edit requires an assigned asset");
    playback.initialized = true;
    if (const auto validation = validateSamplePlaybackSettings(playback, asset->frameCount);
        validation.failed())
        return validation;
    auto replacement = project_.pad(globalIndex);
    replacement.layers[layerIndex].playback = playback;
    return commitPadEdit(globalIndex, std::move(replacement), std::move(description));
}
} // namespace padflow
