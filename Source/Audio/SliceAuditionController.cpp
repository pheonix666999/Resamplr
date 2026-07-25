#include "SliceAuditionController.h"

#include <utility>

namespace padflow {
SliceAuditionController::SliceAuditionController(PreviewPlayer& player) noexcept
    : player_(player) {}

bool SliceAuditionController::startSelected(std::shared_ptr<const SampleAsset> asset,
                                            const SliceRegion& slice, const bool reverse) {
    sequence_.clear();
    sequence_.push_back(slice);
    sequenceIndex_ = 0U;
    reverse_ = reverse;
    activeAsset_ = std::move(asset);
    return startSlice(sequence_.front());
}

bool SliceAuditionController::startSequential(std::shared_ptr<const SampleAsset> asset,
                                              const std::vector<SliceRegion>& slices,
                                              const bool reverse) {
    if (slices.empty())
        return false;
    sequence_ = slices;
    sequenceIndex_ = 0U;
    reverse_ = reverse;
    activeAsset_ = std::move(asset);
    return startSlice(sequence_.front());
}

bool SliceAuditionController::startLazy(std::shared_ptr<const SampleAsset> asset,
                                        const std::int64_t trimStart, const std::int64_t trimEnd) {
    if (trimStart < 0 || trimStart >= trimEnd)
        return false;
    SliceRegion region;
    region.startFrame = trimStart;
    region.endFrame = trimEnd;
    return startSelected(std::move(asset), region);
}

bool SliceAuditionController::stop() noexcept {
    sequence_.clear();
    sequenceIndex_ = 0U;
    const auto stopEpoch = ++epoch_;
    if (!player_.requestStop(stopEpoch))
        return false;
    retireActive(stopEpoch);
    return true;
}

void SliceAuditionController::service() {
    reclaimer_.acknowledgeAudioEpoch(player_.acknowledgedEpoch());
    juce::ignoreUnused(reclaimer_.collectAcknowledged());
    if (activeAsset_ == nullptr || player_.isActive())
        return;
    if (sequence_.size() <= 1U) {
        sequence_.clear();
        retireActive(player_.acknowledgedEpoch());
        return;
    }
    if (++sequenceIndex_ >= sequence_.size()) {
        sequence_.clear();
        retireActive(player_.acknowledgedEpoch());
        return;
    }
    if (!startSlice(sequence_[sequenceIndex_])) {
        sequence_.clear();
        retireActive(player_.acknowledgedEpoch());
    }
}

void SliceAuditionController::clearWhenQuiescent() noexcept {
    player_.panicWhenQuiescent();
    sequence_.clear();
    sequenceIndex_ = 0U;
    activeAsset_.reset();
    juce::ignoreUnused(reclaimer_.collectAcknowledged());
}

bool SliceAuditionController::active() const noexcept {
    return activeAsset_ != nullptr && (player_.isActive() || !sequence_.empty());
}

bool SliceAuditionController::sequential() const noexcept {
    return sequence_.size() > 1U;
}

std::uint64_t SliceAuditionController::sourceFramePosition() const noexcept {
    return player_.sourceFramePosition();
}

bool SliceAuditionController::startSlice(const SliceRegion& slice) {
    if (activeAsset_ == nullptr || slice.startFrame < 0 || slice.startFrame >= slice.endFrame)
        return false;
    const auto start = static_cast<std::uint64_t>(slice.startFrame);
    const auto end = static_cast<std::uint64_t>(slice.endFrame);
    if (end > activeAsset_->metadata().frameCount)
        return false;
    return player_.publishAndStartSlice(activeAsset_.get(), start, end, reverse_, ++epoch_);
}

void SliceAuditionController::retireActive(const std::uint64_t epoch) {
    if (activeAsset_ != nullptr)
        reclaimer_.retireAfterEpoch(epoch, std::move(activeAsset_));
}
} // namespace padflow
