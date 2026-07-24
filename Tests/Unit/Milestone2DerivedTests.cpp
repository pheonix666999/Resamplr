#include "Sampling/DerivedAsset.h"
#include "Serialization/ProjectSerializer.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace padflow {
namespace {
std::shared_ptr<const SampleAsset>
makeDerivedFixture(const juce::String& uuid, const std::uint32_t channels, std::vector<float> pcm) {
    SampleAssetMetadata metadata;
    metadata.assetUuid = uuid;
    metadata.displayName = uuid;
    metadata.sampleRate = 48000.0;
    metadata.channelCount = channels;
    metadata.frameCount = static_cast<std::uint64_t>(pcm.size() / channels);
    metadata.provenance = "synthetic-test";
    metadata.sourcePath = uuid + ".wav";
    metadata.sourceFormat = "WAV";
    metadata.contentFingerprint = "fixture:" + uuid;
    metadata.sourceFileBytes = static_cast<std::uint64_t>(pcm.size() * sizeof(float));
    metadata.durationSeconds = static_cast<double>(metadata.frameCount) / metadata.sampleRate;
    return SampleAsset::create(std::move(metadata), std::move(pcm));
}

ExternalAssetReference makeDerivedReference(const SampleAsset& asset) {
    const auto& metadata = asset.metadata();
    ExternalAssetReference reference;
    reference.uuid = metadata.assetUuid;
    reference.originalPath = metadata.sourcePath;
    reference.originalName = metadata.displayName + ".wav";
    reference.format = "WAV";
    reference.contentFingerprint = metadata.contentFingerprint;
    reference.sourceFileBytes = metadata.sourceFileBytes;
    reference.channels = metadata.channelCount;
    reference.sourceSampleRate = metadata.sampleRate;
    reference.frameCount = metadata.frameCount;
    reference.decodedBytes = static_cast<std::uint64_t>(asset.decodedBytes());
    return reference;
}

void assignFixture(ApplicationController& controller, SampleAssetRegistry& registry,
                   const std::shared_ptr<const SampleAsset>& asset) {
    juce::ignoreUnused(registry.publish(asset));
    const auto target = JobSpec{controller.project().uuid(), controller.project().pad(0U).uuid,
                                controller.project().revision(), 0};
    juce::ignoreUnused(
        controller.commitImportedLayer(target, 0U, 0U, makeDerivedReference(*asset)));
}

DerivedAssetRequest makeRequest(const ApplicationController& controller,
                                const std::shared_ptr<const SampleAsset>& source,
                                const juce::File& outputDirectory,
                                const DerivedAssetOperation operation) {
    DerivedAssetRequest request;
    request.target = {controller.project().uuid(), controller.project().pad(0U).uuid,
                      controller.project().revision(), 0};
    request.source = source;
    request.sourceReference = makeDerivedReference(*source);
    request.playback = resolveSamplePlaybackSettings(controller.project().pad(0U).layers[0],
                                                     source->metadata().frameCount);
    request.operation = operation;
    request.outputDirectory = outputDirectory;
    request.globalPadIndex = 0U;
    request.layerIndex = 0U;
    return request;
}

const DerivedAssetPayload* renderNow(const DerivedAssetRequest& request,
                                     std::shared_ptr<const JobResult>& result) {
    CancellationToken cancellation;
    JobProgress progress;
    result = DerivedAssetRenderer::render(request, cancellation, progress);
    return result != nullptr && result->succeeded
               ? static_cast<const DerivedAssetPayload*>(result->immutablePayload.get())
               : nullptr;
}

float peakOf(const std::span<const float> pcm) {
    auto peak = 0.0F;
    for (const auto sample : pcm)
        peak = std::max(peak, std::abs(sample));
    return peak;
}
} // namespace

class Milestone2DerivedTests final : public juce::UnitTest {
  public:
    Milestone2DerivedTests() : juce::UnitTest("Milestone 2 derived assets", "PadFlow") {}

    void runTest() override {
        const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getNonexistentChildFile("padflow-derived-tests", {}, true);
        const auto derivedDirectory = root.getChildFile("Derived");
        expect(root.createDirectory());

        const std::vector<float> sourceValues{0.1F, -0.1F, 0.5F, 0.25F, -1.0F, 1.0F, 0.2F, 0.4F};
        const auto source = makeDerivedFixture("derived-source", 2U, sourceValues);
        ApplicationController controller;
        controller.createEmptyProject("Derived Test", "derived-project");
        SampleAssetRegistry registry{8U * 1024U * 1024U};
        assignFixture(controller, registry, source);

        beginTest("DERIVED-M2-001 and DERIVED-M2-003 normalize to the requested finite peak");
        auto normalize =
            makeRequest(controller, source, derivedDirectory, DerivedAssetOperation::normalize);
        normalize.normalizeTargetDecibels = -1.0F;
        std::shared_ptr<const JobResult> normalizeResult;
        const auto* normalized = renderNow(normalize, normalizeResult);
        expect(normalized != nullptr);
        if (normalized != nullptr) {
            const auto expected = std::pow(10.0F, -1.0F / 20.0F);
            expectWithinAbsoluteError(peakOf(normalized->asset->interleavedPcm()), expected,
                                      1.0e-5F);
            expect(DerivedAssetRenderer::commit(*normalizeResult, controller, registry).wasOk());
        }

        beginTest("DERIVED-M2-011 source remains immutable and DERIVED-M2-015 undo switches UUID");
        expect(std::equal(source->interleavedPcm().begin(), source->interleavedPcm().end(),
                          sourceValues.begin(), sourceValues.end()));
        const auto normalizedUuid = controller.project().pad(0U).layers[0].assetUuid;
        expect(normalizedUuid != source->metadata().assetUuid);
        expect(controller.undo());
        expectEquals(controller.project().pad(0U).layers[0].assetUuid,
                     source->metadata().assetUuid);
        expect(controller.redo());
        expectEquals(controller.project().pad(0U).layers[0].assetUuid, normalizedUuid);

        beginTest("DERIVED-M2-012 deterministic recipe is reusable");
        const auto recipe = DerivedAssetRenderer::recipeFor(normalize);
        expectEquals(recipe.derivedAssetUuid, normalizedUuid);
        expectEquals(
            DerivedAssetRenderer::findReusableAssetUuid(controller.project().state(), recipe),
            normalizedUuid);

        beginTest("SAVE-M2-005 derived provenance survives schema-v1 round-trip");
        auto restored = Project::createEmpty();
        expect(ProjectSerializer::restoreCanonicalManifest(
                   ProjectSerializer::canonicalManifest(controller.project()), restored)
                   .wasOk());
        expectEquals(static_cast<int>(restored.state().derivedAssets.size()), 1);
        expectEquals(restored.state().derivedAssets[0].derivedAssetUuid, normalizedUuid);

        expect(controller.undo());
        beginTest("DERIVED-M2-004 and DERIVED-M2-005 stereo average and mono no-op policy");
        auto mono =
            makeRequest(controller, source, derivedDirectory, DerivedAssetOperation::stereoToMono);
        std::shared_ptr<const JobResult> monoResult;
        const auto* monoPayload = renderNow(mono, monoResult);
        expect(monoPayload != nullptr);
        if (monoPayload != nullptr) {
            const auto pcm = monoPayload->asset->interleavedPcm();
            expectEquals(static_cast<int>(monoPayload->asset->metadata().channelCount), 1);
            expectWithinAbsoluteError(pcm[0], 0.0F, 1.0e-6F);
            expectWithinAbsoluteError(pcm[1], 0.375F, 1.0e-6F);
            expectWithinAbsoluteError(pcm[2], 0.0F, 1.0e-6F);
        }
        const auto monoSource =
            makeDerivedFixture("mono-source", 1U, std::vector<float>{0.25F, -0.5F});
        auto monoNoOp = mono;
        monoNoOp.source = monoSource;
        monoNoOp.sourceReference = makeDerivedReference(*monoSource);
        monoNoOp.playback = SamplePlaybackSettings{0U, 2U, 0U, 2U, false, false, false, true};
        std::shared_ptr<const JobResult> monoNoOpResult;
        const auto* monoNoOpPayload = renderNow(monoNoOp, monoNoOpResult);
        expect(monoNoOpPayload != nullptr);
        if (monoNoOpPayload != nullptr)
            expect(std::equal(monoNoOpPayload->asset->interleavedPcm().begin(),
                              monoNoOpPayload->asset->interleavedPcm().end(),
                              monoSource->interleavedPcm().begin(),
                              monoSource->interleavedPcm().end()));

        beginTest("DERIVED-M2-006 and DERIVED-M2-007 linear fades stay inside active trim");
        auto fadeIn =
            makeRequest(controller, source, derivedDirectory, DerivedAssetOperation::fadeIn);
        fadeIn.playback = SamplePlaybackSettings{1U, 4U, 1U, 4U, false, false, false, true};
        fadeIn.fadeDurationFrames = 2U;
        std::shared_ptr<const JobResult> fadeInResult;
        const auto* fadedIn = renderNow(fadeIn, fadeInResult);
        expect(fadedIn != nullptr);
        if (fadedIn != nullptr) {
            const auto pcm = fadedIn->asset->interleavedPcm();
            expectWithinAbsoluteError(pcm[0], sourceValues[0], 1.0e-6F);
            expectWithinAbsoluteError(pcm[2], 0.0F, 1.0e-6F);
            expectWithinAbsoluteError(pcm[4], sourceValues[4], 1.0e-6F);
        }
        auto fadeOut = fadeIn;
        fadeOut.operation = DerivedAssetOperation::fadeOut;
        std::shared_ptr<const JobResult> fadeOutResult;
        const auto* fadedOut = renderNow(fadeOut, fadeOutResult);
        expect(fadedOut != nullptr);
        if (fadedOut != nullptr)
            expectWithinAbsoluteError(fadedOut->asset->interleavedPcm()[6], 0.0F, 1.0e-6F);

        beginTest("DERIVED-M2-008 through DERIVED-M2-010 crop and rebase valid loop");
        auto crop = makeRequest(controller, source, derivedDirectory, DerivedAssetOperation::crop);
        crop.playback = SamplePlaybackSettings{1U, 4U, 2U, 4U, true, true, false, true};
        std::shared_ptr<const JobResult> cropResult;
        const auto* cropped = renderNow(crop, cropResult);
        expect(cropped != nullptr);
        if (cropped != nullptr) {
            expectEquals(static_cast<int>(cropped->asset->metadata().frameCount), 3);
            expectEquals(static_cast<int>(cropped->playback.startFrame), 0);
            expectEquals(static_cast<int>(cropped->playback.endFrame), 3);
            expectEquals(static_cast<int>(cropped->playback.loopStartFrame), 1);
            expectEquals(static_cast<int>(cropped->playback.loopEndFrame), 3);
            expect(cropped->playback.loopEnabled);
        }

        beginTest("DERIVED-M2-002 silent normalize remains silent");
        const auto silent = makeDerivedFixture("silent-source", 1U, std::vector<float>(32U, 0.0F));
        auto silentRequest = normalize;
        silentRequest.source = silent;
        silentRequest.sourceReference = makeDerivedReference(*silent);
        silentRequest.playback =
            SamplePlaybackSettings{0U, 32U, 0U, 32U, false, false, false, true};
        std::shared_ptr<const JobResult> silentResult;
        const auto* silentPayload = renderNow(silentRequest, silentResult);
        expect(silentPayload != nullptr);
        if (silentPayload != nullptr)
            expectEquals(peakOf(silentPayload->asset->interleavedPcm()), 0.0F);

        beginTest("DERIVED-M2-013 cancellation leaves model and files unchanged");
        auto cancelledRequest =
            makeRequest(controller, source, derivedDirectory, DerivedAssetOperation::fadeIn);
        cancelledRequest.fadeDurationFrames = 2U;
        CancellationToken cancelled;
        cancelled.cancel();
        JobProgress cancelledProgress;
        const auto cancelledResult =
            DerivedAssetRenderer::render(cancelledRequest, cancelled, cancelledProgress);
        expect(cancelledResult != nullptr && !cancelledResult->succeeded);
        expectEquals(controller.project().pad(0U).layers[0].assetUuid,
                     source->metadata().assetUuid);

        beginTest("DERIVED-M2-014 and DERIVED-M2-016 stale completion removes new output");
        auto stale =
            makeRequest(controller, source, derivedDirectory, DerivedAssetOperation::fadeOut);
        stale.fadeDurationFrames = 3U;
        std::shared_ptr<const JobResult> staleResult;
        const auto* stalePayload = renderNow(stale, staleResult);
        expect(stalePayload != nullptr);
        const auto staleFile = stalePayload != nullptr
                                   ? juce::File{stalePayload->reference.originalPath}
                                   : juce::File{};
        expect(controller.renamePad(0U, "Changed while rendering").wasOk());
        expect(staleResult != nullptr &&
               DerivedAssetRenderer::commit(*staleResult, controller, registry).failed());
        expect(!staleFile.existsAsFile());

        registry.clear();
        expect(root.deleteRecursively());
    }
};

static Milestone2DerivedTests milestone2DerivedTests;
} // namespace padflow
