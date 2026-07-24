#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace padflow {
inline constexpr std::size_t padBankCount = 4U;
inline constexpr std::size_t padsPerBank = 16U;
inline constexpr std::size_t totalPadCount = padBankCount * padsPerBank;
inline constexpr std::size_t minimumLayersPerPad = 4U;

enum class PlaybackMode : std::uint8_t { oneShot, gate, toggle };
enum class PolyphonyMode : std::uint8_t { poly, mono };

struct EnvelopeParameters final {
    float attackSeconds{0.005F};
    float decaySeconds{0.1F};
    float sustainLevel{1.0F};
    float releaseSeconds{0.1F};

    [[nodiscard]] friend bool operator==(const EnvelopeParameters&,
                                         const EnvelopeParameters&) = default;
};

struct SampleLayer final {
    juce::String uuid;
    juce::String assetUuid;
    bool enabled{false};
    std::uint8_t velocityMinimum{1U};
    std::uint8_t velocityMaximum{127U};
    float gainDecibels{0.0F};
    float pan{0.0F};
    float tuningCents{0.0F};

    [[nodiscard]] friend bool operator==(const SampleLayer&, const SampleLayer&) = default;
};

struct PadParameters final {
    float gainDecibels{0.0F};
    float pan{0.0F};
    std::int16_t coarseSemitones{0};
    float fineCents{0.0F};
    PlaybackMode playbackMode{PlaybackMode::oneShot};
    PolyphonyMode polyphonyMode{PolyphonyMode::poly};
    std::uint8_t chokeGroup{0U};
    EnvelopeParameters envelope;
    std::uint16_t maximumVoices{8U};

    [[nodiscard]] friend bool operator==(const PadParameters&, const PadParameters&) = default;
};

struct Pad final {
    juce::String uuid;
    juce::String name;
    std::uint32_t colourArgb{0xff369b91U};
    PadParameters parameters;
    std::array<SampleLayer, minimumLayersPerPad> layers;
    std::uint8_t midiNote{36U};
    juce::String keyboardKey;

    [[nodiscard]] friend bool operator==(const Pad&, const Pad&) = default;
};

struct PadBank final {
    juce::String uuid;
    juce::String name;
    std::array<Pad, padsPerBank> pads;

    [[nodiscard]] friend bool operator==(const PadBank&, const PadBank&) = default;
};

struct ExternalAssetReference final {
    juce::String uuid;
    juce::String originalPath;
    juce::String originalName;
    juce::String format;
    juce::String contentFingerprint;
    std::uint64_t sourceFileBytes{0U};
    std::int64_t modificationTimeMilliseconds{0};
    std::uint32_t channels{0U};
    double sourceSampleRate{0.0};
    std::uint64_t frameCount{0U};
    std::uint64_t decodedBytes{0U};
    bool missing{false};

    [[nodiscard]] friend bool operator==(const ExternalAssetReference&,
                                         const ExternalAssetReference&) = default;
};

struct MidiSettings final {
    juce::String preferredInputIdentifier;
    std::uint8_t channelFilter{0U};

    [[nodiscard]] friend bool operator==(const MidiSettings&, const MidiSettings&) = default;
};

struct AudioSettings final {
    juce::String outputDeviceIdentifier;
    juce::String inputDeviceIdentifier;
    std::uint64_t outputChannelMask{0U};
    std::uint64_t inputChannelMask{0U};
    double sampleRate{0.0};
    std::uint32_t bufferSize{0U};

    [[nodiscard]] friend bool operator==(const AudioSettings&, const AudioSettings&) = default;
};

struct ProjectUiState final {
    std::uint8_t selectedBank{0U};
    std::uint8_t selectedPad{0U};
    std::uint8_t fixedTriggerVelocity{100U};
    float previewVolume{0.7F};
    int windowX{-1};
    int windowY{-1};
    int windowWidth{1180};
    int windowHeight{760};

    [[nodiscard]] friend bool operator==(const ProjectUiState&, const ProjectUiState&) = default;
};

struct ProjectState final {
    juce::String projectUuid;
    juce::String projectName{"Untitled"};
    std::array<PadBank, padBankCount> banks;
    std::vector<ExternalAssetReference> assets;
    MidiSettings midi;
    AudioSettings audio;
    ProjectUiState ui;

    [[nodiscard]] friend bool operator==(const ProjectState&, const ProjectState&) = default;
};

[[nodiscard]] juce::String makeStableUuid(const juce::String& seed);
[[nodiscard]] ProjectState makeDefaultProjectState(const juce::String& projectUuid,
                                                   juce::String projectName);
[[nodiscard]] Pad makeClearedPad(const ProjectState& project, std::size_t globalPadIndex);
void regeneratePadIdentity(Pad& pad);

[[nodiscard]] juce::Result validateLayer(const SampleLayer& layer);
[[nodiscard]] juce::Result validatePad(const Pad& pad);
[[nodiscard]] juce::Result validateProjectState(const ProjectState& state);
[[nodiscard]] std::size_t toGlobalPadIndex(std::size_t bankIndex, std::size_t padIndex) noexcept;
} // namespace padflow
