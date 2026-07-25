#include "MusicalTime.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <numeric>
#include <utility>

namespace padflow {
namespace {
constexpr std::uint64_t microsecondsPerMinute = 60'000'000U;

bool checkedAdd(const std::int64_t left, const std::int64_t right, std::int64_t& output) noexcept {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::lowest() - right))
        return false;
    output = left + right;
    return true;
}

bool checkedMultiply(const std::uint64_t left, const std::uint64_t right,
                     std::uint64_t& output) noexcept {
    if (right != 0U && left > std::numeric_limits<std::uint64_t>::max() / right)
        return false;
    output = left * right;
    return true;
}

juce::Result toQ16(const MusicalTime value, std::int64_t& output) noexcept {
    if (value.wholePpqTicks > std::numeric_limits<std::int64_t>::max() /
                                  static_cast<std::int64_t>(subTickUnitsPerTick) ||
        value.wholePpqTicks < std::numeric_limits<std::int64_t>::lowest() /
                                  static_cast<std::int64_t>(subTickUnitsPerTick))
        return juce::Result::fail("Musical position exceeds the supported Q16 range");
    const auto whole = value.wholePpqTicks * static_cast<std::int64_t>(subTickUnitsPerTick);
    if (!checkedAdd(whole, static_cast<std::int64_t>(value.fractionalTickQ16), output))
        return juce::Result::fail("Musical position exceeds the supported Q16 range");
    return juce::Result::ok();
}

juce::Result toQ16(const MusicalDuration value, std::int64_t& output) noexcept {
    return toQ16(MusicalTime{value.wholePpqTicks, value.fractionalTickQ16}, output);
}

MusicalTime fromQ16(const std::int64_t value) noexcept {
    auto whole = value / static_cast<std::int64_t>(subTickUnitsPerTick);
    auto remainder = value % static_cast<std::int64_t>(subTickUnitsPerTick);
    if (remainder < 0) {
        remainder += static_cast<std::int64_t>(subTickUnitsPerTick);
        --whole;
    }
    return {whole, static_cast<std::uint16_t>(remainder)};
}

bool validSampleRate(const std::uint32_t sampleRate) noexcept {
    return sampleRate == 44'100U || sampleRate == 48'000U || sampleRate == 88'200U ||
           sampleRate == 96'000U;
}

juce::Result roundedRationalFrames(const std::uint64_t durationQ16, const std::int64_t microBpm,
                                   const std::uint32_t sampleRate,
                                   std::int64_t& outputFrames) noexcept {
    if (microBpm < minimumTempoMicroBpm || microBpm > maximumTempoMicroBpm)
        return juce::Result::fail("Tempo must be between 20 and 300 BPM");
    if (!validSampleRate(sampleRate))
        return juce::Result::fail("Sample rate is unsupported by the deterministic scheduler");

    std::array<std::uint64_t, 3U> numerator{durationQ16, static_cast<std::uint64_t>(sampleRate),
                                            microsecondsPerMinute};
    std::array<std::uint64_t, 3U> denominator{static_cast<std::uint64_t>(microBpm),
                                              static_cast<std::uint64_t>(ppqTicksPerQuarterNote),
                                              static_cast<std::uint64_t>(subTickUnitsPerTick)};
    for (auto& numeratorFactor : numerator)
        for (auto& denominatorFactor : denominator) {
            const auto divisor = std::gcd(numeratorFactor, denominatorFactor);
            numeratorFactor /= divisor;
            denominatorFactor /= divisor;
        }

    std::uint64_t combinedNumerator = 1U;
    for (const auto factor : numerator)
        if (!checkedMultiply(combinedNumerator, factor, combinedNumerator))
            return juce::Result::fail("Musical duration exceeds the supported frame range");
    std::uint64_t combinedDenominator = 1U;
    for (const auto factor : denominator)
        if (!checkedMultiply(combinedDenominator, factor, combinedDenominator))
            return juce::Result::fail("Tempo conversion denominator overflowed");
    if (combinedDenominator == 0U)
        return juce::Result::fail("Tempo conversion denominator is zero");

    auto quotient = combinedNumerator / combinedDenominator;
    const auto remainder = combinedNumerator % combinedDenominator;
    const auto complement = combinedDenominator - remainder;
    const auto aboveHalf = remainder > complement;
    const auto exactHalf = remainder == complement;
    if (aboveHalf || (exactHalf && (quotient & 1U) != 0U)) {
        if (quotient == std::numeric_limits<std::uint64_t>::max())
            return juce::Result::fail("Rounded frame position overflowed");
        ++quotient;
    }
    if (quotient > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return juce::Result::fail("Musical duration exceeds the signed frame range");
    outputFrames = static_cast<std::int64_t>(quotient);
    return juce::Result::ok();
}

bool validTempoPoint(const TempoPoint& point) noexcept {
    return point.position.wholePpqTicks >= 0 && point.microBpm >= minimumTempoMicroBpm &&
           point.microBpm <= maximumTempoMicroBpm;
}
} // namespace

bool MusicalDuration::isPositive() const noexcept {
    return wholePpqTicks > 0 || (wholePpqTicks == 0 && fractionalTickQ16 > 0U);
}

bool isSupportedTimeSignature(const TimeSignature signature) noexcept {
    constexpr std::array<TimeSignature, 9U> supported{{
        {2U, 4U},
        {3U, 4U},
        {4U, 4U},
        {5U, 4U},
        {6U, 4U},
        {7U, 4U},
        {6U, 8U},
        {9U, 8U},
        {12U, 8U},
    }};
    return std::find(supported.begin(), supported.end(), signature) != supported.end();
}

std::int64_t ticksPerBeat(const TimeSignature signature) noexcept {
    if (!isSupportedTimeSignature(signature))
        return 0;
    return ppqTicksPerQuarterNote * 4 / static_cast<std::int64_t>(signature.denominator);
}

std::int64_t ticksPerBar(const TimeSignature signature) noexcept {
    return ticksPerBeat(signature) * static_cast<std::int64_t>(signature.numerator);
}

juce::Result musicalTimeToBarBeatTick(const MusicalTime time, const TimeSignature signature,
                                      BarBeatTick& output) noexcept {
    const auto beatTicks = ticksPerBeat(signature);
    const auto barTicks = ticksPerBar(signature);
    if (beatTicks <= 0 || time.wholePpqTicks < 0)
        return juce::Result::fail("Musical position or time signature is invalid");
    output.bar = time.wholePpqTicks / barTicks;
    const auto insideBar = time.wholePpqTicks % barTicks;
    output.beat = static_cast<std::uint8_t>(insideBar / beatTicks);
    output.tick = {insideBar % beatTicks, time.fractionalTickQ16};
    return juce::Result::ok();
}

juce::Result barBeatTickToMusicalTime(const BarBeatTick value, const TimeSignature signature,
                                      MusicalTime& output) noexcept {
    const auto beatTicks = ticksPerBeat(signature);
    const auto barTicks = ticksPerBar(signature);
    if (beatTicks <= 0 || value.bar < 0 || value.beat >= signature.numerator ||
        value.tick.wholePpqTicks < 0 || value.tick.wholePpqTicks >= beatTicks)
        return juce::Result::fail("Bar, beat, tick value is invalid");
    if (value.bar > std::numeric_limits<std::int64_t>::max() / barTicks)
        return juce::Result::fail("Bar position exceeds the supported musical range");
    const auto barStart = value.bar * barTicks;
    const auto beatStart = static_cast<std::int64_t>(value.beat) * beatTicks;
    std::int64_t combined = 0;
    if (!checkedAdd(barStart, beatStart, combined) ||
        !checkedAdd(combined, value.tick.wholePpqTicks, combined))
        return juce::Result::fail("Bar position exceeds the supported musical range");
    output = {combined, value.tick.fractionalTickQ16};
    return juce::Result::ok();
}

juce::Result addMusicalDuration(const MusicalTime start, const MusicalDuration duration,
                                MusicalTime& output) noexcept {
    if (!duration.isPositive())
        return juce::Result::fail("Musical duration must be positive");
    std::int64_t startQ16 = 0;
    std::int64_t durationQ16 = 0;
    if (const auto result = toQ16(start, startQ16); result.failed())
        return result;
    if (const auto result = toQ16(duration, durationQ16); result.failed())
        return result;
    std::int64_t combined = 0;
    if (!checkedAdd(startQ16, durationQ16, combined))
        return juce::Result::fail("Musical addition overflowed");
    output = fromQ16(combined);
    return juce::Result::ok();
}

juce::Result addMicroOffset(const MusicalTime start, const MicroOffsetQ16 offset,
                            MusicalTime& output) noexcept {
    std::int64_t startQ16 = 0;
    if (const auto result = toQ16(start, startQ16); result.failed())
        return result;
    std::int64_t combined = 0;
    if (!checkedAdd(startQ16, static_cast<std::int64_t>(offset.rawValue), combined))
        return juce::Result::fail("Musical nudge overflowed");
    output = fromQ16(combined);
    return juce::Result::ok();
}

TempoMap::TempoMap() : points_{{MusicalTime{}, defaultTempoMicroBpm}} {}

const std::vector<TempoPoint>& TempoMap::points() const noexcept {
    return points_;
}

juce::Result TempoMap::replacePoints(std::vector<TempoPoint> points) {
    if (points.empty() || points.front().position != MusicalTime{})
        return juce::Result::fail("Tempo map requires a point at tick zero");
    for (std::size_t index = 0U; index < points.size(); ++index) {
        if (!validTempoPoint(points[index]))
            return juce::Result::fail("Tempo point is invalid");
        if (index > 0U && !(points[index - 1U].position < points[index].position))
            return juce::Result::fail("Tempo points must have unique increasing positions");
    }
    points_ = std::move(points);
    return juce::Result::ok();
}

juce::Result TempoMap::setTempo(const MusicalTime position, const std::int64_t microBpm) {
    if (!validTempoPoint({position, microBpm}))
        return juce::Result::fail("Tempo point is invalid");
    auto replacement = points_;
    const auto existing = std::lower_bound(
        replacement.begin(), replacement.end(), position,
        [](const TempoPoint& point, const MusicalTime value) { return point.position < value; });
    if (existing != replacement.end() && existing->position == position)
        existing->microBpm = microBpm;
    else
        replacement.insert(existing, TempoPoint{position, microBpm});
    return replacePoints(std::move(replacement));
}

std::int64_t TempoMap::tempoAt(const MusicalTime position) const noexcept {
    const auto following = std::upper_bound(
        points_.begin(), points_.end(), position,
        [](const MusicalTime value, const TempoPoint& point) { return value < point.position; });
    return following == points_.begin() ? points_.front().microBpm : std::prev(following)->microBpm;
}

juce::Result TempoMap::absoluteFrameAt(const MusicalTime position, const std::uint32_t sampleRate,
                                       std::int64_t& outputFrame) const noexcept {
    if (position < MusicalTime{})
        return juce::Result::fail("Absolute musical position must not be negative");
    std::int64_t accumulatedFrames = 0;
    auto segmentStart = MusicalTime{};
    auto segmentTempo = points_.front().microBpm;
    for (std::size_t index = 1U; index <= points_.size(); ++index) {
        const auto segmentEnd = index < points_.size() && points_[index].position < position
                                    ? points_[index].position
                                    : position;
        std::int64_t startQ16 = 0;
        std::int64_t endQ16 = 0;
        if (const auto result = toQ16(segmentStart, startQ16); result.failed())
            return result;
        if (const auto result = toQ16(segmentEnd, endQ16); result.failed())
            return result;
        if (endQ16 > startQ16) {
            std::int64_t segmentFrames = 0;
            const auto durationQ16 = static_cast<std::uint64_t>(endQ16 - startQ16);
            if (const auto result =
                    roundedRationalFrames(durationQ16, segmentTempo, sampleRate, segmentFrames);
                result.failed())
                return result;
            if (!checkedAdd(accumulatedFrames, segmentFrames, accumulatedFrames))
                return juce::Result::fail("Absolute frame position overflowed");
        }
        if (segmentEnd == position)
            break;
        segmentStart = points_[index].position;
        segmentTempo = points_[index].microBpm;
    }
    outputFrame = accumulatedFrames;
    return juce::Result::ok();
}

juce::Result musicalDurationToFrames(const MusicalDuration duration, const std::int64_t microBpm,
                                     const std::uint32_t sampleRate,
                                     std::int64_t& outputFrames) noexcept {
    if (!duration.isPositive())
        return juce::Result::fail("Musical duration must be positive");
    std::int64_t durationQ16 = 0;
    if (const auto result = toQ16(duration, durationQ16); result.failed())
        return result;
    return roundedRationalFrames(static_cast<std::uint64_t>(durationQ16), microBpm, sampleRate,
                                 outputFrames);
}

juce::Result absoluteFrameToBlockOffset(const std::int64_t eventFrame,
                                        const std::int64_t blockStartFrame,
                                        const std::uint32_t blockSize,
                                        std::int32_t& outputOffset) noexcept {
    std::int64_t offset = 0;
    if (!checkedAdd(eventFrame, -blockStartFrame, offset))
        return juce::Result::fail("Block offset overflowed");
    if (offset < 0 || offset >= static_cast<std::int64_t>(blockSize))
        return juce::Result::fail("Event frame is outside the half-open audio block");
    outputOffset = static_cast<std::int32_t>(offset);
    return juce::Result::ok();
}
} // namespace padflow
