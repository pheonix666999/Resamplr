#pragma once

#include "Sequencing/PatternModel.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace padflow {
inline constexpr std::uint32_t probabilityAlgorithmVersion = 1U;
inline constexpr auto probabilityAlgorithmIdentifier = "siphash24-v1";

struct ProbabilityIdentity final {
    std::array<std::uint8_t, 16U> projectSeed{};
    std::array<std::uint8_t, 16U> patternUuid{};
    std::array<std::uint8_t, 16U> eventUuid{};

    [[nodiscard]] friend constexpr bool operator==(const ProbabilityIdentity&,
                                                   const ProbabilityIdentity&) = default;
};

struct ProbabilityContext final {
    ProbabilityIdentity identity;
    std::uint64_t patternLoopIteration{0U};

    [[nodiscard]] friend constexpr bool operator==(const ProbabilityContext&,
                                                   const ProbabilityContext&) = default;
};

[[nodiscard]] juce::Result parseCanonicalUuid(const juce::String& text,
                                              std::array<std::uint8_t, 16U>& output) noexcept;
[[nodiscard]] juce::Result makeProbabilityIdentity(const juce::String& projectSeed,
                                                   const juce::String& patternUuid,
                                                   const juce::String& eventUuid,
                                                   ProbabilityIdentity& output) noexcept;
[[nodiscard]] std::uint64_t sipHash24(const std::array<std::uint8_t, 16U>& key,
                                      std::span<const std::uint8_t> message) noexcept;
[[nodiscard]] std::uint32_t probabilityDrawQ32(const ProbabilityContext& context) noexcept;
[[nodiscard]] bool probabilityAccepts(ProbabilityQ32 probability,
                                      const ProbabilityContext& context) noexcept;

static_assert(std::is_trivially_copyable_v<ProbabilityIdentity>);
static_assert(std::is_trivially_copyable_v<ProbabilityContext>);
} // namespace padflow
