#pragma once

#include <juce_core/juce_core.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace padflow {
inline constexpr std::int64_t ppqTicksPerQuarterNote = 960;
inline constexpr std::uint32_t subTickUnitsPerTick = 65536U;
inline constexpr std::int64_t minimumTempoMicroBpm = 20'000'000;
inline constexpr std::int64_t maximumTempoMicroBpm = 300'000'000;
inline constexpr std::int64_t defaultTempoMicroBpm = 120'000'000;

struct MusicalTime final {
    std::int64_t wholePpqTicks{0};
    std::uint16_t fractionalTickQ16{0U};

    [[nodiscard]] friend constexpr bool operator==(const MusicalTime&,
                                                   const MusicalTime&) = default;
    [[nodiscard]] friend constexpr auto operator<=>(const MusicalTime&,
                                                    const MusicalTime&) = default;
};

struct MusicalDuration final {
    std::int64_t wholePpqTicks{0};
    std::uint16_t fractionalTickQ16{0U};

    [[nodiscard]] bool isPositive() const noexcept;
    [[nodiscard]] friend constexpr bool operator==(const MusicalDuration&,
                                                   const MusicalDuration&) = default;
};

struct MicroOffsetQ16 final {
    std::int32_t rawValue{0};

    [[nodiscard]] static constexpr MicroOffsetQ16
    fromWholeTicks(const std::int16_t ticks) noexcept {
        return {static_cast<std::int32_t>(ticks) * static_cast<std::int32_t>(subTickUnitsPerTick)};
    }

    [[nodiscard]] friend constexpr bool operator==(const MicroOffsetQ16&,
                                                   const MicroOffsetQ16&) = default;
};

struct TimeSignature final {
    std::uint8_t numerator{4U};
    std::uint8_t denominator{4U};

    [[nodiscard]] friend constexpr bool operator==(const TimeSignature&,
                                                   const TimeSignature&) = default;
};

struct BarBeatTick final {
    std::int64_t bar{0};
    std::uint8_t beat{0U};
    MusicalTime tick;

    [[nodiscard]] friend constexpr bool operator==(const BarBeatTick&,
                                                   const BarBeatTick&) = default;
};

struct TempoPoint final {
    MusicalTime position;
    std::int64_t microBpm{defaultTempoMicroBpm};

    [[nodiscard]] friend constexpr bool operator==(const TempoPoint&, const TempoPoint&) = default;
};

[[nodiscard]] bool isSupportedTimeSignature(TimeSignature signature) noexcept;
[[nodiscard]] std::int64_t ticksPerBeat(TimeSignature signature) noexcept;
[[nodiscard]] std::int64_t ticksPerBar(TimeSignature signature) noexcept;
[[nodiscard]] juce::Result musicalTimeToBarBeatTick(MusicalTime time, TimeSignature signature,
                                                    BarBeatTick& output) noexcept;
[[nodiscard]] juce::Result barBeatTickToMusicalTime(BarBeatTick value, TimeSignature signature,
                                                    MusicalTime& output) noexcept;
[[nodiscard]] juce::Result addMusicalDuration(MusicalTime start, MusicalDuration duration,
                                              MusicalTime& output) noexcept;
[[nodiscard]] juce::Result addMicroOffset(MusicalTime start, MicroOffsetQ16 offset,
                                          MusicalTime& output) noexcept;

class TempoMap final {
  public:
    TempoMap();

    [[nodiscard]] const std::vector<TempoPoint>& points() const noexcept;
    [[nodiscard]] juce::Result replacePoints(std::vector<TempoPoint> points);
    [[nodiscard]] juce::Result setTempo(MusicalTime position, std::int64_t microBpm);
    [[nodiscard]] std::int64_t tempoAt(MusicalTime position) const noexcept;
    [[nodiscard]] juce::Result absoluteFrameAt(MusicalTime position, std::uint32_t sampleRate,
                                               std::int64_t& outputFrame) const noexcept;

  private:
    std::vector<TempoPoint> points_;
};

[[nodiscard]] juce::Result musicalDurationToFrames(MusicalDuration duration, std::int64_t microBpm,
                                                   std::uint32_t sampleRate,
                                                   std::int64_t& outputFrames) noexcept;
[[nodiscard]] juce::Result absoluteFrameToBlockOffset(std::int64_t eventFrame,
                                                      std::int64_t blockStartFrame,
                                                      std::uint32_t blockSize,
                                                      std::int32_t& outputOffset) noexcept;
} // namespace padflow
