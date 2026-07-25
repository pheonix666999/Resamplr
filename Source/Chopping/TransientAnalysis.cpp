#include "TransientAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace padflow {
namespace {
std::shared_ptr<const JobResult> failure(const JobSpec& target, juce::String message) {
    return std::make_shared<JobResult>(JobResult{target, false, std::move(message), nullptr});
}

bool finiteParameters(const TransientAnalysisParameters& parameters) {
    return std::isfinite(parameters.sensitivity) && std::isfinite(parameters.thresholdFloor) &&
           parameters.sensitivity >= 0.0F && parameters.sensitivity <= 1.0F &&
           parameters.thresholdFloor >= 0.0F && parameters.minimumSliceFrames > 0 &&
           parameters.attackLookBackFrames >= 0;
}

float finiteMagnitude(const float sample) {
    return std::isfinite(sample) ? std::abs(sample) : 0.0F;
}
} // namespace

std::optional<JobHandle> TransientAnalysis::submit(BackgroundJobSystem& jobs,
                                                   TransientAnalysisRequest request) {
    request.target.kind = JobKind::transientAnalysis;
    const auto target = request.target;
    return jobs.submit(target, [request = std::move(request)](const CancellationToken& token,
                                                              JobProgress& progress) {
        return analyse(request, token, progress);
    });
}

std::shared_ptr<const JobResult> TransientAnalysis::analyse(const TransientAnalysisRequest& request,
                                                            const CancellationToken& cancellation,
                                                            JobProgress& progress) {
    if (request.target.kind != JobKind::transientAnalysis)
        return failure(request.target, "Transient analysis job kind is invalid");
    if (request.asset == nullptr || !finiteParameters(request.parameters))
        return failure(request.target, "Transient analysis input is invalid");

    const auto view = request.asset->view();
    if (view.interleavedData == nullptr || view.channelCount == 0U || view.channelCount > 2U ||
        request.slices.trimStart < 0 || request.slices.trimStart >= request.slices.trimEnd ||
        static_cast<std::uint64_t>(request.slices.trimEnd) > view.frameCount)
        return failure(request.target, "Transient analysis trim or PCM view is invalid");

    const auto frameCount = request.slices.trimEnd - request.slices.trimStart;
    std::vector<float> envelope(static_cast<std::size_t>(frameCount), 0.0F);
    for (std::int64_t offset = 0; offset < frameCount; ++offset) {
        if ((offset & 1023) == 0 && cancellation.isCancellationRequested())
            return failure(request.target, "Transient analysis cancelled");
        const auto sourceFrame = request.slices.trimStart + offset;
        const auto sampleIndex = static_cast<std::uint64_t>(sourceFrame) * view.channelCount;
        auto magnitude = 0.0F;
        for (std::uint32_t channel = 0U; channel < view.channelCount; ++channel)
            magnitude =
                std::max(magnitude, finiteMagnitude(view.interleavedData[sampleIndex + channel]));
        envelope[static_cast<std::size_t>(offset)] = magnitude;
    }
    progress.set(0.35F);

    struct Candidate final {
        std::int64_t frame{0};
        float strength{0.0F};
    };
    std::vector<float> strengths(envelope.size(), 0.0F);
    auto maximumStrength = 0.0F;
    for (std::size_t index = 1U; index < envelope.size(); ++index) {
        strengths[index] = std::max(0.0F, envelope[index] - envelope[index - 1U]);
        maximumStrength = std::max(maximumStrength, strengths[index]);
    }

    std::vector<std::int64_t> internalMarkers;
    const auto epsilon = std::numeric_limits<float>::epsilon();
    if (maximumStrength > std::max(request.parameters.thresholdFloor, epsilon)) {
        const auto threshold =
            std::max(request.parameters.thresholdFloor,
                     maximumStrength * (1.0F - 0.9F * request.parameters.sensitivity));
        std::vector<Candidate> candidates;
        for (std::size_t index = 1U; index < strengths.size(); ++index) {
            const auto left = strengths[index - 1U];
            const auto right = index + 1U < strengths.size() ? strengths[index + 1U] : 0.0F;
            if (strengths[index] >= threshold && strengths[index] > left &&
                strengths[index] >= right)
                candidates.push_back({request.slices.trimStart + static_cast<std::int64_t>(index),
                                      strengths[index]});
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto& first, const auto& second) {
            if (first.strength != second.strength)
                return first.strength > second.strength;
            return first.frame < second.frame;
        });
        for (const auto& candidate : candidates) {
            const auto tooClose = std::any_of(internalMarkers.begin(), internalMarkers.end(),
                                              [&](const auto accepted) {
                                                  return std::abs(candidate.frame - accepted) <
                                                         request.parameters.minimumSliceFrames;
                                              });
            if (!tooClose)
                internalMarkers.push_back(candidate.frame);
        }
        for (auto& marker : internalMarkers)
            marker = std::max(request.slices.trimStart,
                              marker - request.parameters.attackLookBackFrames);
        std::sort(internalMarkers.begin(), internalMarkers.end());
        internalMarkers.erase(std::remove_if(internalMarkers.begin(), internalMarkers.end(),
                                             [&](const auto marker) {
                                                 return marker <= request.slices.trimStart ||
                                                        marker >= request.slices.trimEnd;
                                             }),
                              internalMarkers.end());
        internalMarkers.erase(std::unique(internalMarkers.begin(), internalMarkers.end()),
                              internalMarkers.end());
    }
    progress.set(0.8F);
    if (cancellation.isCancellationRequested())
        return failure(request.target, "Transient analysis cancelled");

    std::vector<std::int64_t> boundaries;
    boundaries.reserve(internalMarkers.size() + 2U);
    boundaries.push_back(request.slices.trimStart);
    boundaries.insert(boundaries.end(), internalMarkers.begin(), internalMarkers.end());
    boundaries.push_back(request.slices.trimEnd);
    SliceSet sliceSet;
    if (const auto generated = makeSliceSetFromBoundaries(request.slices, SliceAlgorithm::transient,
                                                          boundaries, sliceSet);
        generated.failed())
        return failure(request.target, generated.getErrorMessage());
    sliceSet.parameters.transientSensitivity = request.parameters.sensitivity;
    sliceSet.parameters.minimumSliceFrames = request.parameters.minimumSliceFrames;
    sliceSet.parameters.attackLookBackFrames = request.parameters.attackLookBackFrames;
    sliceSet.parameters.sliceCount = static_cast<std::int64_t>(sliceSet.slices.size());

    auto payload = std::make_shared<TransientAnalysisPayload>(TransientAnalysisPayload{
        std::move(sliceSet), request.slices.sourceLayerUuid, request.slices.sourceFingerprint});
    progress.set(1.0F);
    return std::make_shared<JobResult>(
        JobResult{request.target, true, "Transient analysis completed", std::move(payload)});
}
} // namespace padflow
