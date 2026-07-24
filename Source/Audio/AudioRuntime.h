#pragma once

#include "Audio/PlaybackEngine.h"
#include "Model/PadModel.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace padflow {
struct AudioOutputDeviceInfo final {
    juce::String type;
    juce::String name;
};

struct AudioRuntimeStatus final {
    bool deviceOpen{false};
    juce::String deviceName;
    double sampleRate{0.0};
    std::uint32_t bufferSize{0U};
    double cpuUsage{0.0};
    std::uint64_t dropoutCount{0U};
};

class AudioRuntime final : public juce::AudioIODeviceCallback {
  public:
    AudioRuntime() = default;
    ~AudioRuntime() override;

    [[nodiscard]] juce::Result initialise(const AudioSettings& preferred);
    [[nodiscard]] juce::Result applySettings(const AudioSettings& settings);
    void close();
    [[nodiscard]] juce::Result restart();

    [[nodiscard]] std::vector<AudioOutputDeviceInfo> outputDevices();
    [[nodiscard]] static std::vector<juce::MidiDeviceInfo> midiInputDevices();
    void setMidiInputEnabled(const juce::String& identifier, bool enabled,
                             juce::MidiInputCallback* callback);
    [[nodiscard]] AudioSettings currentSettings() const;
    [[nodiscard]] AudioRuntimeStatus status();
    void resetDropoutCount() noexcept;
    void setTestToneEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isTestToneEnabled() const noexcept;

    [[nodiscard]] PlaybackEngine& engine() noexcept;
    [[nodiscard]] const PlaybackEngine& engine() const noexcept;

    void
    audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                     float* const* outputChannelData, int numOutputChannels,
                                     int numSamples,
                                     const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& errorMessage) override;

  private:
    static constexpr std::size_t scratchFrames = 8192U;

    juce::AudioDeviceManager manager_;
    PlaybackEngine engine_;
    std::array<float, scratchFrames> leftScratch_{};
    std::array<float, scratchFrames> rightScratch_{};
    std::atomic<bool> callbackRegistered_{false};
    std::atomic<bool> testToneEnabled_{false};
    std::atomic<bool> deviceError_{false};
    std::atomic<bool> panicRequested_{false};
    std::atomic<std::uint64_t> dropoutBaseline_{0U};
    juce::String activeMidiIdentifier_;
    juce::MidiInputCallback* activeMidiCallback_{nullptr};
    double testTonePhase_{0.0};
    double activeSampleRate_{48000.0};
};
} // namespace padflow
