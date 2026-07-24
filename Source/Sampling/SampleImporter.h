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
struct SampleImportRequest final {
    JobSpec target;
    juce::File sourceFile;
    juce::String assetUuid;
    std::size_t globalPadIndex{0U};
    std::size_t layerIndex{0U};
    std::uint64_t maximumDecodedBytes{defaultMilestone1DecodedBudgetBytes};
};

struct SampleImportPayload final {
    std::shared_ptr<const SampleAsset> asset;
    ExternalAssetReference reference;
    std::size_t globalPadIndex{0U};
    std::size_t layerIndex{0U};
};

class SampleImporter final {
  public:
    [[nodiscard]] static std::optional<JobHandle> submit(BackgroundJobSystem& jobs,
                                                         SampleImportRequest request);
    [[nodiscard]] static std::shared_ptr<const JobResult>
    decode(const SampleImportRequest& request, const CancellationToken& cancellation,
           JobProgress& progress);
    [[nodiscard]] static juce::Result commit(const JobResult& result,
                                             ApplicationController& controller,
                                             SampleAssetRegistry& registry);
};
} // namespace padflow
