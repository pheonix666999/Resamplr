#include "SamplePreviewController.h"

#include <utility>

namespace padflow {
namespace {
constexpr auto previewOwnerUuid = "padflow-preview";
constexpr auto previewTargetUuid = "preview-voice";
} // namespace

SamplePreviewController::SamplePreviewController(ApplicationController& controller,
                                                 PreviewPlayer& player) noexcept
    : controller_(controller), player_(player) {
    player_.setVolume(controller_.project().state().ui.previewVolume);
}

std::optional<JobHandle> SamplePreviewController::begin(BackgroundJobSystem& jobs,
                                                        const juce::File& sourceFile,
                                                        const std::uint64_t maximumDecodedBytes) {
    if (pending_.has_value())
        pending_->cancel();
    juce::ignoreUnused(stop());
    lastError_.clear();
    ++revision_;

    SampleImportRequest request{
        JobSpec{previewOwnerUuid, previewTargetUuid, revision_, 1, JobKind::samplePreview},
        sourceFile,
        "preview-" + juce::String{static_cast<juce::int64>(revision_)},
        0U,
        0U,
        maximumDecodedBytes,
    };
    pending_ = SampleImporter::submit(jobs, std::move(request));
    if (!pending_.has_value())
        lastError_ = "Preview queue is full";
    return pending_;
}

juce::Result SamplePreviewController::commit(const JobResult& result) {
    if (result.target.ownerUuid != previewOwnerUuid ||
        result.target.targetUuid != previewTargetUuid || result.target.targetRevision != revision_)
        return juce::Result::fail("Preview completion is stale");
    pending_.reset();
    if (!result.succeeded) {
        juce::ignoreUnused(stop());
        lastError_ = result.message;
        return juce::Result::fail(lastError_);
    }

    const auto* payload = static_cast<const SampleImportPayload*>(result.immutablePayload.get());
    if (payload == nullptr || payload->asset == nullptr) {
        juce::ignoreUnused(stop());
        lastError_ = "Preview returned no immutable asset";
        return juce::Result::fail(lastError_);
    }

    ++revision_;
    retireActive(revision_);
    activeAsset_ = payload->asset;
    if (!player_.publishAndStart(activeAsset_.get(), revision_)) {
        retireActive(++revision_);
        lastError_ = "Preview command queue is full";
        return juce::Result::fail(lastError_);
    }
    lastError_.clear();
    return juce::Result::ok();
}

juce::Result SamplePreviewController::setVolume(const float volume) {
    if (volume < 0.0F || volume > 1.0F)
        return juce::Result::fail("Preview volume must be within 0..1");
    auto state = controller_.project().state().ui;
    state.previewVolume = volume;
    const auto result = controller_.setUiState(state);
    if (result.wasOk())
        player_.setVolume(volume);
    return result;
}

bool SamplePreviewController::stop() noexcept {
    ++revision_;
    const auto queued = player_.requestStop(revision_);
    if (queued)
        retireActive(revision_);
    return queued;
}

std::size_t SamplePreviewController::collectRetired() {
    reclaimer_.acknowledgeAudioEpoch(player_.acknowledgedEpoch());
    return reclaimer_.collectAcknowledged();
}

juce::String SamplePreviewController::lastError() const {
    return lastError_;
}

std::uint64_t SamplePreviewController::pendingRevision() const noexcept {
    return revision_;
}

void SamplePreviewController::retireActive(const std::uint64_t epoch) {
    if (activeAsset_ != nullptr)
        reclaimer_.retireAfterEpoch(epoch, std::move(activeAsset_));
}
} // namespace padflow
