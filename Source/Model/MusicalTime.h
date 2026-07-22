#pragma once

#include <cstdint>

namespace padflow {
inline constexpr std::int64_t ppqTicksPerQuarterNote = 960;
inline constexpr std::uint32_t subTickUnitsPerTick = 65536U;

struct MusicalTime final {
    std::int64_t wholePpqTicks{0};
    std::uint16_t fractionalTickQ16{0U};

    [[nodiscard]] friend constexpr bool operator==(const MusicalTime&,
                                                   const MusicalTime&) = default;
};

struct MicroOffsetQ16 final {
    std::int32_t rawValue{0};

    [[nodiscard]] static constexpr MicroOffsetQ16
    fromWholeTicks(const std::int16_t ticks) noexcept {
        return {static_cast<std::int32_t>(ticks) * static_cast<std::int32_t>(subTickUnitsPerTick)};
    }
};
} // namespace padflow
