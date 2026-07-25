#include "App/ApplicationController.h"
#include "Sampling/SampleAsset.h"
#include "Sampling/WaveformCache.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace padflow {
namespace {
std::shared_ptr<const SampleAsset> makeAsset(juce::String uuid, juce::String fingerprint,
                                             const std::uint32_t channels,
                                             const std::uint64_t frames) {
    std::vector<float> pcm(static_cast<std::size_t>(frames * channels));
    for (std::uint64_t frame = 0U; frame < frames; ++frame)
        for (std::uint32_t channel = 0U; channel < channels; ++channel) {
            const auto phase = static_cast<int>(frame % 17U) - 8;
            pcm[static_cast<std::size_t>(frame * channels + channel)] =
                static_cast<float>(phase * static_cast<int>(channel + 1U)) / 8.0F;
        }

    SampleAssetMetadata metadata;
    metadata.assetUuid = std::move(uuid);
    metadata.displayName = "Waveform fixture";
    metadata.sampleRate = 48000.0;
    metadata.channelCount = channels;
    metadata.frameCount = frames;
    metadata.provenance = "synthetic-test";
    metadata.contentFingerprint = std::move(fingerprint);
    metadata.durationSeconds = static_cast<double>(frames) / metadata.sampleRate;
    return SampleAsset::create(std::move(metadata), std::move(pcm));
}

std::shared_ptr<const WaveformCache> generate(const SampleAsset& asset) {
    CancellationToken cancellation;
    JobProgress progress;
    juce::String error;
    return WaveformCache::generate(asset, cancellation, progress, error);
}
} // namespace

class Milestone2WaveformTests final : public juce::UnitTest {
  public:
    Milestone2WaveformTests() : juce::UnitTest("Milestone 2 waveform cache", "PadFlow") {}

    void runTest() override {
        beginTest("WAVE-M2-001 mono cache");
        const auto mono = makeAsset("wave-mono", "fingerprint-mono", 1U, 130U);
        const auto monoCache = generate(*mono);
        expect(monoCache != nullptr);
        if (monoCache != nullptr) {
            expectEquals(static_cast<int>(monoCache->key().channelCount), 1);
            expectEquals(static_cast<juce::int64>(monoCache->key().sourceFrameCount),
                         juce::int64{130});
            expectEquals(static_cast<int>(monoCache->levels().front().peakBlockCount()), 3);
            const auto* first = monoCache->levels().front().peak(0U, 0U);
            expect(first != nullptr);
            if (first != nullptr) {
                expectWithinAbsoluteError(first->minimum, -1.0F, 0.000001F);
                expectWithinAbsoluteError(first->maximum, 1.0F, 0.000001F);
            }
        }

        beginTest("WAVE-M2-002 stereo cache");
        const auto stereo = makeAsset("wave-stereo", "fingerprint-stereo", 2U, 65U);
        const auto stereoCache = generate(*stereo);
        expect(stereoCache != nullptr);
        if (stereoCache != nullptr) {
            const auto& level = stereoCache->levels().front();
            expectEquals(static_cast<int>(level.peakBlockCount()), 2);
            const auto* left = level.peak(0U, 0U);
            const auto* right = level.peak(0U, 1U);
            expect(left != nullptr && right != nullptr);
            if (left != nullptr && right != nullptr)
                expectWithinAbsoluteError(right->maximum, left->maximum * 2.0F, 0.000001F);
        }

        beginTest("WAVE-M2-003 very short asset");
        const auto shortAsset = makeAsset("wave-short", "fingerprint-short", 1U, 1U);
        const auto shortCache = generate(*shortAsset);
        expect(shortCache != nullptr);
        if (shortCache != nullptr) {
            expectEquals(static_cast<int>(shortCache->levels().size()), 1);
            expectEquals(static_cast<int>(shortCache->levels().front().peakBlockCount()), 1);
        }

        beginTest("WAVE-M2-004 multi-resolution summaries");
        const auto longAsset = makeAsset("wave-long", "fingerprint-long", 2U, 4096U);
        const auto longCache = generate(*longAsset);
        expect(longCache != nullptr);
        if (longCache != nullptr) {
            expectEquals(static_cast<int>(longCache->levels().size()), 4);
            expectEquals(static_cast<int>(longCache->levels()[0].peakBlockCount()), 64);
            expectEquals(static_cast<int>(longCache->levels()[1].peakBlockCount()), 16);
            expectEquals(static_cast<int>(longCache->levels()[2].peakBlockCount()), 4);
            expectEquals(static_cast<int>(longCache->levels()[3].peakBlockCount()), 1);
            expectEquals(static_cast<juce::int64>(longCache->levels()[3].coveredSourceFrames),
                         juce::int64{4096});
        }

        beginTest("WAVE-M2-005 cache fingerprint invalidation");
        WaveformCacheRegistry registry{1024U * 1024U};
        expect(stereoCache != nullptr && registry.publish(stereoCache));
        if (stereoCache != nullptr) {
            expect(registry.find(stereoCache->key()) != nullptr);
            auto invalidKey = stereoCache->key();
            invalidKey.sourceFingerprint = "changed";
            expect(registry.find(invalidKey) == nullptr);
            expect(registry.invalidate(stereoCache->key().assetUuid));
            expectEquals(static_cast<int>(registry.uniqueCacheCount()), 0);
        }

        beginTest("WAVE-M2-006 stale result discarded");
        ApplicationController controller;
        controller.createEmptyProject("Waveform", "waveform-project");
        ExternalAssetReference reference;
        reference.uuid = stereo->metadata().assetUuid;
        reference.originalPath = "synthetic.wav";
        reference.originalName = "synthetic.wav";
        reference.format = "WAV";
        reference.contentFingerprint = stereo->metadata().contentFingerprint;
        reference.channels = stereo->metadata().channelCount;
        reference.sourceSampleRate = stereo->metadata().sampleRate;
        reference.frameCount = stereo->metadata().frameCount;
        reference.decodedBytes = static_cast<std::uint64_t>(stereo->decodedBytes());
        reference.missing = false;
        const JobSpec importTarget{controller.project().uuid(), controller.project().pad(0U).uuid,
                                   controller.project().revision(), 0, JobKind::sampleImport};
        expect(controller.commitImportedLayer(importTarget, 0U, 0U, reference).wasOk());

        const WaveformCacheRequest request{JobSpec{controller.project().uuid(), reference.uuid,
                                                   controller.project().revision(), 0,
                                                   JobKind::waveformCache},
                                           stereo};
        CancellationToken buildCancellation;
        JobProgress buildProgress;
        const auto result =
            WaveformCacheGenerator::build(request, buildCancellation, buildProgress);
        expect(result != nullptr && result->succeeded);
        auto ui = controller.project().state().ui;
        ui.selectedBank = 1U;
        expect(controller.setUiState(ui).wasOk());
        WaveformCacheRegistry staleRegistry{1024U * 1024U};
        expect(result != nullptr &&
               WaveformCacheGenerator::commit(*result, controller, staleRegistry).failed());
        expectEquals(static_cast<int>(staleRegistry.uniqueCacheCount()), 0);

        beginTest("WAVE-M2-007 cancellation");
        CancellationToken cancelled;
        cancelled.cancel();
        JobProgress cancelledProgress;
        const auto cancelledResult =
            WaveformCacheGenerator::build(request, cancelled, cancelledProgress);
        expect(cancelledResult != nullptr && !cancelledResult->succeeded);

        beginTest("WAVE-M2-008 bounded memory accounting");
        expect(longCache != nullptr && longCache->memoryBytes() > 0U);
        if (longCache != nullptr) {
            WaveformCacheRegistry exactRegistry{
                static_cast<std::uint64_t>(longCache->memoryBytes())};
            expect(exactRegistry.publish(longCache));
            expectEquals(static_cast<juce::int64>(exactRegistry.usedBytes()),
                         static_cast<juce::int64>(longCache->memoryBytes()));
            expect(exactRegistry.publish(longCache));
            expectEquals(static_cast<int>(exactRegistry.uniqueCacheCount()), 1);
            WaveformCacheRegistry tooSmall{
                static_cast<std::uint64_t>(longCache->memoryBytes() - 1U)};
            expect(!tooSmall.publish(longCache));
            expectEquals(static_cast<juce::int64>(tooSmall.usedBytes()), juce::int64{0});
        }
    }
};

static Milestone2WaveformTests milestone2WaveformTests;
} // namespace padflow
