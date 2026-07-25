#include "SliceModel.h"

#include "Model/PadModel.h"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace padflow {
namespace {
juce::String sliceUuid(const SliceSet& set, const std::size_t index, const std::int64_t start,
                       const std::int64_t end) {
    return makeStableUuid(set.uuid + ":slice:" + juce::String{static_cast<juce::int64>(index)} +
                          ":" + juce::String{static_cast<juce::int64>(start)} + ":" +
                          juce::String{static_cast<juce::int64>(end)});
}

juce::Result validateRequest(const SliceGenerationRequest& request) {
    if (request.setUuid.isEmpty() || request.sourceAssetUuid.isEmpty() ||
        request.sourceFingerprint.isEmpty() || request.sourceLayerUuid.isEmpty())
        return juce::Result::fail("Slice generation requires stable source identities");
    if (request.trimStart < 0 || request.trimStart >= request.trimEnd)
        return juce::Result::fail("Slice generation requires a non-empty non-negative trim");
    return juce::Result::ok();
}

SliceSet makeBaseSet(const SliceGenerationRequest& request, const SliceAlgorithm algorithm) {
    SliceSet result;
    result.uuid = request.setUuid;
    result.sourceAssetUuid = request.sourceAssetUuid;
    result.sourceFingerprint = request.sourceFingerprint;
    result.sourceLayerUuid = request.sourceLayerUuid;
    result.sourceTrimStart = request.trimStart;
    result.sourceTrimEnd = request.trimEnd;
    result.algorithm = algorithm;
    result.algorithmVersion = sliceAlgorithmVersion;
    result.parameters.displayUnit = request.displayUnit;
    result.parameters.remainderPolicy = request.remainderPolicy;
    return result;
}
} // namespace

juce::Result validateSliceRegion(const SliceRegion& slice, const std::int64_t trimStart,
                                 const std::int64_t trimEnd) {
    if (slice.uuid.isEmpty())
        return juce::Result::fail("Slice UUID is empty");
    if (trimStart < 0 || trimStart >= trimEnd)
        return juce::Result::fail("Slice trim is invalid");
    if (slice.startFrame < trimStart || slice.startFrame >= slice.endFrame ||
        slice.endFrame > trimEnd)
        return juce::Result::fail("Slice must be a non-empty region inside active trim");
    return juce::Result::ok();
}

juce::Result validateSliceSet(const SliceSet& set, const bool requireContiguous) {
    if (set.uuid.isEmpty() || set.sourceAssetUuid.isEmpty() || set.sourceFingerprint.isEmpty() ||
        set.sourceLayerUuid.isEmpty())
        return juce::Result::fail("Slice set identity is incomplete");
    if (set.algorithmVersion == 0U)
        return juce::Result::fail("Slice algorithm version is invalid");
    if (set.sourceTrimStart < 0 || set.sourceTrimStart >= set.sourceTrimEnd)
        return juce::Result::fail("Slice set trim is invalid");
    if (set.slices.empty())
        return juce::Result::fail("Slice set must contain at least one slice");
    if (set.slices.size() > static_cast<std::size_t>(maximumProvisionalSliceCount))
        return juce::Result::fail("Slice set exceeds the bounded provisional count");

    std::set<juce::String> identities;
    std::int64_t previousEnd = set.sourceTrimStart;
    for (const auto& slice : set.slices) {
        if (const auto validation =
                validateSliceRegion(slice, set.sourceTrimStart, set.sourceTrimEnd);
            validation.failed())
            return validation;
        if (!identities.insert(slice.uuid).second)
            return juce::Result::fail("Slice UUIDs must be unique");
        if (slice.startFrame < previousEnd)
            return juce::Result::fail("Slices must not overlap");
        if (requireContiguous && slice.startFrame != previousEnd)
            return juce::Result::fail("Algorithmic slice sets must be contiguous");
        previousEnd = slice.endFrame;
    }
    if (requireContiguous && previousEnd != set.sourceTrimEnd)
        return juce::Result::fail("Algorithmic slice set must end at trim end");
    return juce::Result::ok();
}

juce::Result makeSliceSetFromBoundaries(const SliceGenerationRequest& request,
                                        const SliceAlgorithm algorithm,
                                        const std::vector<std::int64_t>& boundaries,
                                        SliceSet& output) {
    if (const auto validation = validateRequest(request); validation.failed())
        return validation;
    if (boundaries.size() < 2U ||
        boundaries.size() > static_cast<std::size_t>(maximumProvisionalSliceCount + 1))
        return juce::Result::fail("Slice boundary count is invalid");
    if (boundaries.front() != request.trimStart || boundaries.back() != request.trimEnd)
        return juce::Result::fail("Slice boundaries must preserve trim endpoints");
    if (!std::is_sorted(boundaries.begin(), boundaries.end()) ||
        std::adjacent_find(boundaries.begin(), boundaries.end()) != boundaries.end())
        return juce::Result::fail("Slice boundaries must be strictly ordered and unique");

    auto candidate = makeBaseSet(request, algorithm);
    candidate.slices.reserve(boundaries.size() - 1U);
    for (std::size_t index = 0U; index + 1U < boundaries.size(); ++index) {
        SliceRegion slice;
        slice.startFrame = boundaries[index];
        slice.endFrame = boundaries[index + 1U];
        slice.uuid = sliceUuid(candidate, index, slice.startFrame, slice.endFrame);
        slice.name = "Slice " + juce::String{static_cast<int>(index + 1U)};
        candidate.slices.push_back(std::move(slice));
    }
    if (const auto validation = validateSliceSet(candidate); validation.failed())
        return validation;
    output = std::move(candidate);
    return juce::Result::ok();
}

juce::Result generateEqualSlices(const SliceGenerationRequest& request, SliceSet& output) {
    if (const auto validation = validateRequest(request); validation.failed())
        return validation;
    const auto trimLength = request.trimEnd - request.trimStart;
    if (request.amount <= 0)
        return juce::Result::fail("Equal slice count must be positive");
    if (request.amount > trimLength)
        return juce::Result::fail("Equal slice count cannot exceed trim frame length");
    if (request.amount > maximumProvisionalSliceCount)
        return juce::Result::fail("Equal slice count exceeds the bounded provisional count");

    std::vector<std::int64_t> boundaries;
    boundaries.reserve(static_cast<std::size_t>(request.amount) + 1U);
    for (std::int64_t index = 0; index <= request.amount; ++index) {
        juce::BigInteger product{static_cast<juce::int64>(index)};
        product *= juce::BigInteger{static_cast<juce::int64>(trimLength)};
        product /= juce::BigInteger{static_cast<juce::int64>(request.amount)};
        boundaries.push_back(request.trimStart + static_cast<std::int64_t>(product.toInt64()));
    }

    auto candidateRequest = request;
    if (const auto result =
            makeSliceSetFromBoundaries(candidateRequest, SliceAlgorithm::equal, boundaries, output);
        result.failed())
        return result;
    output.parameters.sliceCount = request.amount;
    return juce::Result::ok();
}

juce::Result generateFixedLengthSlices(const SliceGenerationRequest& request, SliceSet& output) {
    if (const auto validation = validateRequest(request); validation.failed())
        return validation;
    const auto trimLength = request.trimEnd - request.trimStart;
    if (request.amount <= 0)
        return juce::Result::fail("Fixed slice length must be positive");
    if (request.amount > trimLength)
        return juce::Result::fail(
            "Fixed slice length above trim requires explicit whole-trim confirmation");

    const auto completeCount = trimLength / request.amount;
    const auto remainder = trimLength % request.amount;
    const auto includeRemainder =
        remainder != 0 && request.remainderPolicy == SliceRemainderPolicy::include;
    const auto sliceCount = completeCount + (includeRemainder ? 1 : 0);
    if (sliceCount <= 0 || sliceCount > maximumProvisionalSliceCount)
        return juce::Result::fail("Fixed slice count exceeds the bounded provisional count");

    std::vector<std::int64_t> boundaries;
    boundaries.reserve(static_cast<std::size_t>(sliceCount) + 1U);
    boundaries.push_back(request.trimStart);
    for (std::int64_t index = 1; index <= completeCount; ++index)
        boundaries.push_back(request.trimStart + index * request.amount);
    if (includeRemainder)
        boundaries.push_back(request.trimEnd);

    auto candidateRequest = request;
    if (request.remainderPolicy == SliceRemainderPolicy::discard && remainder != 0)
        candidateRequest.trimEnd = boundaries.back();
    if (const auto result = makeSliceSetFromBoundaries(
            candidateRequest, SliceAlgorithm::fixedLength, boundaries, output);
        result.failed())
        return result;
    output.sourceTrimEnd = request.trimEnd;
    output.parameters.fixedLengthFrames = request.amount;
    output.parameters.sliceCount = sliceCount;
    return validateSliceSet(output, request.remainderPolicy != SliceRemainderPolicy::discard);
}
} // namespace padflow
