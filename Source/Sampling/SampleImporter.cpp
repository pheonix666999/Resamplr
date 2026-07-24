#include "SampleImporter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace padflow {
namespace {
juce::String fingerprintFile(const juce::File& file, const CancellationToken& cancellation) {
    juce::FileInputStream input{file};
    if (!input.openedOk())
        return {};

    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = 14695981039346656037ULL;
    std::array<char, 16384U> bytes{};
    for (;;) {
        if (cancellation.isCancellationRequested())
            return {};
        const auto count = input.read(bytes.data(), static_cast<int>(bytes.size()));
        if (count <= 0)
            break;
        for (int index = 0; index < count; ++index) {
            hash ^= static_cast<std::uint64_t>(
                static_cast<unsigned char>(bytes[static_cast<std::size_t>(index)]));
            hash *= prime;
        }
    }
    return "fnv1a64:" + juce::String::toHexString(static_cast<juce::int64>(hash));
}

std::shared_ptr<const JobResult> failedResult(const JobSpec& target, juce::String message) {
    return std::make_shared<const JobResult>(JobResult{target, false, std::move(message), {}});
}
} // namespace

std::optional<JobHandle> SampleImporter::submit(BackgroundJobSystem& jobs,
                                                SampleImportRequest request) {
    const auto target = request.target;
    return jobs.submit(target, [request = std::move(request)](const CancellationToken& cancellation,
                                                              JobProgress& progress) {
        return decode(request, cancellation, progress);
    });
}

std::shared_ptr<const JobResult> SampleImporter::decode(const SampleImportRequest& request,
                                                        const CancellationToken& cancellation,
                                                        JobProgress& progress) {
    if (!request.sourceFile.existsAsFile())
        return failedResult(request.target, "Sample source does not exist");
    if (request.assetUuid.trim().isEmpty())
        return failedResult(request.target, "Sample asset UUID is empty");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(request.sourceFile));
    if (reader == nullptr)
        return failedResult(request.target, "Sample format is corrupt or unsupported");
    if (reader->numChannels == 0U || reader->numChannels > 2U || reader->lengthInSamples <= 0 ||
        !std::isfinite(reader->sampleRate) || reader->sampleRate <= 0.0)
        return failedResult(request.target, "Sample must contain one or two valid channels");

    const auto frames = static_cast<std::uint64_t>(reader->lengthInSamples);
    const auto channels = static_cast<std::uint32_t>(reader->numChannels);
    const auto decodedBytes = estimateDecodedBytes(frames, channels);
    if (decodedBytes == std::numeric_limits<std::uint64_t>::max() ||
        decodedBytes > request.maximumDecodedBytes)
        return failedResult(request.target, "Decoded sample exceeds the configured memory budget");
    if (decodedBytes / sizeof(float) >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        return failedResult(request.target, "Decoded sample is too large for this platform");

    std::vector<float> interleaved(static_cast<std::size_t>(decodedBytes / sizeof(float)));
    constexpr int chunkFrames = 4096;
    juce::AudioBuffer<float> chunk{static_cast<int>(channels), chunkFrames};
    std::uint64_t position = 0U;
    while (position < frames) {
        if (cancellation.isCancellationRequested())
            return failedResult(request.target, "Sample import was cancelled");

        const auto remaining = frames - position;
        const auto count = static_cast<int>(
            std::min<std::uint64_t>(remaining, static_cast<std::uint64_t>(chunkFrames)));
        chunk.clear();
        if (!reader->read(&chunk, 0, count, static_cast<juce::int64>(position), true, true))
            return failedResult(request.target, "Sample decoding failed");

        for (int frame = 0; frame < count; ++frame)
            for (std::uint32_t channel = 0U; channel < channels; ++channel)
                interleaved[static_cast<std::size_t>(
                    (position + static_cast<std::uint64_t>(frame)) * channels + channel)] =
                    chunk.getSample(static_cast<int>(channel), frame);
        position += static_cast<std::uint64_t>(count);
        progress.set(
            static_cast<float>(static_cast<double>(position) / static_cast<double>(frames)));
    }

    const auto fingerprint = fingerprintFile(request.sourceFile, cancellation);
    if (fingerprint.isEmpty())
        return failedResult(request.target, cancellation.isCancellationRequested()
                                                ? "Sample import was cancelled"
                                                : "Sample fingerprint could not be read");

    SampleAssetMetadata metadata;
    metadata.assetUuid = request.assetUuid;
    metadata.displayName = request.sourceFile.getFileNameWithoutExtension();
    metadata.sampleRate = reader->sampleRate;
    metadata.channelCount = channels;
    metadata.frameCount = frames;
    metadata.provenance = "external-import";
    metadata.sourcePath = request.sourceFile.getFullPathName();
    metadata.sourceFormat =
        request.sourceFile.getFileExtension().trimCharactersAtStart(".").toUpperCase();
    metadata.contentFingerprint = fingerprint;
    metadata.sourceFileBytes = static_cast<std::uint64_t>(request.sourceFile.getSize());
    metadata.modificationTimeMilliseconds =
        request.sourceFile.getLastModificationTime().toMilliseconds();
    metadata.durationSeconds = static_cast<double>(frames) / reader->sampleRate;

    auto asset = SampleAsset::create(std::move(metadata), std::move(interleaved));
    const auto& saved = asset->metadata();
    ExternalAssetReference reference;
    reference.uuid = saved.assetUuid;
    reference.originalPath = saved.sourcePath;
    reference.originalName = request.sourceFile.getFileName();
    reference.format = saved.sourceFormat;
    reference.contentFingerprint = saved.contentFingerprint;
    reference.sourceFileBytes = saved.sourceFileBytes;
    reference.modificationTimeMilliseconds = saved.modificationTimeMilliseconds;
    reference.channels = saved.channelCount;
    reference.sourceSampleRate = saved.sampleRate;
    reference.frameCount = saved.frameCount;
    reference.decodedBytes = static_cast<std::uint64_t>(asset->decodedBytes());
    reference.missing = false;

    auto payload = std::make_shared<const SampleImportPayload>(SampleImportPayload{
        std::move(asset), std::move(reference), request.globalPadIndex, request.layerIndex});
    progress.set(1.0F);
    return std::make_shared<const JobResult>(
        JobResult{request.target, true, "Sample import complete", std::move(payload)});
}

juce::Result SampleImporter::commit(const JobResult& result, ApplicationController& controller,
                                    SampleAssetRegistry& registry) {
    if (!result.succeeded)
        return juce::Result::fail(result.message);
    if (!controller.isCurrentJobTarget(result.target))
        return juce::Result::fail("Sample import completion is stale");
    const auto* payload = static_cast<const SampleImportPayload*>(result.immutablePayload.get());
    if (payload == nullptr || payload->asset == nullptr)
        return juce::Result::fail("Sample import returned no immutable asset");

    const auto previous = registry.find(payload->asset->metadata().assetUuid);
    if (!registry.publish(payload->asset))
        return juce::Result::fail("Decoded sample registry budget would be exceeded");

    const auto modelResult = controller.commitImportedLayer(
        result.target, payload->globalPadIndex, payload->layerIndex, payload->reference);
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
