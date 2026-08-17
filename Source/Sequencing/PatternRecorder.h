#pragma once

#include "Audio/AudioCommandQueue.h"
#include "Model/PadModel.h"
#include "Sequencing/Transport.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace padflow {
enum class PatternRecordInputType : std::uint8_t { noteOn, noteOff };
enum class PatternRecordMode : std::uint8_t { overdub, replace };

struct PatternRecordInput final {
    PatternRecordInputType type{PatternRecordInputType::noteOn};
    std::uint32_t padIndex{0U};
    std::uint64_t sourceId{0U};
    std::uint8_t velocity{100U};
    std::int64_t framePosition{0};
    std::uint64_t loopIteration{0U};
};

static_assert(std::is_trivially_copyable_v<PatternRecordInput>);

class PatternRecorder final {
  public:
    [[nodiscard]] juce::Result beginTake(const ProjectState& project, PatternRecordMode mode,
                                         std::uint32_t sampleRate);
    [[nodiscard]] bool capture(PatternRecordInput event, TransportState transportState) noexcept;
    [[nodiscard]] std::size_t drain();
    [[nodiscard]] juce::Result finishTake(std::int64_t stopFrame, Pattern& output);
    void cancel() noexcept;
    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] std::uint64_t overflowCount() const noexcept;

    [[nodiscard]] static juce::Result stepRecord(Pattern& pattern, const juce::String& padUuid,
                                                 std::uint16_t stablePadOrder,
                                                 std::uint8_t velocity, std::int64_t& cursorTicks,
                                                 std::uint64_t revision);

  private:
    [[nodiscard]] std::int64_t absoluteInputFrame(const PatternRecordInput& input) const noexcept;
    [[nodiscard]] std::int64_t frameToQ16(std::int64_t frame) const noexcept;
    [[nodiscard]] std::int64_t quantizeQ16(std::int64_t value) const noexcept;

    SpscQueue<PatternRecordInput, 512U> queue_;
    std::vector<PatternRecordInput> captured_;
    Pattern basePattern_;
    std::array<juce::String, totalPadCount> padUuids_;
    PatternRecordMode mode_{PatternRecordMode::overdub};
    std::int64_t microBpm_{defaultTempoMicroBpm};
    std::int64_t patternLengthFrames_{0};
    std::uint32_t sampleRate_{48'000U};
    std::atomic<bool> active_{false};
    std::atomic<std::uint64_t> overflows_{0U};
};
} // namespace padflow
