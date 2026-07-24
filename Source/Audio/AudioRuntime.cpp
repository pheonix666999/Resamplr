#include "AudioRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace padflow {
namespace {
juce::BigInteger channelMask(const std::uint64_t mask) {
    juce::BigInteger channels;
    for (int bit = 0; bit < 64; ++bit)
        if ((mask & (std::uint64_t{1U} << static_cast<unsigned int>(bit))) != 0U)
            channels.setBit(bit);
    return channels;
}

std::uint64_t channelMask(const juce::BigInteger& channels) noexcept {
    std::uint64_t mask = 0U;
    for (int bit = 0; bit < 64; ++bit)
        if (channels[bit])
            mask |= std::uint64_t{1U} << static_cast<unsigned int>(bit);
    return mask;
}
} // namespace

AudioRuntime::~AudioRuntime() {
    close();
}

juce::Result AudioRuntime::initialise(const AudioSettings& preferred) {
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.outputDeviceName = preferred.outputDeviceIdentifier;
    setup.inputDeviceName.clear();
    setup.sampleRate = preferred.sampleRate;
    setup.bufferSize = static_cast<int>(preferred.bufferSize);
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.useDefaultOutputChannels = preferred.outputChannelMask == 0U;
    setup.outputChannels = channelMask(preferred.outputChannelMask);

    auto error = manager_.initialise(0, 2, nullptr, true, {}, &setup);
    if (error.isNotEmpty()) {
        error = manager_.initialiseWithDefaultDevices(0, 2);
        if (error.isNotEmpty())
            return juce::Result::fail(error);
    }
    if (!callbackRegistered_.exchange(true, std::memory_order_acq_rel))
        manager_.addAudioCallback(this);
    deviceError_.store(false, std::memory_order_release);
    resetDropoutCount();
    return juce::Result::ok();
}

juce::Result AudioRuntime::applySettings(const AudioSettings& settings) {
    auto setup = manager_.getAudioDeviceSetup();
    setup.outputDeviceName = settings.outputDeviceIdentifier;
    setup.inputDeviceName.clear();
    setup.sampleRate = settings.sampleRate;
    setup.bufferSize = static_cast<int>(settings.bufferSize);
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.useDefaultOutputChannels = settings.outputChannelMask == 0U;
    setup.outputChannels = channelMask(settings.outputChannelMask);
    engine_.panic();
    const auto error = manager_.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty())
        return juce::Result::fail(error);
    deviceError_.store(false, std::memory_order_release);
    resetDropoutCount();
    return juce::Result::ok();
}

void AudioRuntime::close() {
    engine_.panic();
    if (activeMidiIdentifier_.isNotEmpty() && activeMidiCallback_ != nullptr) {
        manager_.removeMidiInputDeviceCallback(activeMidiIdentifier_, activeMidiCallback_);
        manager_.setMidiInputDeviceEnabled(activeMidiIdentifier_, false);
    }
    activeMidiIdentifier_.clear();
    activeMidiCallback_ = nullptr;
    if (callbackRegistered_.exchange(false, std::memory_order_acq_rel))
        manager_.removeAudioCallback(this);
    manager_.closeAudioDevice();
}

juce::Result AudioRuntime::restart() {
    engine_.panic();
    manager_.restartLastAudioDevice();
    if (manager_.getCurrentAudioDevice() == nullptr)
        return juce::Result::fail("Audio device could not be restarted");
    resetDropoutCount();
    return juce::Result::ok();
}

std::vector<AudioOutputDeviceInfo> AudioRuntime::outputDevices() {
    std::vector<AudioOutputDeviceInfo> result;
    for (const auto* type : manager_.getAvailableDeviceTypes())
        if (type != nullptr)
            for (const auto& name : type->getDeviceNames(false))
                result.push_back({type->getTypeName(), name});
    return result;
}

std::vector<juce::MidiDeviceInfo> AudioRuntime::midiInputDevices() {
    const auto devices = juce::MidiInput::getAvailableDevices();
    return std::vector<juce::MidiDeviceInfo>{devices.begin(), devices.end()};
}

void AudioRuntime::setMidiInputEnabled(const juce::String& identifier, const bool enabled,
                                       juce::MidiInputCallback* const callback) {
    if (activeMidiIdentifier_.isNotEmpty() && activeMidiCallback_ != nullptr) {
        manager_.removeMidiInputDeviceCallback(activeMidiIdentifier_, activeMidiCallback_);
        manager_.setMidiInputDeviceEnabled(activeMidiIdentifier_, false);
    }
    activeMidiIdentifier_.clear();
    activeMidiCallback_ = nullptr;
    if (!enabled || identifier.isEmpty() || callback == nullptr)
        return;
    manager_.setMidiInputDeviceEnabled(identifier, true);
    manager_.addMidiInputDeviceCallback(identifier, callback);
    activeMidiIdentifier_ = identifier;
    activeMidiCallback_ = callback;
}

AudioSettings AudioRuntime::currentSettings() const {
    AudioSettings settings;
    const auto setup = manager_.getAudioDeviceSetup();
    settings.outputDeviceIdentifier = setup.outputDeviceName;
    settings.outputChannelMask =
        setup.useDefaultOutputChannels ? 0U : channelMask(setup.outputChannels);
    settings.sampleRate = setup.sampleRate;
    settings.bufferSize = setup.bufferSize > 0 ? static_cast<std::uint32_t>(setup.bufferSize) : 0U;
    return settings;
}

AudioRuntimeStatus AudioRuntime::status() {
    AudioRuntimeStatus value;
    const auto* device = manager_.getCurrentAudioDevice();
    value.deviceOpen = device != nullptr && !deviceError_.load(std::memory_order_acquire);
    if (device != nullptr) {
        const auto setup = manager_.getAudioDeviceSetup();
        value.deviceName = device->getName();
        value.sampleRate = setup.sampleRate;
        value.bufferSize = static_cast<std::uint32_t>(std::max(0, setup.bufferSize));
    }
    value.cpuUsage = std::clamp(manager_.getCpuUsage(), 0.0, 1.0);
    const auto xruns = static_cast<std::uint64_t>(std::max(0, manager_.getXRunCount()));
    const auto baseline = dropoutBaseline_.load(std::memory_order_acquire);
    value.dropoutCount = xruns >= baseline ? xruns - baseline : 0U;
    return value;
}

void AudioRuntime::resetDropoutCount() noexcept {
    dropoutBaseline_.store(static_cast<std::uint64_t>(std::max(0, manager_.getXRunCount())),
                           std::memory_order_release);
}

void AudioRuntime::setTestToneEnabled(const bool enabled) noexcept {
    testToneEnabled_.store(enabled, std::memory_order_release);
}

bool AudioRuntime::isTestToneEnabled() const noexcept {
    return testToneEnabled_.load(std::memory_order_acquire);
}

PlaybackEngine& AudioRuntime::engine() noexcept {
    return engine_;
}

const PlaybackEngine& AudioRuntime::engine() const noexcept {
    return engine_;
}

void AudioRuntime::audioDeviceIOCallbackWithContext(
    const float* const* const inputChannelData, const int numInputChannels,
    float* const* const outputChannelData, const int numOutputChannels, const int numSamples,
    const juce::AudioIODeviceCallbackContext& context) {
    juce::ignoreUnused(inputChannelData, numInputChannels, context);
    if (panicRequested_.exchange(false, std::memory_order_acq_rel))
        engine_.panic();
    if (outputChannelData == nullptr || numOutputChannels <= 0 || numSamples <= 0)
        return;

    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            std::fill_n(outputChannelData[channel], static_cast<std::size_t>(numSamples), 0.0F);

    int offset = 0;
    while (offset < numSamples) {
        const auto count = std::min(numSamples - offset, static_cast<int>(scratchFrames));
        auto* left =
            outputChannelData[0] != nullptr ? outputChannelData[0] + offset : leftScratch_.data();
        auto* right = numOutputChannels > 1 && outputChannelData[1] != nullptr
                          ? outputChannelData[1] + offset
                          : rightScratch_.data();
        engine_.processBlock(left, right, static_cast<std::size_t>(count));

        if (testToneEnabled_.load(std::memory_order_relaxed)) {
            constexpr double frequency = 440.0;
            constexpr float amplitude = 0.1F;
            constexpr double twoPi = 6.28318530717958647692;
            const auto phaseStep = twoPi * frequency / activeSampleRate_;
            for (int frame = 0; frame < count; ++frame) {
                const auto sample = amplitude * static_cast<float>(std::sin(testTonePhase_));
                left[frame] += sample;
                right[frame] += sample;
                testTonePhase_ += phaseStep;
                if (testTonePhase_ >= twoPi)
                    testTonePhase_ -= twoPi;
            }
        }
        offset += count;
    }
}

void AudioRuntime::audioDeviceAboutToStart(juce::AudioIODevice* const device) {
    activeSampleRate_ = device != nullptr && device->getCurrentSampleRate() > 0.0
                            ? device->getCurrentSampleRate()
                            : 48000.0;
    engine_.prepare(activeSampleRate_);
    testTonePhase_ = 0.0;
    deviceError_.store(false, std::memory_order_release);
}

void AudioRuntime::audioDeviceStopped() {
    engine_.panic();
    testTonePhase_ = 0.0;
}

void AudioRuntime::audioDeviceError(const juce::String& errorMessage) {
    juce::ignoreUnused(errorMessage);
    deviceError_.store(true, std::memory_order_release);
    panicRequested_.store(true, std::memory_order_release);
}
} // namespace padflow
