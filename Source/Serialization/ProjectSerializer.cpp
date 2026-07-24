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
    setProperty(value, "tuningCents", static_cast<double>(layer.tuningCents));
    setProperty(value, "uuid", layer.uuid);
    setProperty(value, "velocityMaximum", static_cast<int>(layer.velocityMaximum));
    setProperty(value, "velocityMinimum", static_cast<int>(layer.velocityMinimum));
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
    if (const auto result = readFloat(*object, "tuningCents", layer.tuningCents); result.failed())
        return result;
    if (const auto result = readString(*object, "uuid", layer.uuid); result.failed())
        return result;
    if (const auto result = readInteger(*object, "velocityMaximum", layer.velocityMaximum);
        result.failed())
        return result;
    return readInteger(*object, "velocityMinimum", layer.velocityMinimum);
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
        for (const auto* property : {"assets", "audio", "midi", "ui"})
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

    if (const auto result = readAudio(root->getProperty("audio"), state.audio); result.failed())
        return result;
    if (const auto result = readMidi(root->getProperty("midi"), state.midi); result.failed())
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
