#pragma once

#include "App/ApplicationController.h"
#include "Sampling/SampleAsset.h"
#include "Utilities/BackgroundJobSystem.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace padflow {
inline constexpr std::uint32_t derivedAssetAlgorithmVersion = 1U;

enum class DerivedAssetOperation : std::uint8_t { normalize, stereoToMono, fadeIn, fadeOut, crop };

struct DerivedAssetRecipe final {
    juce::String sourceAssetUuid;
    juce::String sourceFingerprint;
    DerivedAssetOperation operation{DerivedAssetOperation::normalize};
    std::uint32_t algorithmVersion{derivedAssetAlgorithmVersion};
    juce::String canonicalParameters;
    juce::String derivedAssetUuid;

    [[nodiscard]] friend bool operator==(const DerivedAssetRecipe&,
                                         const DerivedAssetRecipe&) = default;
};

struct DerivedAssetRequest final {
    JobSpec target;
    std::shared_ptr<const SampleAsset> source;
    ExternalAssetReference sourceReference;
    SamplePlaybackSettings playback;
    DerivedAssetOperation operation{DerivedAssetOperation::normalize};
    float normalizeTargetDecibels{-1.0F};
    std::uint64_t fadeDurationFrames{0U};
    juce::File outputDirectory;
    juce::String projectOwnedRelativeDirectory{"Derived"};
    std::size_t globalPadIndex{0U};
    std::size_t layerIndex{0U};
};

struct DerivedAssetPayload final {
    std::shared_ptr<const SampleAsset> asset;
    ExternalAssetReference reference;
    DerivedAssetRecord provenance;
    SamplePlaybackSettings playback;
    juce::String expectedSourceAssetUuid;
    std::size_t globalPadIndex{0U};
    std::size_t layerIndex{0U};
    bool wroteNewFile{false};
};

class DerivedAssetRenderer final {
  public:
    [[nodiscard]] static DerivedAssetRecipe recipeFor(const DerivedAssetRequest& request);
    [[nodiscard]] static juce::String findReusableAssetUuid(const ProjectState& state,
                                                            const DerivedAssetRecipe& recipe);
    [[nodiscard]] static std::optional<JobHandle> submit(BackgroundJobSystem& jobs,
                                                         DerivedAssetRequest request);
    [[nodiscard]] static std::shared_ptr<const JobResult>
    render(const DerivedAssetRequest& request, const CancellationToken& cancellation,
           JobProgress& progress);
    [[nodiscard]] static juce::Result commit(const JobResult& result,
                                             ApplicationController& controller,
                                             SampleAssetRegistry& registry);
};
} // namespace padflow
