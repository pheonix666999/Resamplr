#include "ChoppingSession.h"

#include "Model/PadModel.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace padflow {
juce::Result ChoppingSession::begin(ChoppingSessionTarget target) {
    if (target.sessionUuid.isEmpty() || target.projectUuid.isEmpty() ||
        target.targetPadUuid.isEmpty() || target.targetLayerUuid.isEmpty() ||
        target.sourceAssetUuid.isEmpty() || target.sourceFingerprint.isEmpty())
        return fail("Chopping session requires stable project and source identities");
    if (target.trimStart < 0 || target.trimStart >= target.trimEnd)
        return fail("Chopping session requires a non-empty non-negative trim");

    target_ = std::move(target);
    state_ = ChoppingSessionState::preparing;
    undoHistory_.clear();
    redoHistory_.clear();
    selectedSlice_ = 0U;
    lastError_.clear();
    provisional_.reset();

    const std::vector<std::int64_t> initial{target_.trimStart, target_.trimEnd};
    SliceSet candidate;
    if (const auto result =
            makeSliceSetFromBoundaries(requestFor(1), SliceAlgorithm::manual, initial, candidate);
        result.failed())
        return fail(result.getErrorMessage());
    provisional_ = std::move(candidate);
    state_ = ChoppingSessionState::ready;
    return juce::Result::ok();
}

void ChoppingSession::cancel() {
    provisional_.reset();
    undoHistory_.clear();
    redoHistory_.clear();
    selectedSlice_ = 0U;
    lastError_.clear();
    state_ = ChoppingSessionState::cancelled;
}

juce::Result ChoppingSession::regenerateEqual(const std::int64_t sliceCount) {
    if (!provisional_.has_value())
        return fail("Chopping session is not ready");
    SliceSet candidate;
    if (const auto result = generateEqualSlices(requestFor(sliceCount), candidate); result.failed())
        return fail(result.getErrorMessage());
    return applyProvisional(std::move(candidate));
}

juce::Result ChoppingSession::regenerateFixed(const std::int64_t lengthFrames,
                                              const SliceRemainderPolicy remainderPolicy,
                                              const SliceDisplayUnit displayUnit) {
    if (!provisional_.has_value())
        return fail("Chopping session is not ready");
    auto request = requestFor(lengthFrames);
    request.remainderPolicy = remainderPolicy;
    request.displayUnit = displayUnit;
    SliceSet candidate;
    if (const auto result = generateFixedLengthSlices(request, candidate); result.failed())
        return fail(result.getErrorMessage());
    return applyProvisional(std::move(candidate));
}

juce::Result ChoppingSession::addMarker(const std::int64_t sourceFrame) {
    auto candidate = boundaries();
    if (candidate.empty())
        return fail("Chopping session is not ready");
    if (sourceFrame <= target_.trimStart || sourceFrame >= target_.trimEnd)
        return fail("Manual marker must be strictly inside active trim");
    const auto insertion = std::lower_bound(candidate.begin(), candidate.end(), sourceFrame);
    if (insertion != candidate.end() && *insertion == sourceFrame)
        return fail("Manual marker already exists");
    candidate.insert(insertion, sourceFrame);
    return applyManualBoundaries(std::move(candidate));
}

juce::Result ChoppingSession::deleteMarker(const std::int64_t sourceFrame) {
    auto candidate = boundaries();
    if (candidate.empty())
        return fail("Chopping session is not ready");
    if (sourceFrame == target_.trimStart || sourceFrame == target_.trimEnd)
        return fail("Trim boundary markers cannot be deleted");
    const auto found = std::find(candidate.begin(), candidate.end(), sourceFrame);
    if (found == candidate.end())
        return fail("Manual marker does not exist");
    candidate.erase(found);
    return applyManualBoundaries(std::move(candidate));
}

juce::Result ChoppingSession::moveMarker(const std::int64_t sourceFrame,
                                         const std::int64_t requestedFrame) {
    auto candidate = boundaries();
    if (candidate.empty())
        return fail("Chopping session is not ready");
    const auto found = std::find(candidate.begin(), candidate.end(), sourceFrame);
    if (found == candidate.end() || found == candidate.begin() ||
        std::next(found) == candidate.end())
        return fail("Only internal markers can be moved");
    const auto previous = *std::prev(found);
    const auto next = *std::next(found);
    *found = std::clamp(requestedFrame, previous + 1, next - 1);
    return applyManualBoundaries(std::move(candidate));
}

juce::Result ChoppingSession::clearInternalMarkers() {
    if (!provisional_.has_value())
        return fail("Chopping session is not ready");
    return applyManualBoundaries({target_.trimStart, target_.trimEnd});
}

bool ChoppingSession::undoSessionEdit() {
    if (!provisional_.has_value() || undoHistory_.empty())
        return false;
    redoHistory_.push_back(std::move(*provisional_));
    provisional_ = std::move(undoHistory_.back());
    undoHistory_.pop_back();
    selectedSlice_ = std::min(selectedSlice_, provisional_->slices.size() - 1U);
    lastError_.clear();
    state_ = ChoppingSessionState::ready;
    return true;
}

bool ChoppingSession::redoSessionEdit() {
    if (!provisional_.has_value() || redoHistory_.empty())
        return false;
    undoHistory_.push_back(std::move(*provisional_));
    provisional_ = std::move(redoHistory_.back());
    redoHistory_.pop_back();
    selectedSlice_ = std::min(selectedSlice_, provisional_->slices.size() - 1U);
    lastError_.clear();
    state_ = ChoppingSessionState::ready;
    return true;
}

bool ChoppingSession::isCurrentTarget(const juce::String& projectUuid,
                                      const juce::String& sourceAssetUuid,
                                      const juce::String& targetLayerUuid,
                                      const std::uint64_t revision) const noexcept {
    return state_ != ChoppingSessionState::idle && state_ != ChoppingSessionState::cancelled &&
           target_.projectUuid == projectUuid && target_.sourceAssetUuid == sourceAssetUuid &&
           target_.targetLayerUuid == targetLayerUuid && target_.targetRevision == revision;
}

ChoppingSessionState ChoppingSession::state() const noexcept {
    return state_;
}

const ChoppingSessionTarget& ChoppingSession::target() const noexcept {
    return target_;
}

const std::optional<SliceSet>& ChoppingSession::provisionalSliceSet() const noexcept {
    return provisional_;
}

std::size_t ChoppingSession::selectedSlice() const noexcept {
    return selectedSlice_;
}

const juce::String& ChoppingSession::lastError() const noexcept {
    return lastError_;
}

bool ChoppingSession::canUndoSessionEdit() const noexcept {
    return !undoHistory_.empty();
}

bool ChoppingSession::canRedoSessionEdit() const noexcept {
    return !redoHistory_.empty();
}

SliceGenerationRequest ChoppingSession::requestFor(const std::int64_t amount) const {
    return {makeStableUuid(target_.sessionUuid + ":slice-set"),
            target_.sourceAssetUuid,
            target_.sourceFingerprint,
            target_.targetLayerUuid,
            target_.trimStart,
            target_.trimEnd,
            amount,
            SliceRemainderPolicy::include,
            SliceDisplayUnit::frames};
}

std::vector<std::int64_t> ChoppingSession::boundaries() const {
    std::vector<std::int64_t> result;
    if (!provisional_.has_value() || provisional_->slices.empty())
        return result;
    result.reserve(provisional_->slices.size() + 1U);
    result.push_back(provisional_->slices.front().startFrame);
    for (const auto& slice : provisional_->slices)
        result.push_back(slice.endFrame);
    return result;
}

juce::Result ChoppingSession::applyProvisional(SliceSet candidate) {
    if (const auto validation = validateSliceSet(
            candidate, candidate.algorithm != SliceAlgorithm::fixedLength ||
                           candidate.parameters.remainderPolicy != SliceRemainderPolicy::discard);
        validation.failed())
        return fail(validation.getErrorMessage());
    if (provisional_.has_value() && *provisional_ == candidate) {
        lastError_.clear();
        state_ = ChoppingSessionState::ready;
        return juce::Result::ok();
    }
    if (provisional_.has_value())
        undoHistory_.push_back(std::move(*provisional_));
    provisional_ = std::move(candidate);
    redoHistory_.clear();
    selectedSlice_ = std::min(selectedSlice_, provisional_->slices.size() - 1U);
    lastError_.clear();
    state_ = ChoppingSessionState::ready;
    return juce::Result::ok();
}

juce::Result ChoppingSession::applyManualBoundaries(std::vector<std::int64_t> candidate) {
    auto request = requestFor(static_cast<std::int64_t>(candidate.size() - 1U));
    SliceSet generated;
    if (const auto result =
            makeSliceSetFromBoundaries(request, SliceAlgorithm::manual, candidate, generated);
        result.failed())
        return fail(result.getErrorMessage());
    generated.parameters.sliceCount = static_cast<std::int64_t>(generated.slices.size());
    return applyProvisional(std::move(generated));
}

juce::Result ChoppingSession::fail(juce::String message) {
    lastError_ = std::move(message);
    return juce::Result::fail(lastError_);
}
} // namespace padflow
