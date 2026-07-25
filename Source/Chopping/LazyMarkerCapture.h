#pragma once

#include "Audio/AudioCommandQueue.h"
#include "Chopping/ChoppingSession.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace padflow {
enum class LazyMarkerSource : std::uint8_t { mouse, keyboard, midi, control };

struct LazyMarkerEvent final {
    std::int64_t sourceFrame{0};
    LazyMarkerSource source{LazyMarkerSource::control};
};
static_assert(std::is_trivially_copyable_v<LazyMarkerEvent>);

struct LazyCaptureSettings final {
    std::int64_t trimStart{0};
    std::int64_t trimEnd{0};
    std::int64_t minimumSliceFrames{1};
    std::int64_t quantizeFrames{0};
};

struct LazyDrainResult final {
    std::size_t accepted{0U};
    std::size_t rejected{0U};
    std::uint64_t overflowCount{0U};
};

class LazyMarkerCapture final {
  public:
    [[nodiscard]] juce::Result start(LazyCaptureSettings settings);
    void stop() noexcept;
    void resetWhenQuiescent() noexcept;

    [[nodiscard]] bool captureFromAudioThread(std::int64_t sourceFrame,
                                              LazyMarkerSource source) noexcept;
    [[nodiscard]] LazyDrainResult drainToSession(ChoppingSession& session);

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::size_t queuedEventCount() const noexcept;
    [[nodiscard]] std::uint64_t overflowCount() const noexcept;

  private:
    [[nodiscard]] std::int64_t quantizeAndClamp(std::int64_t sourceFrame) const noexcept;

    SpscQueue<LazyMarkerEvent, 256U> queue_;
    std::atomic<bool> active_{false};
    std::atomic<std::int64_t> trimStart_{0};
    std::atomic<std::int64_t> trimEnd_{0};
    std::atomic<std::int64_t> minimumSliceFrames_{1};
    std::atomic<std::int64_t> quantizeFrames_{0};
    std::atomic<std::uint64_t> overflowCount_{0U};
};
} // namespace padflow
