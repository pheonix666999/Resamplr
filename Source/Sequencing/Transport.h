#pragma once

#include "Audio/AudioCommandQueue.h"
#include "Model/MusicalTime.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace padflow {
enum class TransportState : std::uint8_t { stopped, playing, recording, countIn };
enum class CountInBars : std::uint8_t { off, one, two };

struct TransportConfiguration final {
    static constexpr std::size_t maximumBeatCount = 1536U;

    std::uint64_t generation{0U};
    std::int64_t patternLengthFrames{0};
    std::int64_t firstBarLengthFrames{0};
    std::uint32_t sampleRate{48'000U};
    TimeSignature timeSignature;
    std::array<std::int64_t, maximumBeatCount> beatFrames{};
    std::array<bool, maximumBeatCount> accentedBeats{};
    std::size_t beatCount{0U};
    float metronomeVolume{0.5F};
};

[[nodiscard]] juce::Result
makeTransportConfiguration(const TempoMap& tempoMap, TimeSignature timeSignature,
                           MusicalTime patternLength, std::uint32_t sampleRate,
                           std::uint64_t generation, float metronomeVolume,
                           TransportConfiguration& output);

enum class TransportCommandType : std::uint8_t {
    play,
    stop,
    record,
    returnToStart,
    setPositionFrames,
    setLoopEnabled,
    setMetronomeEnabled,
    setCountInBars,
    panic
};

struct TransportCommand final {
    TransportCommandType type{TransportCommandType::stop};
    std::int64_t value{0};
};

static_assert(std::is_trivially_copyable_v<TransportCommand>);

struct TransportPositionSnapshot final {
    TransportState state{TransportState::stopped};
    std::int64_t framePosition{0};
    std::uint64_t loopIteration{0U};
    std::uint64_t configurationGeneration{0U};
    bool loopEnabled{true};
    bool metronomeEnabled{false};
};

class TapTempoEstimator final {
  public:
    [[nodiscard]] std::int64_t tap(std::int64_t timestampMilliseconds) noexcept;
    void reset() noexcept;

  private:
    std::array<std::int64_t, 8U> timestamps_{};
    std::size_t count_{0U};
};

class TransportEngine final {
  public:
    [[nodiscard]] bool publishConfiguration(const TransportConfiguration* configuration) noexcept;
    [[nodiscard]] bool enqueue(const TransportCommand& command) noexcept;
    [[nodiscard]] bool play() noexcept;
    [[nodiscard]] bool stop() noexcept;
    [[nodiscard]] bool record() noexcept;
    [[nodiscard]] bool returnToStart() noexcept;
    [[nodiscard]] bool setPositionFrames(std::int64_t position) noexcept;
    [[nodiscard]] bool setLoopEnabled(bool enabled) noexcept;
    [[nodiscard]] bool setMetronomeEnabled(bool enabled) noexcept;
    [[nodiscard]] bool setCountInBars(CountInBars bars) noexcept;
    [[nodiscard]] bool panic() noexcept;

    void beginBlock() noexcept;
    void processMetronomeAdd(float* left, float* right, std::size_t frameCount) noexcept;
    [[nodiscard]] bool consumePanicRequest() noexcept;
    void stopAndPanicWhenQuiescent() noexcept;

    [[nodiscard]] TransportPositionSnapshot snapshot() const noexcept;
    [[nodiscard]] std::uint64_t commandOverflowCount() const noexcept;

  private:
    void applyCommand(const TransportCommand& command) noexcept;
    void beginPlayback(bool recording) noexcept;
    void resetBeatCursor() noexcept;
    void startClick(bool accent) noexcept;
    void publishSnapshot() noexcept;

    SpscQueue<TransportCommand, 128U> commands_;
    std::atomic<const TransportConfiguration*> configuration_{nullptr};
    const TransportConfiguration* activeConfiguration_{nullptr};
    TransportState state_{TransportState::stopped};
    CountInBars countInBars_{CountInBars::off};
    std::int64_t framePosition_{0};
    std::int64_t countInFramesRemaining_{0};
    std::uint64_t loopIteration_{0U};
    std::size_t nextBeatIndex_{0U};
    std::uint32_t clickFramesRemaining_{0U};
    double clickPhase_{0.0};
    double clickPhaseStep_{0.0};
    float clickAmplitude_{0.0F};
    bool loopEnabled_{true};
    bool metronomeEnabled_{false};
    bool panicRequested_{false};

    std::atomic<std::uint8_t> publishedState_{static_cast<std::uint8_t>(TransportState::stopped)};
    std::atomic<std::int64_t> publishedFramePosition_{0};
    std::atomic<std::uint64_t> publishedLoopIteration_{0U};
    std::atomic<std::uint64_t> publishedConfigurationGeneration_{0U};
    std::atomic<bool> publishedLoopEnabled_{true};
    std::atomic<bool> publishedMetronomeEnabled_{false};
    std::atomic<std::uint64_t> commandOverflows_{0U};
};
} // namespace padflow
