#include "ProjectSerializer.h"

#include "App/ProductInfo.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace padflow {
namespace {
juce::var makeObject() {
    auto object = std::make_unique<juce::DynamicObject>();
    return juce::var{object.release()};
}

void setProperty(juce::var& object, const char* name, const juce::var& value) {
    object.getDynamicObject()->setProperty(juce::Identifier{name}, value);
}

juce::var makeArray(const std::vector<juce::var>& values) {
    juce::Array<juce::var> array;
    array.ensureStorageAllocated(static_cast<int>(values.size()));
    for (const auto& value : values)
        array.add(value);
    return juce::var{array};
}

template <typename Integer> juce::String decimalString(const Integer value) {
    static_assert(std::is_integral_v<Integer>);
    return juce::String{value};
}

const char* playbackModeName(const PlaybackMode mode) noexcept {
    switch (mode) {
    case PlaybackMode::oneShot:
        return "one-shot";
    case PlaybackMode::gate:
        return "gate";
    case PlaybackMode::toggle:
        return "toggle";
    }
    return "one-shot";
}

const char* polyphonyModeName(const PolyphonyMode mode) noexcept {
    switch (mode) {
    case PolyphonyMode::poly:
        return "poly";
    case PolyphonyMode::mono:
        return "mono";
    }
    return "poly";
}

const char* sliceAlgorithmName(const SliceAlgorithm algorithm) noexcept {
    switch (algorithm) {
    case SliceAlgorithm::equal:
        return "equal";
    case SliceAlgorithm::fixedLength:
        return "fixed-length";
    case SliceAlgorithm::transient:
        return "transient";
    case SliceAlgorithm::manual:
        return "manual";
    case SliceAlgorithm::lazy:
        return "lazy";
    }
    return "manual";
}

const char* sliceDisplayUnitName(const SliceDisplayUnit unit) noexcept {
    return unit == SliceDisplayUnit::milliseconds ? "milliseconds" : "frames";
}

const char* sliceRemainderPolicyName(const SliceRemainderPolicy policy) noexcept {
    return policy == SliceRemainderPolicy::discard ? "discard" : "include";
}

juce::var envelopeValue(const EnvelopeParameters& envelope) {
    auto value = makeObject();
    setProperty(value, "attackSeconds", static_cast<double>(envelope.attackSeconds));
    setProperty(value, "decaySeconds", static_cast<double>(envelope.decaySeconds));
    setProperty(value, "releaseSeconds", static_cast<double>(envelope.releaseSeconds));
    setProperty(value, "sustainLevel", static_cast<double>(envelope.sustainLevel));
    return value;
}

juce::var samplePlaybackValue(const SamplePlaybackSettings& playback) {
    auto value = makeObject();
    setProperty(value, "endFrame", decimalString(playback.endFrame));
    setProperty(value, "loopEnabled", playback.loopEnabled);
    setProperty(value, "loopEndFrame", decimalString(playback.loopEndFrame));
    setProperty(value, "loopStartFrame", decimalString(playback.loopStartFrame));
    setProperty(value, "reverseEnabled", playback.reverseEnabled);
    setProperty(value, "startFrame", decimalString(playback.startFrame));
    setProperty(value, "zeroCrossingSnap", playback.zeroCrossingSnap);
    return value;
}

juce::var layerValue(const SampleLayer& layer) {
    auto value = makeObject();
    setProperty(value, "assetUuid", layer.assetUuid);
    if (layer.playback.initialized)
        setProperty(value, "editing", samplePlaybackValue(layer.playback));
    setProperty(value, "enabled", layer.enabled);
    setProperty(value, "gainDecibels", static_cast<double>(layer.gainDecibels));
    setProperty(value, "pan", static_cast<double>(layer.pan));
    if (layer.sliceUuid.isNotEmpty()) {
        auto assignment = makeObject();
        setProperty(assignment, "assignmentSessionUuid", layer.assignmentSessionUuid);
        setProperty(assignment, "sliceSetUuid", layer.sliceSetUuid);
        setProperty(assignment, "sliceUuid", layer.sliceUuid);
        setProperty(value, "sliceAssignment", assignment);
    }
    setProperty(value, "tuningCents", static_cast<double>(layer.tuningCents));
    setProperty(value, "uuid", layer.uuid);
    setProperty(value, "velocityMaximum", static_cast<int>(layer.velocityMaximum));
    setProperty(value, "velocityMinimum", static_cast<int>(layer.velocityMinimum));
    return value;
}

juce::var sliceRegionValue(const SliceRegion& slice) {
    auto value = makeObject();
    if (slice.colourArgb.has_value())
        setProperty(value, "colourArgb", decimalString(*slice.colourArgb));
    setProperty(value, "endFrame", decimalString(slice.endFrame));
    setProperty(value, "name", slice.name);
    setProperty(value, "startFrame", decimalString(slice.startFrame));
    setProperty(value, "uuid", slice.uuid);
    return value;
}

juce::var sliceParametersValue(const SliceAlgorithmParameters& parameters) {
    auto value = makeObject();
    setProperty(value, "attackLookBackFrames", decimalString(parameters.attackLookBackFrames));
    setProperty(value, "displayUnit", sliceDisplayUnitName(parameters.displayUnit));
    setProperty(value, "fixedLengthFrames", decimalString(parameters.fixedLengthFrames));
    setProperty(value, "minimumSliceFrames", decimalString(parameters.minimumSliceFrames));
    setProperty(value, "quantizeFrames", decimalString(parameters.quantizeFrames));
    setProperty(value, "remainderPolicy", sliceRemainderPolicyName(parameters.remainderPolicy));
    setProperty(value, "sliceCount", decimalString(parameters.sliceCount));
    setProperty(value, "transientSensitivity",
                static_cast<double>(parameters.transientSensitivity));
    setProperty(value, "transientThresholdFloor",
                static_cast<double>(parameters.transientThresholdFloor));
    return value;
}

juce::var sliceSetValue(const SliceSet& sliceSet) {
    auto value = makeObject();
    setProperty(value, "algorithm", sliceAlgorithmName(sliceSet.algorithm));
    setProperty(value, "algorithmVersion", static_cast<int>(sliceSet.algorithmVersion));
    setProperty(value, "parameters", sliceParametersValue(sliceSet.parameters));
    setProperty(value, "sourceAssetUuid", sliceSet.sourceAssetUuid);
    setProperty(value, "sourceFingerprint", sliceSet.sourceFingerprint);
    setProperty(value, "sourceLayerUuid", sliceSet.sourceLayerUuid);
    setProperty(value, "sourceTrimEnd", decimalString(sliceSet.sourceTrimEnd));
    setProperty(value, "sourceTrimStart", decimalString(sliceSet.sourceTrimStart));

    std::vector<juce::var> slices;
    slices.reserve(sliceSet.slices.size());
    for (const auto& slice : sliceSet.slices)
        slices.push_back(sliceRegionValue(slice));
    setProperty(value, "slices", makeArray(slices));
    setProperty(value, "uuid", sliceSet.uuid);
    return value;
}

juce::var padParametersValue(const PadParameters& parameters) {
    auto value = makeObject();
    setProperty(value, "chokeGroup", static_cast<int>(parameters.chokeGroup));
    setProperty(value, "coarseSemitones", static_cast<int>(parameters.coarseSemitones));
    setProperty(value, "envelope", envelopeValue(parameters.envelope));
    setProperty(value, "fineCents", static_cast<double>(parameters.fineCents));
    setProperty(value, "gainDecibels", static_cast<double>(parameters.gainDecibels));
    setProperty(value, "maximumVoices", static_cast<int>(parameters.maximumVoices));
    setProperty(value, "pan", static_cast<double>(parameters.pan));
    setProperty(value, "playbackMode", playbackModeName(parameters.playbackMode));
    setProperty(value, "polyphonyMode", polyphonyModeName(parameters.polyphonyMode));
    return value;
}

juce::var padValue(const Pad& pad) {
    auto value = makeObject();
    setProperty(value, "colourArgb", decimalString(pad.colourArgb));
    setProperty(value, "keyboardKey", pad.keyboardKey);

    std::vector<juce::var> layers;
    layers.reserve(pad.layers.size());
    for (const auto& layer : pad.layers)
        layers.push_back(layerValue(layer));
    setProperty(value, "layers", makeArray(layers));

    setProperty(value, "midiNote", static_cast<int>(pad.midiNote));
    setProperty(value, "name", pad.name);
    setProperty(value, "parameters", padParametersValue(pad.parameters));
    setProperty(value, "uuid", pad.uuid);
    return value;
}

juce::var bankValue(const PadBank& bank) {
    auto value = makeObject();
    setProperty(value, "name", bank.name);

    std::vector<juce::var> pads;
    pads.reserve(bank.pads.size());
    for (const auto& pad : bank.pads)
        pads.push_back(padValue(pad));
    setProperty(value, "pads", makeArray(pads));

    setProperty(value, "uuid", bank.uuid);
    return value;
}

juce::var assetValue(const ExternalAssetReference& asset) {
    auto value = makeObject();
    setProperty(value, "channels", static_cast<int>(asset.channels));
    setProperty(value, "contentFingerprint", asset.contentFingerprint);
    setProperty(value, "decodedBytes", decimalString(asset.decodedBytes));
    setProperty(value, "format", asset.format);
    setProperty(value, "frameCount", decimalString(asset.frameCount));
    setProperty(value, "missing", asset.missing);
    setProperty(value, "modificationTimeMilliseconds",
                decimalString(asset.modificationTimeMilliseconds));
    setProperty(value, "originalName", asset.originalName);
    setProperty(value, "originalPath", asset.originalPath);
    setProperty(value, "sourceFileBytes", decimalString(asset.sourceFileBytes));
    setProperty(value, "sourceSampleRate", asset.sourceSampleRate);
    setProperty(value, "uuid", asset.uuid);
    return value;
}

juce::var derivedAssetValue(const DerivedAssetRecord& asset) {
    auto value = makeObject();
    setProperty(value, "algorithmVersion", static_cast<int>(asset.algorithmVersion));
    setProperty(value, "canonicalOperationParameters", asset.canonicalOperationParameters);
    setProperty(value, "creationMetadata", asset.creationMetadata);
    setProperty(value, "derivedAssetUuid", asset.derivedAssetUuid);
    setProperty(value, "operationIdentifier", asset.operationIdentifier);
    setProperty(value, "outputFingerprint", asset.outputFingerprint);
    setProperty(value, "parentAssetUuid", asset.parentAssetUuid);
    setProperty(value, "projectOwnedRelativePath", asset.projectOwnedRelativePath);
    setProperty(value, "sourceFingerprint", asset.sourceFingerprint);
    return value;
}

juce::var recordedAssetValue(const RecordedAssetRecord& asset) {
    auto value = makeObject();
    setProperty(value, "channels", static_cast<int>(asset.channels));
    setProperty(value, "contentFingerprint", asset.contentFingerprint);
    setProperty(value, "frameCount", decimalString(asset.frameCount));
    setProperty(value, "inputDeviceIdentifier", asset.inputDeviceIdentifier);
    setProperty(value, "projectOwnedRelativePath", asset.projectOwnedRelativePath);
    setProperty(value, "recordedAssetUuid", asset.recordedAssetUuid);
    setProperty(value, "sampleRate", asset.sampleRate);
    setProperty(value, "sessionUuid", asset.sessionUuid);
    setProperty(value, "targetLayerUuid", asset.targetLayerUuid);
    setProperty(value, "targetPadUuid", asset.targetPadUuid);
    setProperty(value, "targetProjectUuid", asset.targetProjectUuid);
    setProperty(value, "targetProjectRevision", decimalString(asset.targetProjectRevision));
    return value;
}

juce::var recordingValue(const RecordingPreferences& recording) {
    auto value = makeObject();
    setProperty(value, "autoAssign", recording.autoAssign);
    setProperty(value, "channels", static_cast<int>(recording.channels));
    setProperty(value, "preRollMilliseconds", static_cast<int>(recording.preRollMilliseconds));
    setProperty(value, "thresholdDecibels", static_cast<double>(recording.thresholdDecibels));
    setProperty(value, "thresholdMode", recording.thresholdMode);
    return value;
}

juce::var audioValue(const AudioSettings& audio) {
    auto value = makeObject();
    setProperty(value, "bufferSize", static_cast<int>(audio.bufferSize));
    setProperty(value, "inputChannelMask", decimalString(audio.inputChannelMask));
    setProperty(value, "inputDeviceIdentifier", audio.inputDeviceIdentifier);
    setProperty(value, "outputChannelMask", decimalString(audio.outputChannelMask));
    setProperty(value, "outputDeviceIdentifier", audio.outputDeviceIdentifier);
    setProperty(value, "sampleRate", audio.sampleRate);
    return value;
}

juce::var midiValue(const MidiSettings& midi) {
    auto value = makeObject();
    setProperty(value, "channelFilter", static_cast<int>(midi.channelFilter));
    setProperty(value, "preferredInputIdentifier", midi.preferredInputIdentifier);
    return value;
}

juce::var uiValue(const ProjectUiState& ui) {
    auto value = makeObject();
    setProperty(value, "fixedTriggerVelocity", static_cast<int>(ui.fixedTriggerVelocity));
    setProperty(value, "previewVolume", static_cast<double>(ui.previewVolume));
    setProperty(value, "selectedBank", static_cast<int>(ui.selectedBank));
    setProperty(value, "selectedPad", static_cast<int>(ui.selectedPad));
    setProperty(value, "windowHeight", ui.windowHeight);
    setProperty(value, "windowWidth", ui.windowWidth);
    setProperty(value, "windowX", ui.windowX);
    setProperty(value, "windowY", ui.windowY);
    return value;
}

juce::var manifestValue(const Project& project) {
    const auto& state = project.state();
    auto value = makeObject();
    setProperty(value, "applicationVersion", juce::String{product::version.data()});
    setProperty(value, "audio", audioValue(state.audio));

    std::vector<juce::var> assets;
    assets.reserve(state.assets.size());
    for (const auto& asset : state.assets)
        assets.push_back(assetValue(asset));
    setProperty(value, "assets", makeArray(assets));

    std::vector<juce::var> derivedAssets;
    derivedAssets.reserve(state.derivedAssets.size());
    for (const auto& asset : state.derivedAssets)
        derivedAssets.push_back(derivedAssetValue(asset));
    setProperty(value, "derivedAssets", makeArray(derivedAssets));

    std::vector<juce::var> recordedAssets;
    recordedAssets.reserve(state.recordedAssets.size());
    for (const auto& asset : state.recordedAssets)
        recordedAssets.push_back(recordedAssetValue(asset));
    setProperty(value, "recordedAssets", makeArray(recordedAssets));

    std::vector<juce::var> sliceSets;
    sliceSets.reserve(state.sliceSets.size());
    for (const auto& sliceSet : state.sliceSets)
        sliceSets.push_back(sliceSetValue(sliceSet));
    setProperty(value, "sliceSets", makeArray(sliceSets));

    std::vector<juce::var> banks;
    banks.reserve(state.banks.size());
    for (const auto& bank : state.banks)
        banks.push_back(bankValue(bank));
    setProperty(value, "banks", makeArray(banks));

    setProperty(value, "bundleIdentifier", juce::String{product::bundleId.data()});
    setProperty(value, "company", juce::String{product::company.data()});
    setProperty(value, "format", "padflow-project");
    setProperty(value, "midi", midiValue(state.midi));
    setProperty(value, "product", juce::String{product::name.data()});
    setProperty(value, "projectName", project.name());
    setProperty(value, "projectUuid", project.uuid());
    setProperty(value, "recording", recordingValue(state.recording));
    setProperty(value, "revision", decimalString(project.revision()));
    setProperty(value, "schemaVersion", project.schemaVersion());
    setProperty(value, "ui", uiValue(state.ui));
    return value;
}

juce::File temporarySibling(const juce::File& destination) {
    return destination.getSiblingFile(destination.getFileName() + ".tmp");
}

juce::Result requireObject(const juce::var& value, const juce::String& label,
                           const juce::DynamicObject*& object) {
    object = value.getDynamicObject();
    if (object == nullptr)
        return juce::Result::fail(label + " must be a JSON object");
    return juce::Result::ok();
}

juce::Result readString(const juce::DynamicObject& object, const char* name, juce::String& output) {
    const auto value = object.getProperty(juce::Identifier{name});
    if (!value.isString())
        return juce::Result::fail(juce::String{name} + " must be a string");
    output = value.toString();
    return juce::Result::ok();
}

juce::Result readBool(const juce::DynamicObject& object, const char* name, bool& output) {
    const auto value = object.getProperty(juce::Identifier{name});
    if (!value.isBool())
        return juce::Result::fail(juce::String{name} + " must be a boolean");
    output = static_cast<bool>(value);
    return juce::Result::ok();
}

template <typename Integer>
juce::Result readInteger(const juce::DynamicObject& object, const char* name, Integer& output) {
    static_assert(std::is_integral_v<Integer>);
    const auto value = object.getProperty(juce::Identifier{name});
    if (!value.isInt() && !value.isInt64())
        return juce::Result::fail(juce::String{name} + " must be an integer");

    const auto parsed = static_cast<juce::int64>(value);
    if constexpr (std::is_unsigned_v<Integer>) {
        if (parsed < 0 || static_cast<std::uint64_t>(parsed) >
                              static_cast<std::uint64_t>(std::numeric_limits<Integer>::max()))
            return juce::Result::fail(juce::String{name} + " is outside its integer range");
    } else if (parsed < static_cast<juce::int64>(std::numeric_limits<Integer>::lowest()) ||
               parsed > static_cast<juce::int64>(std::numeric_limits<Integer>::max())) {
        return juce::Result::fail(juce::String{name} + " is outside its integer range");
    }

    output = static_cast<Integer>(parsed);
    return juce::Result::ok();
}

template <typename Integer>
juce::Result readDecimalString(const juce::DynamicObject& object, const char* name,
                               Integer& output) {
    static_assert(std::is_integral_v<Integer>);
    const auto value = object.getProperty(juce::Identifier{name});
    if (!value.isString())
        return juce::Result::fail(juce::String{name} + " must be a decimal string");

    const auto text = value.toString().toStdString();
    if (text.empty())
        return juce::Result::fail(juce::String{name} + " must not be empty");

    Integer parsed{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return juce::Result::fail(juce::String{name} + " is not a canonical integer");
    output = parsed;
    return juce::Result::ok();
}

juce::Result readDouble(const juce::DynamicObject& object, const char* name, double& output) {
    const auto value = object.getProperty(juce::Identifier{name});
    if (!value.isDouble() && !value.isInt() && !value.isInt64())
        return juce::Result::fail(juce::String{name} + " must be numeric");
    output = static_cast<double>(value);
    if (!std::isfinite(output))
        return juce::Result::fail(juce::String{name} + " must be finite");
    return juce::Result::ok();
}

juce::Result readFloat(const juce::DynamicObject& object, const char* name, float& output) {
    double parsed = 0.0;
    if (const auto result = readDouble(object, name, parsed); result.failed())
        return result;
    if (parsed < -static_cast<double>(std::numeric_limits<float>::max()) ||
        parsed > static_cast<double>(std::numeric_limits<float>::max()))
        return juce::Result::fail(juce::String{name} + " is outside the float range");
    output = static_cast<float>(parsed);
    return juce::Result::ok();
}

juce::Result readEnvelope(const juce::var& value, EnvelopeParameters& envelope) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "envelope", object); result.failed())
        return result;
    if (const auto result = readFloat(*object, "attackSeconds", envelope.attackSeconds);
        result.failed())
        return result;
    if (const auto result = readFloat(*object, "decaySeconds", envelope.decaySeconds);
        result.failed())
        return result;
    if (const auto result = readFloat(*object, "releaseSeconds", envelope.releaseSeconds);
        result.failed())
        return result;
    return readFloat(*object, "sustainLevel", envelope.sustainLevel);
}

juce::Result readSamplePlayback(const juce::var& value, SamplePlaybackSettings& playback) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "editing", object); result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "endFrame", playback.endFrame);
        result.failed())
        return result;
    if (const auto result = readBool(*object, "loopEnabled", playback.loopEnabled); result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "loopEndFrame", playback.loopEndFrame);
        result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "loopStartFrame", playback.loopStartFrame);
        result.failed())
        return result;
    if (const auto result = readBool(*object, "reverseEnabled", playback.reverseEnabled);
        result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "startFrame", playback.startFrame);
        result.failed())
        return result;
    if (const auto result = readBool(*object, "zeroCrossingSnap", playback.zeroCrossingSnap);
        result.failed())
        return result;
    playback.initialized = true;
    return juce::Result::ok();
}

juce::Result readLayer(const juce::var& value, SampleLayer& layer) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "layer", object); result.failed())
        return result;
    if (const auto result = readString(*object, "assetUuid", layer.assetUuid); result.failed())
        return result;
    const auto editingIdentifier = juce::Identifier{"editing"};
    if (object->hasProperty(editingIdentifier))
        if (const auto result =
                readSamplePlayback(object->getProperty(editingIdentifier), layer.playback);
            result.failed())
            return result;
    if (const auto result = readBool(*object, "enabled", layer.enabled); result.failed())
        return result;
    if (const auto result = readFloat(*object, "gainDecibels", layer.gainDecibels); result.failed())
        return result;
    if (const auto result = readFloat(*object, "pan", layer.pan); result.failed())
        return result;
    const auto sliceAssignmentIdentifier = juce::Identifier{"sliceAssignment"};
    if (object->hasProperty(sliceAssignmentIdentifier)) {
        const juce::DynamicObject* assignment = nullptr;
        if (const auto result = requireObject(object->getProperty(sliceAssignmentIdentifier),
                                              "sliceAssignment", assignment);
            result.failed())
            return result;
        if (const auto result =
                readString(*assignment, "assignmentSessionUuid", layer.assignmentSessionUuid);
            result.failed())
            return result;
        if (const auto result = readString(*assignment, "sliceSetUuid", layer.sliceSetUuid);
            result.failed())
            return result;
        if (const auto result = readString(*assignment, "sliceUuid", layer.sliceUuid);
            result.failed())
            return result;
    }
    if (const auto result = readFloat(*object, "tuningCents", layer.tuningCents); result.failed())
        return result;
    if (const auto result = readString(*object, "uuid", layer.uuid); result.failed())
        return result;
    if (const auto result = readInteger(*object, "velocityMaximum", layer.velocityMaximum);
        result.failed())
        return result;
    return readInteger(*object, "velocityMinimum", layer.velocityMinimum);
}

juce::Result readSliceAlgorithm(const juce::DynamicObject& object, SliceAlgorithm& algorithm) {
    juce::String name;
    if (const auto result = readString(object, "algorithm", name); result.failed())
        return result;
    if (name == "equal")
        algorithm = SliceAlgorithm::equal;
    else if (name == "fixed-length")
        algorithm = SliceAlgorithm::fixedLength;
    else if (name == "transient")
        algorithm = SliceAlgorithm::transient;
    else if (name == "manual")
        algorithm = SliceAlgorithm::manual;
    else if (name == "lazy")
        algorithm = SliceAlgorithm::lazy;
    else
        return juce::Result::fail("slice algorithm is unsupported");
    return juce::Result::ok();
}

juce::Result readSliceParameters(const juce::var& value, SliceAlgorithmParameters& parameters) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "slice parameters", object); result.failed())
        return result;
    if (const auto result =
            readDecimalString(*object, "attackLookBackFrames", parameters.attackLookBackFrames);
        result.failed())
        return result;
    juce::String displayUnit;
    if (const auto result = readString(*object, "displayUnit", displayUnit); result.failed())
        return result;
    if (displayUnit == "frames")
        parameters.displayUnit = SliceDisplayUnit::frames;
    else if (displayUnit == "milliseconds")
        parameters.displayUnit = SliceDisplayUnit::milliseconds;
    else
        return juce::Result::fail("slice display unit is unsupported");
    if (const auto result =
            readDecimalString(*object, "fixedLengthFrames", parameters.fixedLengthFrames);
        result.failed())
        return result;
    if (const auto result =
            readDecimalString(*object, "minimumSliceFrames", parameters.minimumSliceFrames);
        result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "quantizeFrames", parameters.quantizeFrames);
        result.failed())
        return result;
    juce::String remainder;
    if (const auto result = readString(*object, "remainderPolicy", remainder); result.failed())
        return result;
    if (remainder == "include")
        parameters.remainderPolicy = SliceRemainderPolicy::include;
    else if (remainder == "discard")
        parameters.remainderPolicy = SliceRemainderPolicy::discard;
    else
        return juce::Result::fail("slice remainder policy is unsupported");
    if (const auto result = readDecimalString(*object, "sliceCount", parameters.sliceCount);
        result.failed())
        return result;
    if (const auto result =
            readFloat(*object, "transientSensitivity", parameters.transientSensitivity);
        result.failed())
        return result;
    return readFloat(*object, "transientThresholdFloor", parameters.transientThresholdFloor);
}

juce::Result readSliceRegion(const juce::var& value, SliceRegion& slice) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "slice", object); result.failed())
        return result;
    const auto colourIdentifier = juce::Identifier{"colourArgb"};
    if (object->hasProperty(colourIdentifier)) {
        std::uint32_t colour = 0U;
        if (const auto result = readDecimalString(*object, "colourArgb", colour); result.failed())
            return result;
        slice.colourArgb = colour;
    }
    if (const auto result = readDecimalString(*object, "endFrame", slice.endFrame); result.failed())
        return result;
    if (const auto result = readString(*object, "name", slice.name); result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "startFrame", slice.startFrame);
        result.failed())
        return result;
    return readString(*object, "uuid", slice.uuid);
}

juce::Result readSliceSet(const juce::var& value, SliceSet& sliceSet) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "slice set", object); result.failed())
        return result;
    if (const auto result = readSliceAlgorithm(*object, sliceSet.algorithm); result.failed())
        return result;
    if (const auto result = readInteger(*object, "algorithmVersion", sliceSet.algorithmVersion);
        result.failed())
        return result;
    if (const auto result =
            readSliceParameters(object->getProperty("parameters"), sliceSet.parameters);
        result.failed())
        return result;
    if (const auto result = readString(*object, "sourceAssetUuid", sliceSet.sourceAssetUuid);
        result.failed())
        return result;
    if (const auto result = readString(*object, "sourceFingerprint", sliceSet.sourceFingerprint);
        result.failed())
        return result;
    if (const auto result = readString(*object, "sourceLayerUuid", sliceSet.sourceLayerUuid);
        result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "sourceTrimEnd", sliceSet.sourceTrimEnd);
        result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "sourceTrimStart", sliceSet.sourceTrimStart);
        result.failed())
        return result;
    const auto slicesValue = object->getProperty("slices");
    const auto* slices = slicesValue.getArray();
    if (slices == nullptr)
        return juce::Result::fail("slices must be an array");
    sliceSet.slices.clear();
    sliceSet.slices.reserve(static_cast<std::size_t>(slices->size()));
    for (const auto& sliceValue : *slices) {
        SliceRegion slice;
        if (const auto result = readSliceRegion(sliceValue, slice); result.failed())
            return result;
        sliceSet.slices.push_back(std::move(slice));
    }
    return readString(*object, "uuid", sliceSet.uuid);
}

juce::Result readPadParameters(const juce::var& value, PadParameters& parameters) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "parameters", object); result.failed())
        return result;
    if (const auto result = readInteger(*object, "chokeGroup", parameters.chokeGroup);
        result.failed())
        return result;
    if (const auto result = readInteger(*object, "coarseSemitones", parameters.coarseSemitones);
        result.failed())
        return result;
    if (const auto result = readEnvelope(object->getProperty("envelope"), parameters.envelope);
        result.failed())
        return result;
    if (const auto result = readFloat(*object, "fineCents", parameters.fineCents); result.failed())
        return result;
    if (const auto result = readFloat(*object, "gainDecibels", parameters.gainDecibels);
        result.failed())
        return result;
    if (const auto result = readInteger(*object, "maximumVoices", parameters.maximumVoices);
        result.failed())
        return result;
    if (const auto result = readFloat(*object, "pan", parameters.pan); result.failed())
        return result;

    juce::String playback;
    if (const auto result = readString(*object, "playbackMode", playback); result.failed())
        return result;
    if (playback == "one-shot")
        parameters.playbackMode = PlaybackMode::oneShot;
    else if (playback == "gate")
        parameters.playbackMode = PlaybackMode::gate;
    else if (playback == "toggle")
        parameters.playbackMode = PlaybackMode::toggle;
    else
        return juce::Result::fail("playbackMode is unsupported");

    juce::String polyphony;
    if (const auto result = readString(*object, "polyphonyMode", polyphony); result.failed())
        return result;
    if (polyphony == "poly")
        parameters.polyphonyMode = PolyphonyMode::poly;
    else if (polyphony == "mono")
        parameters.polyphonyMode = PolyphonyMode::mono;
    else
        return juce::Result::fail("polyphonyMode is unsupported");
    return juce::Result::ok();
}

juce::Result readPad(const juce::var& value, Pad& pad) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "pad", object); result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "colourArgb", pad.colourArgb);
        result.failed())
        return result;
    if (const auto result = readString(*object, "keyboardKey", pad.keyboardKey); result.failed())
        return result;

    const auto layersValue = object->getProperty("layers");
    const auto* layers = layersValue.getArray();
    if (layers == nullptr || layers->size() != static_cast<int>(minimumLayersPerPad))
        return juce::Result::fail("layers must contain exactly four entries");
    for (std::size_t index = 0; index < minimumLayersPerPad; ++index)
        if (const auto result = readLayer((*layers)[static_cast<int>(index)], pad.layers[index]);
            result.failed())
            return result;

    if (const auto result = readInteger(*object, "midiNote", pad.midiNote); result.failed())
        return result;
    if (const auto result = readString(*object, "name", pad.name); result.failed())
        return result;
    if (const auto result = readPadParameters(object->getProperty("parameters"), pad.parameters);
        result.failed())
        return result;
    return readString(*object, "uuid", pad.uuid);
}

juce::Result readBank(const juce::var& value, PadBank& bank) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "bank", object); result.failed())
        return result;
    if (const auto result = readString(*object, "name", bank.name); result.failed())
        return result;

    const auto padsValue = object->getProperty("pads");
    const auto* pads = padsValue.getArray();
    if (pads == nullptr || pads->size() != static_cast<int>(padsPerBank))
        return juce::Result::fail("pads must contain exactly sixteen entries");
    for (std::size_t index = 0; index < padsPerBank; ++index)
        if (const auto result = readPad((*pads)[static_cast<int>(index)], bank.pads[index]);
            result.failed())
            return result;
    return readString(*object, "uuid", bank.uuid);
}

juce::Result readAsset(const juce::var& value, ExternalAssetReference& asset) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "asset", object); result.failed())
        return result;
    if (const auto result = readInteger(*object, "channels", asset.channels); result.failed())
        return result;
    if (const auto result = readString(*object, "contentFingerprint", asset.contentFingerprint);
        result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "decodedBytes", asset.decodedBytes);
        result.failed())
        return result;
    if (const auto result = readString(*object, "format", asset.format); result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "frameCount", asset.frameCount);
        result.failed())
        return result;
    if (const auto result = readBool(*object, "missing", asset.missing); result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "modificationTimeMilliseconds",
                                              asset.modificationTimeMilliseconds);
        result.failed())
        return result;
    if (const auto result = readString(*object, "originalName", asset.originalName);
        result.failed())
        return result;
    if (const auto result = readString(*object, "originalPath", asset.originalPath);
        result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "sourceFileBytes", asset.sourceFileBytes);
        result.failed())
        return result;
    if (const auto result = readDouble(*object, "sourceSampleRate", asset.sourceSampleRate);
        result.failed())
        return result;
    return readString(*object, "uuid", asset.uuid);
}

juce::Result readDerivedAsset(const juce::var& value, DerivedAssetRecord& asset) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "derived asset", object); result.failed())
        return result;
    if (const auto result = readInteger(*object, "algorithmVersion", asset.algorithmVersion);
        result.failed())
        return result;
    if (const auto result =
            readString(*object, "canonicalOperationParameters", asset.canonicalOperationParameters);
        result.failed())
        return result;
    if (const auto result = readString(*object, "creationMetadata", asset.creationMetadata);
        result.failed())
        return result;
    if (const auto result = readString(*object, "derivedAssetUuid", asset.derivedAssetUuid);
        result.failed())
        return result;
    if (const auto result = readString(*object, "operationIdentifier", asset.operationIdentifier);
        result.failed())
        return result;
    if (const auto result = readString(*object, "outputFingerprint", asset.outputFingerprint);
        result.failed())
        return result;
    if (const auto result = readString(*object, "parentAssetUuid", asset.parentAssetUuid);
        result.failed())
        return result;
    if (const auto result =
            readString(*object, "projectOwnedRelativePath", asset.projectOwnedRelativePath);
        result.failed())
        return result;
    return readString(*object, "sourceFingerprint", asset.sourceFingerprint);
}

juce::Result readRecordedAsset(const juce::var& value, RecordedAssetRecord& asset) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "recorded asset", object); result.failed())
        return result;
    if (const auto result = readInteger(*object, "channels", asset.channels); result.failed())
        return result;
    if (const auto result = readString(*object, "contentFingerprint", asset.contentFingerprint);
        result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "frameCount", asset.frameCount);
        result.failed())
        return result;
    if (const auto result =
            readString(*object, "inputDeviceIdentifier", asset.inputDeviceIdentifier);
        result.failed())
        return result;
    if (const auto result =
            readString(*object, "projectOwnedRelativePath", asset.projectOwnedRelativePath);
        result.failed())
        return result;
    if (const auto result = readString(*object, "recordedAssetUuid", asset.recordedAssetUuid);
        result.failed())
        return result;
    if (const auto result = readDouble(*object, "sampleRate", asset.sampleRate); result.failed())
        return result;
    if (const auto result = readString(*object, "sessionUuid", asset.sessionUuid); result.failed())
        return result;
    if (const auto result = readString(*object, "targetLayerUuid", asset.targetLayerUuid);
        result.failed())
        return result;
    if (const auto result = readString(*object, "targetPadUuid", asset.targetPadUuid);
        result.failed())
        return result;
    if (const auto result = readString(*object, "targetProjectUuid", asset.targetProjectUuid);
        result.failed())
        return result;
    return readDecimalString(*object, "targetProjectRevision", asset.targetProjectRevision);
}

juce::Result readRecording(const juce::var& value, RecordingPreferences& recording) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "recording", object); result.failed())
        return result;
    if (const auto result = readBool(*object, "autoAssign", recording.autoAssign); result.failed())
        return result;
    if (const auto result = readInteger(*object, "channels", recording.channels); result.failed())
        return result;
    if (const auto result =
            readInteger(*object, "preRollMilliseconds", recording.preRollMilliseconds);
        result.failed())
        return result;
    if (const auto result = readFloat(*object, "thresholdDecibels", recording.thresholdDecibels);
        result.failed())
        return result;
    return readBool(*object, "thresholdMode", recording.thresholdMode);
}

juce::Result readAudio(const juce::var& value, AudioSettings& audio) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "audio", object); result.failed())
        return result;
    if (const auto result = readInteger(*object, "bufferSize", audio.bufferSize); result.failed())
        return result;
    if (const auto result = readDecimalString(*object, "inputChannelMask", audio.inputChannelMask);
        result.failed())
        return result;
    if (const auto result =
            readString(*object, "inputDeviceIdentifier", audio.inputDeviceIdentifier);
        result.failed())
        return result;
    if (const auto result =
            readDecimalString(*object, "outputChannelMask", audio.outputChannelMask);
        result.failed())
        return result;
    if (const auto result =
            readString(*object, "outputDeviceIdentifier", audio.outputDeviceIdentifier);
        result.failed())
        return result;
    return readDouble(*object, "sampleRate", audio.sampleRate);
}

juce::Result readMidi(const juce::var& value, MidiSettings& midi) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "midi", object); result.failed())
        return result;
    if (const auto result = readInteger(*object, "channelFilter", midi.channelFilter);
        result.failed())
        return result;
    return readString(*object, "preferredInputIdentifier", midi.preferredInputIdentifier);
}

juce::Result readUi(const juce::var& value, ProjectUiState& ui) {
    const juce::DynamicObject* object = nullptr;
    if (const auto result = requireObject(value, "ui", object); result.failed())
        return result;
    if (const auto result = readInteger(*object, "fixedTriggerVelocity", ui.fixedTriggerVelocity);
        result.failed())
        return result;
    if (const auto result = readFloat(*object, "previewVolume", ui.previewVolume); result.failed())
        return result;
    if (const auto result = readInteger(*object, "selectedBank", ui.selectedBank); result.failed())
        return result;
    if (const auto result = readInteger(*object, "selectedPad", ui.selectedPad); result.failed())
        return result;
    if (const auto result = readInteger(*object, "windowHeight", ui.windowHeight); result.failed())
        return result;
    if (const auto result = readInteger(*object, "windowWidth", ui.windowWidth); result.failed())
        return result;
    if (const auto result = readInteger(*object, "windowX", ui.windowX); result.failed())
        return result;
    return readInteger(*object, "windowY", ui.windowY);
}

juce::Result parseManifest(const juce::String& text, Project& project) {
    const auto parsed = juce::JSON::parse(text);
    const juce::DynamicObject* root = nullptr;
    if (const auto result = requireObject(parsed, "Project manifest", root); result.failed())
        return result;

    int schema = 0;
    if (const auto result = readInteger(*root, "schemaVersion", schema); result.failed())
        return result;
    if (schema != product::schemaVersion)
        return juce::Result::fail("Unsupported project schema version");

    juce::String format;
    if (const auto result = readString(*root, "format", format); result.failed())
        return result;
    if (format != "padflow-project")
        return juce::Result::fail("Unsupported project format");

    juce::String uuid;
    juce::String name;
    std::uint64_t revision = 0U;
    if (const auto result = readString(*root, "projectUuid", uuid); result.failed())
        return result;
    if (const auto result = readString(*root, "projectName", name); result.failed())
        return result;
    if (const auto result = readDecimalString(*root, "revision", revision); result.failed())
        return result;
    if (uuid.trim().isEmpty() || name.trim().isEmpty())
        return juce::Result::fail("Project manifest is missing required values");

    const auto banksIdentifier = juce::Identifier{"banks"};
    if (!root->hasProperty(banksIdentifier)) {
        for (const auto* property : {"assets", "audio", "derivedAssets", "midi", "recordedAssets",
                                     "recording", "sliceSets", "ui"})
            if (root->hasProperty(juce::Identifier{property}))
                return juce::Result::fail("Project manifest has an incomplete model payload");
        auto legacy = Project::createEmpty(name, uuid);
        legacy.restoreRevision(revision);
        project = std::move(legacy);
        return juce::Result::ok();
    }

    auto state = makeDefaultProjectState(uuid, name);
    const auto banksValue = root->getProperty(banksIdentifier);
    const auto* banks = banksValue.getArray();
    if (banks == nullptr || banks->size() != static_cast<int>(padBankCount))
        return juce::Result::fail("banks must contain exactly four entries");
    for (std::size_t index = 0; index < padBankCount; ++index)
        if (const auto result = readBank((*banks)[static_cast<int>(index)], state.banks[index]);
            result.failed())
            return result;

    const auto assetsValue = root->getProperty("assets");
    const auto* assets = assetsValue.getArray();
    if (assets == nullptr)
        return juce::Result::fail("assets must be an array");
    state.assets.clear();
    state.assets.reserve(static_cast<std::size_t>(assets->size()));
    for (const auto& assetValueEntry : *assets) {
        ExternalAssetReference asset;
        if (const auto result = readAsset(assetValueEntry, asset); result.failed())
            return result;
        state.assets.push_back(std::move(asset));
    }

    const auto derivedIdentifier = juce::Identifier{"derivedAssets"};
    if (root->hasProperty(derivedIdentifier)) {
        const auto derivedAssetsValue = root->getProperty(derivedIdentifier);
        const auto* derivedAssets = derivedAssetsValue.getArray();
        if (derivedAssets == nullptr)
            return juce::Result::fail("derivedAssets must be an array");
        state.derivedAssets.reserve(static_cast<std::size_t>(derivedAssets->size()));
        for (const auto& derivedValue : *derivedAssets) {
            DerivedAssetRecord derived;
            if (const auto result = readDerivedAsset(derivedValue, derived); result.failed())
                return result;
            state.derivedAssets.push_back(std::move(derived));
        }
    }

    const auto recordedIdentifier = juce::Identifier{"recordedAssets"};
    if (root->hasProperty(recordedIdentifier)) {
        const auto recordedAssetsValue = root->getProperty(recordedIdentifier);
        const auto* recordedAssets = recordedAssetsValue.getArray();
        if (recordedAssets == nullptr)
            return juce::Result::fail("recordedAssets must be an array");
        state.recordedAssets.reserve(static_cast<std::size_t>(recordedAssets->size()));
        for (const auto& recordedValue : *recordedAssets) {
            RecordedAssetRecord recorded;
            if (const auto result = readRecordedAsset(recordedValue, recorded); result.failed())
                return result;
            state.recordedAssets.push_back(std::move(recorded));
        }
    }

    const auto sliceSetsIdentifier = juce::Identifier{"sliceSets"};
    if (root->hasProperty(sliceSetsIdentifier)) {
        const auto sliceSetsValue = root->getProperty(sliceSetsIdentifier);
        const auto* sliceSets = sliceSetsValue.getArray();
        if (sliceSets == nullptr)
            return juce::Result::fail("sliceSets must be an array");
        state.sliceSets.reserve(static_cast<std::size_t>(sliceSets->size()));
        for (const auto& sliceSetValueEntry : *sliceSets) {
            SliceSet sliceSet;
            if (const auto result = readSliceSet(sliceSetValueEntry, sliceSet); result.failed())
                return result;
            state.sliceSets.push_back(std::move(sliceSet));
        }
    }

    if (const auto result = readAudio(root->getProperty("audio"), state.audio); result.failed())
        return result;
    if (const auto result = readMidi(root->getProperty("midi"), state.midi); result.failed())
        return result;
    const auto recordingIdentifier = juce::Identifier{"recording"};
    if (root->hasProperty(recordingIdentifier))
        if (const auto result =
                readRecording(root->getProperty(recordingIdentifier), state.recording);
            result.failed())
            return result;
    if (const auto result = readUi(root->getProperty("ui"), state.ui); result.failed())
        return result;
    return project.restoreState(std::move(state), revision);
}
} // namespace

juce::String ProjectSerializer::canonicalManifest(const Project& project) {
    return juce::JSON::toString(manifestValue(project), false, 17) + "\n";
}

juce::Result ProjectSerializer::restoreCanonicalManifest(const juce::String& manifest,
                                                         Project& project) {
    return parseManifest(manifest, project);
}

SerializationResult ProjectSerializer::save(const Project& project, const juce::File& destination) {
    if (destination.getFileExtension().toLowerCase() !=
        juce::String{product::projectExtension.data()})
        return {false, "Project destination must use the .padflow extension"};
    if (const auto validation = validateProjectState(project.state()); validation.failed())
        return {false, "Project state is invalid: " + validation.getErrorMessage()};

    const auto temporary = temporarySibling(destination);
    if (temporary.existsAsFile() && !temporary.deleteFile())
        return {false, "Could not remove a stale temporary project"};

    juce::ZipFile::Builder archive;
    const auto manifest = canonicalManifest(project);
    archive.addEntry(std::make_unique<juce::MemoryInputStream>(manifest.toRawUTF8(),
                                                               manifest.getNumBytesAsUTF8(), true),
                     9, "manifest.json", juce::Time{0});

    {
        juce::FileOutputStream output{temporary};
        if (!output.openedOk() || !archive.writeToStream(output, nullptr)) {
            temporary.deleteFile();
            return {false, "Could not write the temporary project archive"};
        }
        output.flush();
        if (output.getStatus().failed()) {
            temporary.deleteFile();
            return {false, "Could not flush the temporary project archive"};
        }
    }

    juce::ZipFile validationArchive{temporary};
    const auto manifestIndex = validationArchive.getIndexOfFileName("manifest.json");
    if (manifestIndex < 0) {
        temporary.deleteFile();
        return {false, "Temporary project archive has no manifest"};
    }

    std::unique_ptr<juce::InputStream> validationStream(
        validationArchive.createStreamForEntry(manifestIndex));
    auto validationProject = Project::createEmpty();
    const auto validationResult =
        validationStream != nullptr
            ? parseManifest(validationStream->readEntireStreamAsString(), validationProject)
            : juce::Result::fail("Temporary project manifest could not be read");
    if (validationResult.failed() || validationProject.state() != project.state() ||
        validationProject.revision() != project.revision()) {
        temporary.deleteFile();
        return {false, "Temporary project manifest failed semantic validation"};
    }

    const auto backup = destination.getSiblingFile(destination.getFileName() + ".bak");
    if (destination.existsAsFile()) {
        if (backup.existsAsFile() && !backup.deleteFile()) {
            temporary.deleteFile();
            return {false, "Could not remove the previous project backup"};
        }
        if (!destination.moveFileTo(backup)) {
            temporary.deleteFile();
            return {false, "Could not rotate the previous project backup"};
        }
    }

    if (!temporary.moveFileTo(destination)) {
        if (backup.existsAsFile())
            juce::ignoreUnused(backup.moveFileTo(destination));
        temporary.deleteFile();
        return {false, "Could not publish the validated project archive"};
    }

    return {true, {}};
}

juce::Result ProjectSerializer::load(const juce::File& source, Project& project) {
    if (!source.existsAsFile())
        return juce::Result::fail("Project file does not exist");

    juce::ZipFile archive{source};
    const auto manifestIndex = archive.getIndexOfFileName("manifest.json");
    if (manifestIndex < 0)
        return juce::Result::fail("Project archive has no manifest");

    std::unique_ptr<juce::InputStream> stream(archive.createStreamForEntry(manifestIndex));
    if (stream == nullptr)
        return juce::Result::fail("Project manifest could not be read");
    return parseManifest(stream->readEntireStreamAsString(), project);
}
} // namespace padflow
