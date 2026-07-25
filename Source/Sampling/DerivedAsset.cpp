#include "DerivedAsset.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace padflow {
namespace {
constexpr std::size_t processingChunkSamples = 16384U;

[[nodiscard]] const char* operationName(const DerivedAssetOperation operation) noexcept {
    switch (operation) {
    case DerivedAssetOperation::normalize:
        return "normalize";
    case DerivedAssetOperation::stereoToMono:
        return "stereo-to-mono";
    case DerivedAssetOperation::fadeIn:
        return "fade-in";
    case DerivedAssetOperation::fadeOut:
        return "fade-out";
    case DerivedAssetOperation::crop:
        return "crop";
    }
    return "unknown";
}

[[nodiscard]] std::shared_ptr<const JobResult> failedResult(const JobSpec& target,
                                                            juce::String message) {
    return std::make_shared<const JobResult>(JobResult{target, false, std::move(message), {}});
}

[[nodiscard]] float finiteSample(const float sample) noexcept {
    return std::isfinite(sample) ? sample : 0.0F;
}

[[nodiscard]] juce::String fingerprintPcm(const std::vector<float>& pcm,
                                          const std::uint32_t channels,
                                          const std::uint64_t frames) {
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = 14695981039346656037ULL;
    const auto addByte = [&hash](const std::uint8_t value) {
        hash ^= value;
        hash *= prime;
    };
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U)
        addByte(static_cast<std::uint8_t>((channels >> shift) & 0xffU));
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U)
        addByte(static_cast<std::uint8_t>((frames >> shift) & 0xffU));
    for (const auto sample : pcm) {
        const auto bits = std::bit_cast<std::uint32_t>(finiteSample(sample));
        for (std::uint32_t shift = 0U; shift < 32U; shift += 8U)
            addByte(static_cast<std::uint8_t>((bits >> shift) & 0xffU));
    }
    return "pcm-fnv1a64:" + juce::String::toHexString(static_cast<juce::int64>(hash));
}

[[nodiscard]] bool writeWaveFile(const juce::File& file, const std::vector<float>& pcm,
                                 const std::uint32_t channels, const std::uint64_t frames,
                                 const double sampleRate, const CancellationToken& cancellation) {
    auto stream = file.createOutputStream();
    if (stream == nullptr || !stream->openedOk())
        return false;
    std::unique_ptr<juce::OutputStream> output{std::move(stream)};
    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(sampleRate)
                             .withNumChannels(static_cast<int>(channels))
                             .withBitsPerSample(32);
    auto writer = format.createWriterFor(output, options);
    if (writer == nullptr)
        return false;

    constexpr int blockFrames = 4096;
    juce::AudioBuffer<float> block{static_cast<int>(channels), blockFrames};
    std::uint64_t position = 0U;
    while (position < frames) {
        if (cancellation.isCancellationRequested())
            return false;
        const auto count = static_cast<int>(
            std::min<std::uint64_t>(frames - position, static_cast<std::uint64_t>(blockFrames)));
        for (int frame = 0; frame < count; ++frame)
            for (std::uint32_t channel = 0U; channel < channels; ++channel)
                block.setSample(
                    static_cast<int>(channel), frame,
                    pcm[static_cast<std::size_t>(
                        (position + static_cast<std::uint64_t>(frame)) * channels + channel)]);
        if (!writer->writeFromAudioSampleBuffer(block, 0, count))
            return false;
        position += static_cast<std::uint64_t>(count);
    }
    writer.reset();
    return true;
}

[[nodiscard]] bool existingWaveMatches(const juce::File& file, const std::uint32_t channels,
                                       const std::uint64_t frames, const double sampleRate) {
    juce::WavAudioFormat format;
    auto input = file.createInputStream();
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(input.release(), true));
    return reader != nullptr && reader->numChannels == channels &&
           reader->lengthInSamples == static_cast<juce::int64>(frames) &&
           std::abs(reader->sampleRate - sampleRate) < 0.5;
}

void removeIfExists(const juce::File& file) {
    if (file.existsAsFile())
        juce::ignoreUnused(file.deleteFile());
}
} // namespace

DerivedAssetRecipe DerivedAssetRenderer::recipeFor(const DerivedAssetRequest& request) {
    DerivedAssetRecipe recipe;
    if (request.source != nullptr) {
        recipe.sourceAssetUuid = request.source->metadata().assetUuid;
        recipe.sourceFingerprint = request.source->metadata().contentFingerprint;
    }
    recipe.operation = request.operation;
    switch (request.operation) {
    case DerivedAssetOperation::normalize:
        recipe.canonicalParameters =
            "targetDb=" + juce::String{static_cast<double>(request.normalizeTargetDecibels), 6};
        break;
    case DerivedAssetOperation::stereoToMono:
        recipe.canonicalParameters = "mix=(left+right)*0.5";
        break;
    case DerivedAssetOperation::fadeIn:
    case DerivedAssetOperation::fadeOut:
        recipe.canonicalParameters =
            "frames=" + juce::String{request.fadeDurationFrames} + ";curve=linear-v1";
        break;
    case DerivedAssetOperation::crop:
        recipe.canonicalParameters = "start=" + juce::String{request.playback.startFrame} +
                                     ";end=" + juce::String{request.playback.endFrame};
        break;
    }
    recipe.derivedAssetUuid =
        makeStableUuid(recipe.sourceAssetUuid + "|" + recipe.sourceFingerprint + "|" +
                       operationName(recipe.operation) + "|" +
                       juce::String{recipe.algorithmVersion} + "|" + recipe.canonicalParameters);
    return recipe;
}

juce::String DerivedAssetRenderer::findReusableAssetUuid(const ProjectState& state,
                                                         const DerivedAssetRecipe& recipe) {
    const auto found = std::find_if(
        state.derivedAssets.begin(), state.derivedAssets.end(), [&](const auto& record) {
            return record.parentAssetUuid == recipe.sourceAssetUuid &&
                   record.sourceFingerprint == recipe.sourceFingerprint &&
                   record.operationIdentifier == operationName(recipe.operation) &&
                   record.algorithmVersion == recipe.algorithmVersion &&
                   record.canonicalOperationParameters == recipe.canonicalParameters;
        });
    return found == state.derivedAssets.end() ? juce::String{} : found->derivedAssetUuid;
}

std::optional<JobHandle> DerivedAssetRenderer::submit(BackgroundJobSystem& jobs,
                                                      DerivedAssetRequest request) {
    const auto target = request.target;
    return jobs.submit(target, [request = std::move(request)](const CancellationToken& cancellation,
                                                              JobProgress& progress) {
        return render(request, cancellation, progress);
    });
}

std::shared_ptr<const JobResult> DerivedAssetRenderer::render(const DerivedAssetRequest& request,
                                                              const CancellationToken& cancellation,
                                                              JobProgress& progress) {
    if (request.source == nullptr)
        return failedResult(request.target, "Derived operation has no immutable source");
    const auto& sourceMetadata = request.source->metadata();
    if (sourceMetadata.assetUuid != request.sourceReference.uuid ||
        sourceMetadata.contentFingerprint != request.sourceReference.contentFingerprint)
        return failedResult(request.target, "Derived source identity changed");
    if (const auto validation =
            validateSamplePlaybackSettings(request.playback, sourceMetadata.frameCount);
        validation.failed())
        return failedResult(request.target, validation.getErrorMessage());
    if (request.outputDirectory == juce::File{} ||
        request.projectOwnedRelativeDirectory.trim().isEmpty())
        return failedResult(request.target, "Derived output directory is not configured");
    if (request.operation == DerivedAssetOperation::normalize &&
        (!std::isfinite(request.normalizeTargetDecibels) ||
         request.normalizeTargetDecibels < -24.0F || request.normalizeTargetDecibels > 0.0F))
        return failedResult(request.target, "Normalize target must be within -24..0 dBFS");

    const auto sourceChannels = sourceMetadata.channelCount;
    const auto sourceFrames = sourceMetadata.frameCount;
    const auto sourcePcm = request.source->interleavedPcm();
    auto outputChannels = sourceChannels;
    auto outputFrames = sourceFrames;
    if (request.operation == DerivedAssetOperation::stereoToMono)
        outputChannels = 1U;
    if (request.operation == DerivedAssetOperation::crop)
        outputFrames = request.playback.endFrame - request.playback.startFrame;
    const auto outputBytes = estimateDecodedBytes(outputFrames, outputChannels);
    if (outputBytes == std::numeric_limits<std::uint64_t>::max() ||
        outputBytes / sizeof(float) >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        return failedResult(request.target, "Derived output is too large for this platform");
    std::vector<float> output(static_cast<std::size_t>(outputBytes / sizeof(float)));

    if (request.operation == DerivedAssetOperation::stereoToMono) {
        for (std::uint64_t frame = 0U; frame < sourceFrames; ++frame) {
            if (frame % processingChunkSamples == 0U && cancellation.isCancellationRequested())
                return failedResult(request.target, "Derived operation was cancelled");
            const auto base = static_cast<std::size_t>(frame * sourceChannels);
            output[static_cast<std::size_t>(frame)] =
                sourceChannels == 1U
                    ? finiteSample(sourcePcm[base])
                    : (finiteSample(sourcePcm[base]) + finiteSample(sourcePcm[base + 1U])) * 0.5F;
        }
    } else {
        const auto firstSample =
            request.operation == DerivedAssetOperation::crop
                ? static_cast<std::size_t>(request.playback.startFrame * sourceChannels)
                : 0U;
        for (std::size_t index = 0U; index < output.size(); ++index) {
            if (index % processingChunkSamples == 0U && cancellation.isCancellationRequested())
                return failedResult(request.target, "Derived operation was cancelled");
            output[index] = finiteSample(sourcePcm[firstSample + index]);
        }
    }
    progress.set(0.25F);

    if (request.operation == DerivedAssetOperation::normalize) {
        auto peak = 0.0F;
        for (std::size_t index = 0U; index < output.size(); ++index) {
            if (index % processingChunkSamples == 0U && cancellation.isCancellationRequested())
                return failedResult(request.target, "Derived operation was cancelled");
            peak = std::max(peak, std::abs(output[index]));
        }
        if (peak > 0.0F) {
            const auto target = std::pow(10.0F, request.normalizeTargetDecibels / 20.0F);
            const auto gain = target / peak;
            for (auto& sample : output)
                sample = finiteSample(sample * gain);
        }
    } else if (request.operation == DerivedAssetOperation::fadeIn ||
               request.operation == DerivedAssetOperation::fadeOut) {
        const auto regionFrames = request.playback.endFrame - request.playback.startFrame;
        const auto duration = std::min(request.fadeDurationFrames, regionFrames);
        if (duration == 0U)
            return failedResult(request.target, "Fade duration must contain at least one frame");
        const auto first = request.operation == DerivedAssetOperation::fadeIn
                               ? request.playback.startFrame
                               : request.playback.endFrame - duration;
        for (std::uint64_t offset = 0U; offset < duration; ++offset) {
            const auto proportion = duration == 1U
                                        ? 0.0F
                                        : static_cast<float>(static_cast<double>(offset) /
                                                             static_cast<double>(duration - 1U));
            const auto gain =
                request.operation == DerivedAssetOperation::fadeIn ? proportion : 1.0F - proportion;
            for (std::uint32_t channel = 0U; channel < outputChannels; ++channel) {
                const auto index =
                    static_cast<std::size_t>((first + offset) * outputChannels + channel);
                output[index] = finiteSample(output[index] * gain);
            }
        }
    }
    if (cancellation.isCancellationRequested())
        return failedResult(request.target, "Derived operation was cancelled");
    progress.set(0.55F);

    const auto recipe = recipeFor(request);
    const auto filename = "derived-" + recipe.derivedAssetUuid + ".wav";
    const auto finalFile = request.outputDirectory.getChildFile(filename);
    const auto temporaryFile =
        request.outputDirectory.getChildFile(filename + ".part-" + juce::Uuid{}.toString());
    if (request.outputDirectory.createDirectory().failed())
        return failedResult(request.target, "Derived output directory could not be created");

    bool wroteNewFile = false;
    if (finalFile.existsAsFile()) {
        if (!existingWaveMatches(finalFile, outputChannels, outputFrames,
                                 sourceMetadata.sampleRate))
            return failedResult(request.target,
                                "Existing deterministic derived file does not match its recipe");
    } else {
        if (!writeWaveFile(temporaryFile, output, outputChannels, outputFrames,
                           sourceMetadata.sampleRate, cancellation)) {
            removeIfExists(temporaryFile);
            return failedResult(request.target, cancellation.isCancellationRequested()
                                                    ? "Derived operation was cancelled"
                                                    : "Derived WAV writing failed");
        }
        if (cancellation.isCancellationRequested()) {
            removeIfExists(temporaryFile);
            return failedResult(request.target, "Derived operation was cancelled");
        }
        if (!temporaryFile.moveFileTo(finalFile)) {
            removeIfExists(temporaryFile);
            return failedResult(request.target, "Derived WAV publication failed");
        }
        wroteNewFile = true;
    }
    progress.set(0.85F);

    const auto outputFingerprint = fingerprintPcm(output, outputChannels, outputFrames);
    SampleAssetMetadata metadata;
    metadata.assetUuid = recipe.derivedAssetUuid;
    metadata.displayName = finalFile.getFileNameWithoutExtension();
    metadata.sampleRate = sourceMetadata.sampleRate;
    metadata.channelCount = outputChannels;
    metadata.frameCount = outputFrames;
    metadata.sourceAssetUuid = sourceMetadata.assetUuid;
    metadata.provenance = "derived:" + juce::String{operationName(request.operation)};
    metadata.sourcePath = finalFile.getFullPathName();
    metadata.sourceFormat = "WAV";
    metadata.contentFingerprint = outputFingerprint;
    metadata.sourceFileBytes = static_cast<std::uint64_t>(finalFile.getSize());
    metadata.modificationTimeMilliseconds = finalFile.getLastModificationTime().toMilliseconds();
    metadata.durationSeconds = static_cast<double>(outputFrames) / sourceMetadata.sampleRate;
    auto asset = SampleAsset::create(std::move(metadata), std::move(output));

    ExternalAssetReference reference;
    reference.uuid = recipe.derivedAssetUuid;
    reference.originalPath = finalFile.getFullPathName();
    reference.originalName = finalFile.getFileName();
    reference.format = "WAV";
    reference.contentFingerprint = outputFingerprint;
    reference.sourceFileBytes = static_cast<std::uint64_t>(finalFile.getSize());
    reference.modificationTimeMilliseconds = finalFile.getLastModificationTime().toMilliseconds();
    reference.channels = outputChannels;
    reference.sourceSampleRate = sourceMetadata.sampleRate;
    reference.frameCount = outputFrames;
    reference.decodedBytes = static_cast<std::uint64_t>(asset->decodedBytes());

    DerivedAssetRecord provenance;
    provenance.derivedAssetUuid = recipe.derivedAssetUuid;
    provenance.parentAssetUuid = recipe.sourceAssetUuid;
    provenance.sourceFingerprint = recipe.sourceFingerprint;
    provenance.operationIdentifier = operationName(recipe.operation);
    provenance.algorithmVersion = recipe.algorithmVersion;
    provenance.canonicalOperationParameters = recipe.canonicalParameters;
    provenance.outputFingerprint = outputFingerprint;
    provenance.creationMetadata = "PadFlow deterministic derived renderer v1";
    provenance.projectOwnedRelativePath = request.projectOwnedRelativeDirectory + "/" + filename;

    auto playback = request.playback;
    if (request.operation == DerivedAssetOperation::crop) {
        playback.loopStartFrame -= request.playback.startFrame;
        playback.loopEndFrame -= request.playback.startFrame;
        playback.startFrame = 0U;
        playback.endFrame = outputFrames;
    }
    auto payload = std::make_shared<const DerivedAssetPayload>(DerivedAssetPayload{
        std::move(asset), std::move(reference), std::move(provenance), playback,
        sourceMetadata.assetUuid, request.globalPadIndex, request.layerIndex, wroteNewFile});
    progress.set(1.0F);
    return std::make_shared<const JobResult>(
        JobResult{request.target, true, "Derived asset complete", std::move(payload)});
}

juce::Result DerivedAssetRenderer::commit(const JobResult& result,
                                          ApplicationController& controller,
                                          SampleAssetRegistry& registry) {
    if (!result.succeeded)
        return juce::Result::fail(result.message);
    const auto* payload = static_cast<const DerivedAssetPayload*>(result.immutablePayload.get());
    if (payload == nullptr || payload->asset == nullptr)
        return juce::Result::fail("Derived operation returned no immutable asset");
    const auto cleanup = [&] {
        if (payload->wroteNewFile)
            removeIfExists(juce::File{payload->reference.originalPath});
    };
    if (!controller.isCurrentJobTarget(result.target)) {
        cleanup();
        return juce::Result::fail("Derived asset completion is stale");
    }

    const auto previous = registry.find(payload->asset->metadata().assetUuid);
    if (!registry.publish(payload->asset)) {
        cleanup();
        return juce::Result::fail("Derived asset exceeds the decoded-memory budget");
    }
    const auto commitResult =
        controller.commitDerivedLayer(result.target, payload->globalPadIndex, payload->layerIndex,
                                      payload->expectedSourceAssetUuid, payload->reference,
                                      payload->provenance, payload->playback);
    if (commitResult.failed()) {
        if (previous != nullptr)
            juce::ignoreUnused(registry.publish(previous));
        else
            juce::ignoreUnused(registry.erase(payload->asset->metadata().assetUuid));
        cleanup();
        return commitResult;
    }
    return juce::Result::ok();
}
} // namespace padflow
