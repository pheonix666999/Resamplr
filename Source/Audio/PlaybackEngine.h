#pragma once

#include "Audio/AudioCommandQueue.h"
#include "Model/PadModel.h"
#include "Sampling/SampleAsset.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace padflow {
struct PlaybackLayerSnapshot final {
    SampleAssetView asset;
    bool enabled{false};
    std::uint8_t velocityMinimum{1U};
    std::uint8_t velocityMaximum{127U};
    float gainLinear{1.0F};
    float pan{-0.0F};
    float tuningCents{0.0F};
    std::uint64_t startFrame{0U};
    std::uint64_t endFrame{0U};
    std::uint64_t loopStartFrame{0U};
    std::uint64_t loopEndFrame{0U};
    bool loopEnabled{false};
    bool reverseEnabled{false};
};

struct PlaybackPadSnapshot final {
    std::array<PlaybackLayerSnapshot, minimumLayersPerPad> layers;
    PlaybackMode playbackMode{PlaybackMode::oneShot};
    PolyphonyMode polyphonyMode{PolyphonyMode::poly};
    std::uint8_t chokeGroup{0U};
    std::uint16_t maximumVoices{8U};
    float gainLinear{1.0F};
    float pan{0.0F};
    float coarseSemitones{0.0F};
    float fineCents{0.0F};
    EnvelopeParameters envelope;
};

struct PlaybackSnapshot final {
    std::array<PlaybackPadSnapshot, totalPadCount> pads;
    std::uint64_t generation{0U};
};

struct PlaybackMetrics final {
    std::uint32_t activeVoices{0U};
    float peakLeft{0.0F};
    float peakRight{0.0F};
    std::uint64_t renderedBlocks{0U};
};

class PlaybackEngine final {
  public:
    static constexpr std::size_t voiceCount = 128U;

    void prepare(double outputSampleRate) noexcept;
    void publishSnapshot(const PlaybackSnapshot* snapshot) noexcept;
    [[nodiscard]] bool enqueue(const AudioCommand& command) noexcept;
    void processBlock(float* left, float* right, std::size_t frameCount) noexcept;
    void panic() noexcept;

    [[nodiscard]] PlaybackMetrics metrics() const noexcept;
    [[nodiscard]] std::size_t activeVoiceCount() const noexcept;
    [[nodiscard]] int lastAllocatedVoiceIndex() const noexcept;
    [[nodiscard]] std::uint64_t acknowledgedSnapshotGeneration() const noexcept;

  private:
    enum class EnvelopeStage : std::uint8_t { inactive, attack, decay, sustain, release };

    struct Voice final {
        SampleAssetView asset;
        std::uint32_t padIndex{0U};
        std::uint32_t sourceId{0U};
        std::uint8_t chokeGroup{0U};
        PlaybackMode playbackMode{PlaybackMode::oneShot};
        EnvelopeStage stage{EnvelopeStage::inactive};
        double position{0.0};
        double increment{1.0};
        std::uint64_t startFrame{0U};
        std::uint64_t endFrame{0U};
        std::uint64_t loopStartFrame{0U};
        std::uint64_t loopEndFrame{0U};
        bool loopEnabled{false};
        float envelope{0.0F};
        float sustain{1.0F};
        float attackStep{1.0F};
        float decayStep{0.0F};
        float releaseStep{1.0F};
        float leftGain{1.0F};
        float rightGain{1.0F};
        std::uint64_t triggerAge{0U};
        std::uint64_t snapshotGeneration{0U};
    };

    void handleCommand(const AudioCommand& command) noexcept;
    void trigger(std::uint32_t padIndex, std::uint32_t sourceId, float velocity) noexcept;
    void release(std::uint32_t sourceId) noexcept;
    void releaseVoice(Voice& voice) noexcept;
    [[nodiscard]] std::size_t allocateVoice(std::uint32_t padIndex) noexcept;
    [[nodiscard]] static float interpolate(const SampleAssetView& asset, std::uint32_t channel,
                                           double position) noexcept;
    static void advancePosition(Voice& voice) noexcept;
    [[nodiscard]] float advanceEnvelope(Voice& voice) const noexcept;

    std::array<Voice, voiceCount> voices_{};
    AudioCommandQueue commands_;
    std::atomic<const PlaybackSnapshot*> snapshot_{nullptr};
    double outputSampleRate_{48000.0};
    std::uint64_t nextTriggerAge_{1U};
    std::atomic<std::uint32_t> activeVoices_{0U};
    std::atomic<float> peakLeft_{0.0F};
    std::atomic<float> peakRight_{0.0F};
    std::atomic<std::uint64_t> renderedBlocks_{0U};
    std::atomic<int> lastAllocatedVoice_{-1};
    std::atomic<std::uint64_t> reclaimableSnapshotGeneration_{0U};
};

[[nodiscard]] PlaybackSnapshot makePlaybackSnapshot(const ProjectState& project,
                                                    const SampleAssetRegistry& assets);
} // namespace padflow
