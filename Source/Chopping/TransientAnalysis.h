#pragma once

#include "Chopping/SliceModel.h"
#include "Sampling/SampleAsset.h"
#include "Utilities/BackgroundJobSystem.h"

#include <cstdint>
#include <memory>

namespace padflow {
struct TransientAnalysisParameters final {
    float sensitivity{0.5F};
    std::int64_t minimumSliceFrames{1};
    std::int64_t attackLookBackFrames{0};
    float thresholdFloor{0.0F};

    [[nodiscard]] friend bool operator==(const TransientAnalysisParameters&,
                                         const TransientAnalysisParameters&) = default;
};

struct TransientAnalysisRequest final {
    JobSpec target;
    SliceGenerationRequest slices;
    TransientAnalysisParameters parameters;
    std::shared_ptr<const SampleAsset> asset;
};

struct TransientAnalysisPayload final {
    SliceSet sliceSet;
    juce::String targetLayerUuid;
    juce::String sourceFingerprint;
};

class TransientAnalysis final {
  public:
    [[nodiscard]] static std::optional<JobHandle> submit(BackgroundJobSystem& jobs,
                                                         TransientAnalysisRequest request);
    [[nodiscard]] static std::shared_ptr<const JobResult>
    analyse(const TransientAnalysisRequest& request, const CancellationToken& cancellation,
            JobProgress& progress);
};
} // namespace padflow
