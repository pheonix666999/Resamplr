#include "RecordedAsset.h"

#include "Sampling/SampleImporter.h"

#include <utility>

namespace padflow {
namespace {
std::shared_ptr<const JobResult> failedResult(const JobSpec& target, juce::String message) {
    return std::make_shared<const JobResult>(JobResult{target, false, std::move(message), {}});
}
} // namespace

std::optional<JobHandle> RecordedAssetPublisher::submit(BackgroundJobSystem& jobs,
                                                        RecordedAssetRequest request) {
    const auto target = request.target;
    return jobs.submit(target, [request = std::move(request)](const CancellationToken& cancellation,
                                                              JobProgress& progress) {
        return decode(request, cancellation, progress);
    });
}

std::shared_ptr<const JobResult>
RecordedAssetPublisher::decode(const RecordedAssetRequest& request,
                               const CancellationToken& cancellation, JobProgress& progress) {
    if (request.target.kind != JobKind::recordedAsset)
        return failedResult(request.target, "Recorded asset request has the wrong job kind");
    if (request.sessionUuid.trim().isEmpty() || request.projectOwnedRelativePath.trim().isEmpty() ||
        request.expectedLayerUuid.trim().isEmpty())
        return failedResult(request.target, "Recorded asset request is incomplete");

    const SampleImportRequest importRequest{request.target,     request.sourceFile,
                                            request.assetUuid,  request.globalPadIndex,
                                            request.layerIndex, request.maximumDecodedBytes};
    const auto decoded = SampleImporter::decode(importRequest, cancellation, progress);
    if (decoded == nullptr || !decoded->succeeded)
        return decoded != nullptr
                   ? decoded
                   : failedResult(request.target, "Recorded asset decoding returned no result");
    const auto* imported = static_cast<const SampleImportPayload*>(decoded->immutablePayload.get());
    if (imported == nullptr || imported->asset == nullptr)
        return failedResult(request.target, "Recorded asset decoding returned no immutable audio");

    RecordedAssetRecord provenance;
    provenance.recordedAssetUuid = imported->reference.uuid;
    provenance.sessionUuid = request.sessionUuid;
    provenance.contentFingerprint = imported->reference.contentFingerprint;
    provenance.projectOwnedRelativePath = request.projectOwnedRelativePath;
    provenance.inputDeviceIdentifier = request.inputDeviceIdentifier;
    provenance.targetProjectUuid = request.target.ownerUuid;
    provenance.targetPadUuid = request.target.targetUuid;
    provenance.targetLayerUuid = request.expectedLayerUuid;
    provenance.targetProjectRevision = request.target.targetRevision;
    provenance.channels = imported->reference.channels;
    provenance.sampleRate = imported->reference.sourceSampleRate;
    provenance.frameCount = imported->reference.frameCount;

    auto payload = std::make_shared<const RecordedAssetPayload>(RecordedAssetPayload{
        imported->asset, imported->reference, std::move(provenance), request.expectedLayerUuid,
        request.globalPadIndex, request.layerIndex});
    progress.set(1.0F);
    return std::make_shared<const JobResult>(
        JobResult{request.target, true, "Recorded asset decoded", std::move(payload)});
}

juce::Result RecordedAssetPublisher::commit(const JobResult& result,
                                            ApplicationController& controller,
                                            SampleAssetRegistry& registry) {
    if (!result.succeeded)
        return juce::Result::fail(result.message);
    if (result.target.kind != JobKind::recordedAsset)
        return juce::Result::fail("Recorded asset completion has the wrong job kind");
    if (!controller.isCurrentJobTarget(result.target))
        return juce::Result::fail("Recorded asset completion is stale; file remains unassigned");
    const auto* payload = static_cast<const RecordedAssetPayload*>(result.immutablePayload.get());
    if (payload == nullptr || payload->asset == nullptr)
        return juce::Result::fail("Recorded asset completion has no immutable audio");

    const auto previous = registry.find(payload->asset->metadata().assetUuid);
    if (!registry.publish(payload->asset))
        return juce::Result::fail("Recorded sample exceeds the decoded-memory budget");

    const auto modelResult = controller.commitRecordedLayer(
        result.target, payload->globalPadIndex, payload->layerIndex, payload->expectedLayerUuid,
        payload->reference, payload->provenance);
    if (modelResult.failed()) {
        if (previous != nullptr)
            juce::ignoreUnused(registry.publish(previous));
        else
            juce::ignoreUnused(registry.erase(payload->asset->metadata().assetUuid));
        return modelResult;
    }
    return juce::Result::ok();
}
} // namespace padflow
