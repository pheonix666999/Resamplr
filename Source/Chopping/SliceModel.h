#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace padflow {
inline constexpr std::int64_t maximumProvisionalSliceCount = 4096;
inline constexpr std::uint32_t sliceAlgorithmVersion = 1U;

enum class SliceAlgorithm : std::uint8_t { equal, fixedLength, transient, manual, lazy };
enum class SliceDisplayUnit : std::uint8_t { frames, milliseconds };
enum class SliceRemainderPolicy : std::uint8_t { include, discard };

struct SliceRegion final {
    juce::String uuid;
    std::int64_t startFrame{0};
    std::int64_t endFrame{0};
    juce::String name;
    std::optional<std::uint32_t> colourArgb;

    [[nodiscard]] friend bool operator==(const SliceRegion&, const SliceRegion&) = default;
};

struct SliceAlgorithmParameters final {
    std::int64_t sliceCount{1};
    std::int64_t fixedLengthFrames{0};
    SliceDisplayUnit displayUnit{SliceDisplayUnit::frames};
    SliceRemainderPolicy remainderPolicy{SliceRemainderPolicy::include};
    float transientSensitivity{0.5F};
    float transientThresholdFloor{0.0F};
    std::int64_t minimumSliceFrames{1};
    std::int64_t attackLookBackFrames{0};
    std::int64_t quantizeFrames{0};

    [[nodiscard]] friend bool operator==(const SliceAlgorithmParameters&,
                                         const SliceAlgorithmParameters&) = default;
};

struct SliceSet final {
    juce::String uuid;
    juce::String sourceAssetUuid;
    juce::String sourceFingerprint;
    juce::String sourceLayerUuid;
    std::int64_t sourceTrimStart{0};
    std::int64_t sourceTrimEnd{0};
    SliceAlgorithm algorithm{SliceAlgorithm::manual};
    std::uint32_t algorithmVersion{sliceAlgorithmVersion};
    std::vector<SliceRegion> slices;
    SliceAlgorithmParameters parameters;

    [[nodiscard]] friend bool operator==(const SliceSet&, const SliceSet&) = default;
};

struct SliceGenerationRequest final {
    juce::String setUuid;
    juce::String sourceAssetUuid;
    juce::String sourceFingerprint;
    juce::String sourceLayerUuid;
    std::int64_t trimStart{0};
    std::int64_t trimEnd{0};
    std::int64_t amount{1};
    SliceRemainderPolicy remainderPolicy{SliceRemainderPolicy::include};
    SliceDisplayUnit displayUnit{SliceDisplayUnit::frames};
};

[[nodiscard]] juce::Result validateSliceRegion(const SliceRegion& slice, std::int64_t trimStart,
                                               std::int64_t trimEnd);
[[nodiscard]] juce::Result validateSliceSet(const SliceSet& set, bool requireContiguous = true);
[[nodiscard]] juce::Result generateEqualSlices(const SliceGenerationRequest& request,
                                               SliceSet& output);
[[nodiscard]] juce::Result generateFixedLengthSlices(const SliceGenerationRequest& request,
                                                     SliceSet& output);
[[nodiscard]] juce::Result makeSliceSetFromBoundaries(const SliceGenerationRequest& request,
                                                      SliceAlgorithm algorithm,
                                                      const std::vector<std::int64_t>& boundaries,
                                                      SliceSet& output);
} // namespace padflow
