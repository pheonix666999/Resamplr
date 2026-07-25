#include "Audio/SliceAuditionController.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace padflow {
namespace {
std::shared_ptr<const SampleAsset> makePreviewAsset() {
    SampleAssetMetadata metadata;
    metadata.assetUuid = "slice-preview-asset";
    metadata.displayName = "Slice preview fixture";
    metadata.sampleRate = 48000.0;
    metadata.channelCount = 1U;
    metadata.frameCount = 32U;
    std::vector<float> pcm(32U, 8.0F);
    std::fill(pcm.begin() + 8, pcm.begin() + 24, 0.25F);
    return SampleAsset::create(std::move(metadata), std::move(pcm));
}
} // namespace

class Milestone3AuditionTests final : public juce::UnitTest {
  public:
    Milestone3AuditionTests() : juce::UnitTest("Milestone 3 slice audition", "PadFlow") {}

    void runTest() override {
        const auto asset = makePreviewAsset();
        expect(asset != nullptr);
        if (asset == nullptr)
            return;

        PreviewPlayer player;
        player.prepare(48000.0);
        player.setVolume(1.0F);
        SliceAuditionController audition{player};
        const SliceRegion region{"preview-slice", 8, 24, "Slice", std::nullopt};

        beginTest("Bounded selected audition never interpolates outside slice boundaries");
        expect(audition.startSelected(asset, region));
        std::array<float, 32U> left{};
        std::array<float, 32U> right{};
        player.processAdd(left.data(), right.data(), left.size());
        expect(std::all_of(left.begin(), left.end(), [](const auto value) {
            return std::isfinite(value) && std::abs(value) <= 0.25001F;
        }));
        expect(!player.isActive());
        audition.service();

        beginTest("Reverse and one-frame slice audition remain finite and bounded");
        const SliceRegion oneFrame{"one-frame-preview", 12, 13, "One frame", std::nullopt};
        left.fill(0.0F);
        right.fill(0.0F);
        expect(audition.startSelected(asset, oneFrame, true));
        player.processAdd(left.data(), right.data(), left.size());
        expect(std::all_of(left.begin(), left.end(), [](const auto value) {
            return std::isfinite(value) && std::abs(value) <= 0.25001F;
        }));
        audition.service();

        beginTest("Sequential audition advances in stable slice order");
        const std::vector<SliceRegion> sequence{
            {"sequence-1", 8, 10, "One", std::nullopt},
            {"sequence-2", 20, 23, "Two", std::nullopt},
        };
        expect(audition.startSequential(asset, sequence));
        left.fill(0.0F);
        right.fill(0.0F);
        player.processAdd(left.data(), right.data(), 4U);
        expect(!player.isActive());
        audition.service();
        player.processAdd(left.data(), right.data(), 4U);
        expect(!player.isActive());
        audition.service();
        expect(!audition.active());

        beginTest("Lazy source position is published without callback allocation");
        expect(audition.startLazy(asset, 8, 24));
        left.fill(0.0F);
        right.fill(0.0F);
        player.processAdd(left.data(), right.data(), 5U);
        expect(audition.sourceFramePosition() >= 8U);
        expect(audition.sourceFramePosition() < 24U);
        expect(audition.stop());
        player.processAdd(left.data(), right.data(), 1U);
        audition.service();
        audition.clearWhenQuiescent();
    }
};

static Milestone3AuditionTests milestone3AuditionTests;
} // namespace padflow
