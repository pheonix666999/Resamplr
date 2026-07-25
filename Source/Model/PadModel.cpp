#include "PadModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace padflow {
namespace {
constexpr std::array<const char*, padsPerBank> defaultKeyboardKeys{
    "1", "2", "3", "4", "Q", "W", "E", "R", "A", "S", "D", "F", "Z", "X", "C", "V"};

juce::String formatUuidHex(const juce::String& hex) {
    return hex.substring(0, 8) + "-" + hex.substring(8, 12) + "-" + hex.substring(12, 16) + "-" +
           hex.substring(16, 20) + "-" + hex.substring(20, 32);
}

std::uint64_t fnv1a64(const std::string_view value, std::uint64_t hash) noexcept {
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (const auto byte : value) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(byte));
        hash *= prime;
    }
    return hash;
}

void writeHashBytes(std::array<std::uint8_t, 16U>& bytes, const std::size_t offset,
                    const std::uint64_t hash) noexcept {
    for (std::size_t index = 0; index < 8U; ++index) {
        const auto shift = static_cast<unsigned int>((7U - index) * 8U);
        bytes[offset + index] = static_cast<std::uint8_t>((hash >> shift) & 0xffU);
    }
}

bool isFiniteInRange(const float value, const float minimum, const float maximum) noexcept {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool playbackBoundsValid(const SamplePlaybackSettings& settings,
                         const std::uint64_t sourceFrameCount) noexcept {
    return settings.initialized && sourceFrameCount > 0U &&
           settings.startFrame < settings.endFrame && settings.endFrame <= sourceFrameCount &&
           settings.loopStartFrame >= settings.startFrame &&
           settings.loopStartFrame < settings.loopEndFrame &&
           settings.loopEndFrame <= settings.endFrame;
}

juce::Result validateUuid(const juce::String& uuid, const juce::String& label) {
    if (uuid.trim().isEmpty())
        return juce::Result::fail(label + " UUID is empty");
    return juce::Result::ok();
}

void addUuid(std::unordered_set<std::string>& uuids, const juce::String& uuid,
             juce::Result& result) {
    if (result.failed())
        return;

    const auto utf8 = uuid.toStdString();
    if (!uuids.insert(utf8).second)
        result = juce::Result::fail("Project contains duplicate UUID " + uuid);
}
} // namespace

juce::String makeStableUuid(const juce::String& seed) {
    const auto utf8 = seed.toStdString();
    std::array<std::uint8_t, 16U> bytes{};
    writeHashBytes(bytes, 0U, fnv1a64(utf8, 14695981039346656037ULL));
    writeHashBytes(bytes, 8U, fnv1a64(utf8, 7809847782465536322ULL));
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x50U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
    return formatUuidHex(juce::Uuid{bytes.data()}.toString());
}

ProjectState makeDefaultProjectState(const juce::String& projectUuid, juce::String projectName) {
    ProjectState state;
    state.projectUuid = projectUuid;
    state.projectName = std::move(projectName);

    for (std::size_t bankIndex = 0; bankIndex < padBankCount; ++bankIndex) {
        auto& bank = state.banks[bankIndex];
        bank.name = juce::String::charToString(
            static_cast<juce::juce_wchar>('A' + static_cast<int>(bankIndex)));
        bank.uuid = makeStableUuid(projectUuid + "/bank/" + bank.name);

        for (std::size_t padIndex = 0; padIndex < padsPerBank; ++padIndex) {
            const auto globalIndex = toGlobalPadIndex(bankIndex, padIndex);
            auto& pad = bank.pads[padIndex];
            pad.uuid =
                makeStableUuid(bank.uuid + "/pad/" + juce::String{static_cast<int>(padIndex)});
            pad.name = "Pad " + juce::String{static_cast<int>(padIndex + 1U)};
            pad.keyboardKey = defaultKeyboardKeys[padIndex];
            pad.midiNote = static_cast<std::uint8_t>(36U + globalIndex);

            for (std::size_t layerIndex = 0; layerIndex < minimumLayersPerPad; ++layerIndex)
                pad.layers[layerIndex].uuid = makeStableUuid(
                    pad.uuid + "/layer/" + juce::String{static_cast<int>(layerIndex)});
        }
    }

    return state;
}

Pad makeClearedPad(const ProjectState& project, const std::size_t globalPadIndex) {
    const auto bankIndex = globalPadIndex / padsPerBank;
    const auto padIndex = globalPadIndex % padsPerBank;
    auto cleared = makeDefaultProjectState(project.projectUuid, project.projectName)
                       .banks[bankIndex]
                       .pads[padIndex];
    const auto& existing = project.banks[bankIndex].pads[padIndex];
    cleared.uuid = existing.uuid;
    cleared.keyboardKey = existing.keyboardKey;
    cleared.midiNote = existing.midiNote;
    for (std::size_t index = 0; index < minimumLayersPerPad; ++index)
        cleared.layers[index].uuid = existing.layers[index].uuid;
    return cleared;
}

void regeneratePadIdentity(Pad& pad) {
    pad.uuid = juce::Uuid{}.toString();
    for (auto& layer : pad.layers)
        layer.uuid = juce::Uuid{}.toString();
}

juce::Result validateLayer(const SampleLayer& layer) {
    if (const auto uuidResult = validateUuid(layer.uuid, "Layer"); uuidResult.failed())
        return uuidResult;
    if (layer.velocityMinimum < 1U || layer.velocityMaximum > 127U ||
        layer.velocityMinimum > layer.velocityMaximum)
        return juce::Result::fail("Layer velocity range must be inclusive within 1..127");
    if (!isFiniteInRange(layer.gainDecibels, -60.0F, 24.0F))
        return juce::Result::fail("Layer gain must be within -60..24 dB");
    if (!isFiniteInRange(layer.pan, -1.0F, 1.0F))
        return juce::Result::fail("Layer pan must be within -1..1");
    if (!isFiniteInRange(layer.tuningCents, -1200.0F, 1200.0F))
        return juce::Result::fail("Layer tuning must be within -1200..1200 cents");
    if (layer.enabled && layer.assetUuid.trim().isEmpty())
        return juce::Result::fail("An enabled layer must reference an asset");
    if (!layer.playback.initialized &&
        (layer.playback.startFrame != 0U || layer.playback.endFrame != 0U ||
         layer.playback.loopStartFrame != 0U || layer.playback.loopEndFrame != 0U ||
         layer.playback.loopEnabled || layer.playback.reverseEnabled ||
         layer.playback.zeroCrossingSnap))
        return juce::Result::fail("Uninitialized layer playback state must contain defaults");
    if (layer.playback.initialized && layer.assetUuid.trim().isEmpty())
        return juce::Result::fail("Initialized layer playback state requires an asset");
    return juce::Result::ok();
}

juce::Result validateSamplePlaybackSettings(const SamplePlaybackSettings& settings,
                                            const std::uint64_t sourceFrameCount) {
    if (!settings.initialized)
        return juce::Result::fail("Sample playback bounds are not initialized");
    if (sourceFrameCount == 0U || settings.startFrame >= settings.endFrame ||
        settings.endFrame > sourceFrameCount)
        return juce::Result::fail("Sample trim must be a non-empty range inside the source");
    if (!playbackBoundsValid(settings, sourceFrameCount))
        return juce::Result::fail("Sample loop must be a non-empty range inside the trim");
    return juce::Result::ok();
}

SamplePlaybackSettings
resolveSamplePlaybackSettings(const SampleLayer& layer,
                              const std::uint64_t sourceFrameCount) noexcept {
    if (playbackBoundsValid(layer.playback, sourceFrameCount))
        return layer.playback;
    SamplePlaybackSettings resolved;
    if (sourceFrameCount > 0U) {
        resolved.endFrame = sourceFrameCount;
        resolved.loopEndFrame = sourceFrameCount;
        resolved.initialized = true;
    }
    return resolved;
}

juce::Result validatePad(const Pad& pad) {
    if (const auto uuidResult = validateUuid(pad.uuid, "Pad"); uuidResult.failed())
        return uuidResult;
    const auto trimmedName = pad.name.trim();
    if (trimmedName.isEmpty() || trimmedName.length() > 64)
        return juce::Result::fail("Pad name must contain 1..64 characters");
    if ((pad.colourArgb >> 24U) == 0U)
        return juce::Result::fail(
            "Pad colour must be opaque or translucent, not fully transparent");

    const auto& parameters = pad.parameters;
    if (!isFiniteInRange(parameters.gainDecibels, -60.0F, 24.0F) ||
        !isFiniteInRange(parameters.pan, -1.0F, 1.0F) || parameters.coarseSemitones < -48 ||
        parameters.coarseSemitones > 48 ||
        !isFiniteInRange(parameters.fineCents, -100.0F, 100.0F) || parameters.chokeGroup > 16U ||
        parameters.maximumVoices < 1U || parameters.maximumVoices > 128U)
        return juce::Result::fail("Pad parameter is outside its supported range");

    const auto& envelope = parameters.envelope;
    if (!isFiniteInRange(envelope.attackSeconds, 0.0F, 60.0F) ||
        !isFiniteInRange(envelope.decaySeconds, 0.0F, 60.0F) ||
        !isFiniteInRange(envelope.sustainLevel, 0.0F, 1.0F) ||
        !isFiniteInRange(envelope.releaseSeconds, 0.0F, 60.0F))
        return juce::Result::fail("Pad envelope is outside its supported range");

    if (pad.midiNote > 127U)
        return juce::Result::fail("Pad MIDI note must be within 0..127");
    if (pad.keyboardKey.length() > 16)
        return juce::Result::fail("Pad keyboard assignment is too long");
    for (const auto& layer : pad.layers)
        if (const auto result = validateLayer(layer); result.failed())
            return result;
    return juce::Result::ok();
}

juce::Result validateProjectState(const ProjectState& state) {
    if (const auto uuidResult = validateUuid(state.projectUuid, "Project"); uuidResult.failed())
        return uuidResult;
    if (state.projectName.trim().isEmpty() || state.projectName.trim().length() > 128)
        return juce::Result::fail("Project name must contain 1..128 characters");
    if (state.midi.channelFilter > 16U)
        return juce::Result::fail("MIDI channel filter must be omni or 1..16");
    if (state.ui.selectedBank >= padBankCount || state.ui.selectedPad >= padsPerBank ||
        state.ui.fixedTriggerVelocity < 1U || state.ui.fixedTriggerVelocity > 127U ||
        !isFiniteInRange(state.ui.previewVolume, 0.0F, 1.0F) || state.ui.windowWidth < 640 ||
        state.ui.windowHeight < 480)
        return juce::Result::fail("Project UI state is outside its supported range");

    std::unordered_set<std::string> uuids;
    juce::Result result = juce::Result::ok();
    addUuid(uuids, state.projectUuid, result);
    for (std::size_t bankIndex = 0; bankIndex < padBankCount && result.wasOk(); ++bankIndex) {
        const auto& bank = state.banks[bankIndex];
        if (bank.name != juce::String::charToString(
                             static_cast<juce::juce_wchar>('A' + static_cast<int>(bankIndex))))
            return juce::Result::fail("Pad bank names must be A..D in stable order");
        addUuid(uuids, bank.uuid, result);
        for (const auto& pad : bank.pads) {
            if (const auto padResult = validatePad(pad); padResult.failed())
                return padResult;
            addUuid(uuids, pad.uuid, result);
            for (const auto& layer : pad.layers)
                addUuid(uuids, layer.uuid, result);
        }
    }

    for (const auto& asset : state.assets) {
        if (asset.uuid.trim().isEmpty() || asset.originalName.trim().isEmpty() ||
            asset.channels > 2U || !std::isfinite(asset.sourceSampleRate) ||
            asset.sourceSampleRate < 0.0)
            return juce::Result::fail("External asset metadata is invalid");
        addUuid(uuids, asset.uuid, result);
    }
    for (const auto& derived : state.derivedAssets) {
        if (derived.derivedAssetUuid.trim().isEmpty() || derived.parentAssetUuid.trim().isEmpty() ||
            derived.sourceFingerprint.trim().isEmpty() ||
            derived.operationIdentifier.trim().isEmpty() || derived.algorithmVersion == 0U ||
            derived.canonicalOperationParameters.trim().isEmpty() ||
            derived.outputFingerprint.trim().isEmpty() ||
            derived.creationMetadata.trim().isEmpty() ||
            derived.projectOwnedRelativePath.trim().isEmpty())
            return juce::Result::fail("Derived asset provenance is incomplete");
        const auto output =
            std::find_if(state.assets.begin(), state.assets.end(), [&](const auto& asset) {
                return asset.uuid == derived.derivedAssetUuid &&
                       asset.contentFingerprint == derived.outputFingerprint;
            });
        const auto parent =
            std::find_if(state.assets.begin(), state.assets.end(), [&](const auto& asset) {
                return asset.uuid == derived.parentAssetUuid &&
                       asset.contentFingerprint == derived.sourceFingerprint;
            });
        if (output == state.assets.end() || parent == state.assets.end())
            return juce::Result::fail("Derived asset provenance references unknown source data");
    }
    for (const auto& recorded : state.recordedAssets) {
        if (recorded.recordedAssetUuid.trim().isEmpty() || recorded.sessionUuid.trim().isEmpty() ||
            recorded.contentFingerprint.trim().isEmpty() ||
            recorded.projectOwnedRelativePath.trim().isEmpty() ||
            recorded.targetProjectUuid.trim().isEmpty() ||
            recorded.targetPadUuid.trim().isEmpty() || recorded.targetLayerUuid.trim().isEmpty() ||
            (recorded.channels != 1U && recorded.channels != 2U) ||
            !std::isfinite(recorded.sampleRate) || recorded.sampleRate <= 0.0 ||
            recorded.frameCount == 0U)
            return juce::Result::fail("Recorded asset provenance is incomplete");
        const auto output =
            std::find_if(state.assets.begin(), state.assets.end(), [&](const auto& asset) {
                return asset.uuid == recorded.recordedAssetUuid &&
                       asset.contentFingerprint == recorded.contentFingerprint &&
                       asset.channels == recorded.channels &&
                       asset.sourceSampleRate == recorded.sampleRate &&
                       asset.frameCount == recorded.frameCount;
            });
        if (output == state.assets.end())
            return juce::Result::fail("Recorded asset provenance references unknown source data");
        if (recorded.targetProjectUuid != state.projectUuid)
            return juce::Result::fail("Recorded asset provenance targets a different project");
        const auto targetPad =
            std::find_if(state.banks.begin(), state.banks.end(), [&](const auto& bank) {
                return std::any_of(bank.pads.begin(), bank.pads.end(), [&](const auto& pad) {
                    return pad.uuid == recorded.targetPadUuid &&
                           std::any_of(pad.layers.begin(), pad.layers.end(),
                                       [&](const auto& layer) {
                                           return layer.uuid == recorded.targetLayerUuid;
                                       });
                });
            });
        if (targetPad == state.banks.end())
            return juce::Result::fail("Recorded asset provenance targets an unknown layer");
    }
    if ((state.recording.channels != 1U && state.recording.channels != 2U) ||
        !isFiniteInRange(state.recording.thresholdDecibels, -96.0F, 0.0F) ||
        state.recording.preRollMilliseconds > 2000U)
        return juce::Result::fail("Recording preferences are outside their supported range");
    for (const auto& bank : state.banks)
        for (const auto& pad : bank.pads)
            for (const auto& layer : pad.layers) {
                if (!layer.playback.initialized)
                    continue;
                const auto asset =
                    std::find_if(state.assets.begin(), state.assets.end(),
                                 [&](const auto& entry) { return entry.uuid == layer.assetUuid; });
                if (asset == state.assets.end())
                    return juce::Result::fail(
                        "Initialized layer playback state references an unknown asset");
                if (const auto playbackResult =
                        validateSamplePlaybackSettings(layer.playback, asset->frameCount);
                    playbackResult.failed())
                    return playbackResult;
            }
    return result;
}

std::size_t toGlobalPadIndex(const std::size_t bankIndex, const std::size_t padIndex) noexcept {
    return bankIndex * padsPerBank + padIndex;
}
} // namespace padflow
