#pragma once

#include "Audio/AudioCommandQueue.h"
#include "Sequencing/PatternModel.h"
#include "Sequencing/Probability.h"
#include "Sequencing/Transport.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace padflow {
inline constexpr std::size_t schedulerCommandCapacity = 4096U;

struct SchedulerPulse final {
    ProbabilityIdentity probabilityIdentity;
    ProbabilityQ32 probability{probabilityQ32Maximum};
    std::int64_t loopingTriggerFrame{0};
    std::int64_t loopingReleaseFrame{0};
    std::int64_t nonLoopingTriggerFrame{-1};
    std::int64_t nonLoopingReleaseFrame{-1};
    std::int16_t triggerLoopDelta{0};
    std::int16_t releaseLoopDelta{0};
    std::uint32_t padIndex{0U};
    std::uint32_t sourceToken{0U};
    std::uint32_t sequenceOrder{0U};
    float velocity{100.0F};
};

struct SchedulerSnapshot final {
    std::vector<SchedulerPulse> pulses;
    std::int64_t patternLengthFrames{0};
    std::uint64_t generation{0U};
};

struct ScheduledCommandBuffer final {
    std::array<AudioCommand, schedulerCommandCapacity> commands{};
    std::size_t size{0U};
    std::uint64_t dropped{0U};

    void clear() noexcept;
    [[nodiscard]] std::span<const AudioCommand> view() const noexcept;
};

[[nodiscard]] juce::Result makeSchedulerSnapshot(const Pattern& pattern, const TempoMap& tempoMap,
                                                 const juce::String& projectProbabilitySeed,
                                                 std::uint32_t sampleRate, std::uint64_t generation,
                                                 SchedulerSnapshot& output);

class PatternScheduler final {
  public:
    void publishSnapshot(const SchedulerSnapshot* snapshot) noexcept;
    void processBlock(const TransportPositionSnapshot& transport, std::size_t frameCount,
                      ScheduledCommandBuffer& output) noexcept;
    [[nodiscard]] std::uint64_t overflowCount() const noexcept;
    [[nodiscard]] std::uint64_t acknowledgedSnapshotGeneration() const noexcept;

  private:
    std::atomic<const SchedulerSnapshot*> snapshot_{nullptr};
    std::atomic<std::uint64_t> overflows_{0U};
    std::atomic<std::uint64_t> acknowledgedGeneration_{0U};
};
} // namespace padflow
