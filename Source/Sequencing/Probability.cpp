#include "Probability.h"

#include <algorithm>
#include <array>
#include <bit>

namespace padflow {
namespace {
constexpr std::size_t probabilityPayloadSize = 84U;

std::uint8_t hexValue(const juce::juce_wchar character) noexcept {
    if (character >= '0' && character <= '9')
        return static_cast<std::uint8_t>(character - '0');
    if (character >= 'a' && character <= 'f')
        return static_cast<std::uint8_t>(character - 'a' + 10);
    if (character >= 'A' && character <= 'F')
        return static_cast<std::uint8_t>(character - 'A' + 10);
    return 0xFFU;
}

std::uint64_t loadLittleEndian64(const std::uint8_t* const bytes) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index)
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    return value;
}

void storeLittleEndian64(std::array<std::uint8_t, probabilityPayloadSize>& payload,
                         const std::size_t offset, const std::uint64_t value) noexcept {
    for (std::size_t index = 0U; index < 8U; ++index)
        payload[offset + index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & std::uint64_t{0xFFU});
}

void sipRound(std::uint64_t& v0, std::uint64_t& v1, std::uint64_t& v2, std::uint64_t& v3) noexcept {
    v0 += v1;
    v1 = std::rotl(v1, 13);
    v1 ^= v0;
    v0 = std::rotl(v0, 32);
    v2 += v3;
    v3 = std::rotl(v3, 16);
    v3 ^= v2;
    v0 += v3;
    v3 = std::rotl(v3, 21);
    v3 ^= v0;
    v2 += v1;
    v1 = std::rotl(v1, 17);
    v1 ^= v2;
    v2 = std::rotl(v2, 32);
}
} // namespace

juce::Result parseCanonicalUuid(const juce::String& text,
                                std::array<std::uint8_t, 16U>& output) noexcept {
    std::array<std::uint8_t, 32U> nibbles{};
    std::size_t nibbleCount = 0U;
    for (const auto character : text) {
        if (character == '-')
            continue;
        const auto value = hexValue(character);
        if (value == 0xFFU || nibbleCount >= nibbles.size())
            return juce::Result::fail("Probability identity must use canonical UUID hex");
        nibbles[nibbleCount++] = value;
    }
    if (nibbleCount != nibbles.size())
        return juce::Result::fail("Probability identity must contain exactly 128 bits");
    for (std::size_t index = 0U; index < output.size(); ++index)
        output[index] =
            static_cast<std::uint8_t>((nibbles[index * 2U] << 4U) | nibbles[index * 2U + 1U]);
    return juce::Result::ok();
}

juce::Result makeProbabilityIdentity(const juce::String& projectSeed,
                                     const juce::String& patternUuid, const juce::String& eventUuid,
                                     ProbabilityIdentity& output) noexcept {
    ProbabilityIdentity candidate;
    if (const auto result = parseCanonicalUuid(projectSeed, candidate.projectSeed); result.failed())
        return result;
    if (const auto result = parseCanonicalUuid(patternUuid, candidate.patternUuid); result.failed())
        return result;
    if (const auto result = parseCanonicalUuid(eventUuid, candidate.eventUuid); result.failed())
        return result;
    output = candidate;
    return juce::Result::ok();
}

std::uint64_t sipHash24(const std::array<std::uint8_t, 16U>& key,
                        const std::span<const std::uint8_t> message) noexcept {
    const auto k0 = loadLittleEndian64(key.data());
    const auto k1 = loadLittleEndian64(key.data() + 8U);
    auto v0 = std::uint64_t{0x736f6d6570736575U} ^ k0;
    auto v1 = std::uint64_t{0x646f72616e646f6dU} ^ k1;
    auto v2 = std::uint64_t{0x6c7967656e657261U} ^ k0;
    auto v3 = std::uint64_t{0x7465646279746573U} ^ k1;

    const auto completeBytes = message.size() - message.size() % 8U;
    for (std::size_t offset = 0U; offset < completeBytes; offset += 8U) {
        const auto word = loadLittleEndian64(message.data() + offset);
        v3 ^= word;
        sipRound(v0, v1, v2, v3);
        sipRound(v0, v1, v2, v3);
        v0 ^= word;
    }

    auto tail = static_cast<std::uint64_t>(message.size()) << 56U;
    for (std::size_t index = 0U; index < message.size() - completeBytes; ++index)
        tail |= static_cast<std::uint64_t>(message[completeBytes + index]) << (index * 8U);
    v3 ^= tail;
    sipRound(v0, v1, v2, v3);
    sipRound(v0, v1, v2, v3);
    v0 ^= tail;
    v2 ^= 0xFFU;
    for (std::size_t round = 0U; round < 4U; ++round)
        sipRound(v0, v1, v2, v3);
    return v0 ^ v1 ^ v2 ^ v3;
}

std::uint32_t probabilityDrawQ32(const ProbabilityContext& context) noexcept {
    std::array<std::uint8_t, probabilityPayloadSize> payload{};
    std::copy(context.identity.projectSeed.begin(), context.identity.projectSeed.end(),
              payload.begin());
    std::copy(context.identity.patternUuid.begin(), context.identity.patternUuid.end(),
              payload.begin() + 16);
    std::copy(context.identity.eventUuid.begin(), context.identity.eventUuid.end(),
              payload.begin() + 32);
    storeLittleEndian64(payload, 64U, context.patternLoopIteration);
    payload[80U] = static_cast<std::uint8_t>(probabilityAlgorithmVersion & 0xFFU);
    payload[81U] = static_cast<std::uint8_t>((probabilityAlgorithmVersion >> 8U) & 0xFFU);
    payload[82U] = static_cast<std::uint8_t>((probabilityAlgorithmVersion >> 16U) & 0xFFU);
    payload[83U] = static_cast<std::uint8_t>((probabilityAlgorithmVersion >> 24U) & 0xFFU);
    return static_cast<std::uint32_t>(sipHash24(context.identity.projectSeed, payload) >> 32U);
}

bool probabilityAccepts(const ProbabilityQ32 probability,
                        const ProbabilityContext& context) noexcept {
    if (probability == 0U)
        return false;
    if (probability >= probabilityQ32Maximum)
        return true;
    return static_cast<ProbabilityQ32>(probabilityDrawQ32(context)) < probability;
}
} // namespace padflow
