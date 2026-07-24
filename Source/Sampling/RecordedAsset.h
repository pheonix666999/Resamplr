#pragma once

#include "App/ApplicationController.h"
#include "Sampling/SampleAsset.h"
#include "Utilities/BackgroundJobSystem.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace padflow {
struct RecordedAssetRequest final {
    JobSpec target;
    juce::File sourceFile;
    juce::String sessionUuid;
    juce::String assetUuid;
    juce::String projectOwnedRelativePath;
    juce::String inputDeviceIdentifier;
    juce::String expectedLayerUuid;
    std::size_t globalPadIndex{0U};
    std::size_t layerIndex{0U};
    std::uint64_t maximumDecodedBytes{defaultMilestone1DecodedBudgetBytes};
};

struct RecordedAssetPayload final {
    std::shared_ptr<const SampleAsset> asset;
    ExternalAssetReference reference;
    RecordedAssetRecord provenance;
    juce::String expectedLayerUuid;
    std::size_t globalPadIndex{0U};
    std::size_t layerIndex{0U};
};

class RecordedAssetPublisher final {
  public:
    [[nodiscard]] static std::optional<JobHandle> submit(BackgroundJobSystem& jobs,
                                                         RecordedAssetRequest request);
    [[nodiscard]] static std::shared_ptr<const JobResult>
    decode(const RecordedAssetRequest& request, const CancellationToken& cancellation,
           JobProgress& progress);
    [[nodiscard]] static juce::Result commit(const JobResult& result,
                                             ApplicationController& controller,
                                             SampleAssetRegistry& registry);
};
} // namespace padflow
