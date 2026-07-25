#pragma once

#include "Audio/AudioCommandQueue.h"
#include "Sampling/SampleAsset.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace padflow {
enum class PreviewCommandType : std::uint8_t { start, stop };

struct PreviewCommand final {
    PreviewCommandType type{PreviewCommandType::stop};
    std::uint64_t epoch{0U};
    const SampleAsset* asset{nullptr};
    std::uint64_t startFrame{0U};
    std::uint64_t endFrame{0U};
    bool reverse{false};
};

class PreviewPlayer final {
  public:
    void prepare(double outputSampleRate) noexcept;
    [[nodiscard]] bool publishAndStart(const SampleAsset* asset, std::uint64_t epoch) noexcept;
    [[nodiscard]] bool publishAndStartSlice(const SampleAsset* asset, std::uint64_t startFrame,
                                            std::uint64_t endFrame, bool reverse,
                                            std::uint64_t epoch) noexcept;
    [[nodiscard]] bool requestStop(std::uint64_t epoch) noexcept;
    void setVolume(float volume) noexcept;
    void processAdd(float* left, float* right, std::size_t frameCount) noexcept;
    void panicWhenQuiescent() noexcept;

    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] float volume() const noexcept;
    [[nodiscard]] std::uint64_t acknowledgedEpoch() const noexcept;
    [[nodiscard]] std::uint64_t sourceFramePosition() const noexcept;

  private:
    [[nodiscard]] static float interpolate(const SampleAssetView& asset, std::uint32_t channel,
                                           double position, std::uint64_t startFrame,
                                           std::uint64_t endFrame) noexcept;

    SpscQueue<PreviewCommand, 32U> commands_;
    std::atomic<float> volume_{0.7F};
    std::atomic<bool> activeMeter_{false};
    std::atomic<std::uint64_t> acknowledgedEpoch_{0U};
    std::atomic<std::uint64_t> sourceFramePosition_{0U};
    SampleAssetView activeAsset_;
    double position_{0.0};
    double increment_{1.0};
    double outputSampleRate_{48000.0};
    std::uint64_t startFrame_{0U};
    std::uint64_t endFrame_{0U};
    bool reverse_{false};
};

static_assert(std::is_trivially_copyable_v<PreviewCommand>);
} // namespace padflow
