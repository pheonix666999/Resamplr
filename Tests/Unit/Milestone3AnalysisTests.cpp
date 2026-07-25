#include "Chopping/ChoppingSession.h"
#include "Chopping/LazyMarkerCapture.h"
#include "Chopping/TransientAnalysis.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace padflow {
namespace {
std::shared_ptr<const SampleAsset> fixture(const std::uint32_t channels,
                                           const std::vector<std::int64_t>& transients,
                                           const bool constant = false) {
    constexpr auto frames = std::uint64_t{128U};
    std::vector<float> pcm(static_cast<std::size_t>(frames) * channels, constant ? 0.5F : 0.0F);
    for (const auto frame : transients)
        for (std::uint32_t channel = 0U; channel < channels; ++channel)
            pcm[static_cast<std::size_t>(frame) * channels + channel] =
                channel == 0U ? 1.0F : 0.75F;
    return SampleAsset::create({"analysis-asset",
                                "Synthetic transient fixture",
                                48000.0,
                                channels,
                                frames,
                                {},
                                {},
                                {},
                                {},
                                "analysis-fingerprint",
                                0U,
                                0,
                                0.0},
                               std::move(pcm));
}

TransientAnalysisRequest analysisRequest(std::shared_ptr<const SampleAsset> asset,
                                         const TransientAnalysisParameters parameters = {}) {
    return {{"analysis-project", "analysis-asset", 9U, 0, JobKind::transientAnalysis},
            {"analysis-set", "analysis-asset", "analysis-fingerprint", "analysis-layer", 0, 128, 1,
             SliceRemainderPolicy::include, SliceDisplayUnit::frames},
            parameters,
            std::move(asset)};
}

std::shared_ptr<const JobResult> analyse(const TransientAnalysisRequest& request,
                                         CancellationToken& token) {
    JobProgress progress;
    return TransientAnalysis::analyse(request, token, progress);
}

std::vector<std::int64_t> internalBoundaries(const SliceSet& set) {
    std::vector<std::int64_t> result;
    for (std::size_t index = 1U; index < set.slices.size(); ++index)
        result.push_back(set.slices[index].startFrame);
    return result;
}

ChoppingSessionTarget sessionTarget() {
    return {"lazy-session", "lazy-project",     "lazy-pad", "lazy-layer", 3U,
            "lazy-asset",   "lazy-fingerprint", 10,         110};
}
} // namespace

class Milestone3AnalysisTests final : public juce::UnitTest {
  public:
    Milestone3AnalysisTests()
        : juce::UnitTest("Milestone 3 transient and lazy workflows", "PadFlow") {}

    void runTest() override {
        beginTest("CHOP-M3-060 through CHOP-M3-065 deterministic synthetic analysis");
        CancellationToken token;
        auto result = analyse(analysisRequest(fixture(1U, {32})), token);
        expect(result != nullptr && result->succeeded);
        const auto* payload =
            static_cast<const TransientAnalysisPayload*>(result->immutablePayload.get());
        expect(payload != nullptr);
        if (payload != nullptr)
            expect(internalBoundaries(payload->sliceSet) == std::vector<std::int64_t>{32});

        result = analyse(analysisRequest(fixture(2U, {20, 52, 91})), token);
        payload = static_cast<const TransientAnalysisPayload*>(result->immutablePayload.get());
        expect(result->succeeded && payload != nullptr);
        if (payload != nullptr)
            expect(internalBoundaries(payload->sliceSet) ==
                   std::vector<std::int64_t>({20, 52, 91}));

        for (const auto& asset : {fixture(1U, {}), fixture(1U, {}, true), fixture(2U, {})}) {
            result = analyse(analysisRequest(asset), token);
            payload = static_cast<const TransientAnalysisPayload*>(result->immutablePayload.get());
            expect(result->succeeded && payload != nullptr);
            if (payload != nullptr)
                expectEquals(payload->sliceSet.slices.size(), std::size_t{1U});
        }

        beginTest("CHOP-M3-066 through CHOP-M3-069 sensitivity, spacing, and look-back");
        auto low = analysisRequest(fixture(1U, {20, 40, 70}));
        auto high = low;
        low.parameters.sensitivity = 0.0F;
        high.parameters.sensitivity = 1.0F;
        auto lowResult = analyse(low, token);
        auto highResult = analyse(high, token);
        const auto* lowPayload =
            static_cast<const TransientAnalysisPayload*>(lowResult->immutablePayload.get());
        const auto* highPayload =
            static_cast<const TransientAnalysisPayload*>(highResult->immutablePayload.get());
        expect(lowPayload != nullptr && highPayload != nullptr);
        if (lowPayload != nullptr && highPayload != nullptr)
            expect(highPayload->sliceSet.slices.size() >= lowPayload->sliceSet.slices.size());

        auto spaced = analysisRequest(fixture(1U, {20, 23, 70}));
        spaced.parameters.minimumSliceFrames = 10;
        spaced.parameters.attackLookBackFrames = 30;
        result = analyse(spaced, token);
        payload = static_cast<const TransientAnalysisPayload*>(result->immutablePayload.get());
        expect(result->succeeded && payload != nullptr);
        if (payload != nullptr) {
            const auto markers = internalBoundaries(payload->sliceSet);
            expect(std::is_sorted(markers.begin(), markers.end()));
            expect(std::adjacent_find(markers.begin(), markers.end()) == markers.end());
        }

        beginTest("CHOP-M3-070, CHOP-M3-071, THREAD-M3-001 and THREAD-M3-002");
        CancellationToken cancelled;
        cancelled.cancel();
        result = analyse(analysisRequest(fixture(1U, {32})), cancelled);
        expect(result != nullptr && !result->succeeded);
        ChoppingSession session;
        expect(session
                   .begin({"analysis-session", "analysis-project", "analysis-pad", "analysis-layer",
                           9U, "analysis-asset", "analysis-fingerprint", 0, 128})
                   .wasOk());
        CancellationToken currentToken;
        result = analyse(analysisRequest(fixture(1U, {32})), currentToken);
        expect(session.acceptTransientResult(*result).wasOk());
        auto stale = *result;
        stale.target.targetRevision = 10U;
        const auto beforeStale = *session.provisionalSliceSet();
        expect(session.acceptTransientResult(stale).failed());
        expect(*session.provisionalSliceSet() == beforeStale);

        beginTest("CHOP-M3-080 through CHOP-M3-090 bounded lazy capture");
        ChoppingSession lazySession;
        expect(lazySession.begin(sessionTarget()).wasOk());
        LazyMarkerCapture capture;
        expect(capture.start({10, 110, 2, 0}).wasOk());
        expect(capture.captureFromAudioThread(70, LazyMarkerSource::mouse));
        expect(capture.captureFromAudioThread(30, LazyMarkerSource::keyboard));
        expect(capture.captureFromAudioThread(50, LazyMarkerSource::midi));
        auto drained = capture.drainToSession(lazySession);
        expectEquals(drained.accepted, std::size_t{3U});
        expect(internalBoundaries(*lazySession.provisionalSliceSet()) ==
               std::vector<std::int64_t>({30, 50, 70}));
        expect(capture.captureFromAudioThread(50, LazyMarkerSource::control));
        expect(capture.captureFromAudioThread(51, LazyMarkerSource::control));
        drained = capture.drainToSession(lazySession);
        expectEquals(drained.accepted, std::size_t{0U});
        expectEquals(drained.rejected, std::size_t{2U});
        capture.stop();
        expect(!capture.captureFromAudioThread(80, LazyMarkerSource::mouse));

        LazyMarkerCapture quantized;
        ChoppingSession quantizedSession;
        expect(quantizedSession.begin(sessionTarget()).wasOk());
        expect(quantized.start({10, 110, 1, 10}).wasOk());
        expect(quantized.captureFromAudioThread(26, LazyMarkerSource::control));
        expect(quantized.captureFromAudioThread(42, LazyMarkerSource::control));
        drained = quantized.drainToSession(quantizedSession);
        expectEquals(drained.accepted, std::size_t{2U});
        expect(internalBoundaries(*quantizedSession.provisionalSliceSet()) ==
               std::vector<std::int64_t>({30, 40}));

        beginTest("REGRESSION-M3-001 large-frame lazy quantization cannot overflow");
        const auto maximum = std::numeric_limits<std::int64_t>::max();
        LazyMarkerCapture largeFrame;
        ChoppingSession largeSession;
        expect(largeSession
                   .begin({"large-session", "lazy-project", "lazy-pad", "lazy-layer", 3U,
                           "lazy-asset", "lazy-fingerprint", maximum - 100, maximum})
                   .wasOk());
        expect(largeFrame.start({maximum - 100, maximum, 1, 64}).wasOk());
        expect(largeFrame.captureFromAudioThread(maximum - 1, LazyMarkerSource::control));
        drained = largeFrame.drainToSession(largeSession);
        expectEquals(drained.accepted, std::size_t{0U});
        expectEquals(drained.rejected, std::size_t{1U});

        LazyMarkerCapture full;
        expect(full.start({10, 10000, 1, 0}).wasOk());
        for (std::int64_t frame = 0; frame < 256; ++frame)
            expect(full.captureFromAudioThread(frame + 20, LazyMarkerSource::control));
        expect(!full.captureFromAudioThread(500, LazyMarkerSource::control));
        expectEquals(full.queuedEventCount(), std::size_t{256U});
        expectEquals(full.overflowCount(), std::uint64_t{1U});
        full.stop();
        full.resetWhenQuiescent();
        expectEquals(full.queuedEventCount(), std::size_t{0U});
    }
};

static Milestone3AnalysisTests milestone3AnalysisTests;
} // namespace padflow
