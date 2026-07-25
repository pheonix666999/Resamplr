#include "App/ApplicationController.h"
#include "Sampling/SampleAsset.h"
#include "Sampling/SampleImporter.h"
#include "Utilities/BackgroundJobSystem.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <memory>

namespace padflow {
namespace {
bool writeFixture(const juce::File& file, const double sampleRate, const int channels,
                  const int frames) {
    std::unique_ptr<juce::AudioFormat> format;
    const auto extension = file.getFileExtension().toLowerCase();
    if (extension == ".wav")
        format = std::make_unique<juce::WavAudioFormat>();
    else if (extension == ".aiff")
        format = std::make_unique<juce::AiffAudioFormat>();
    else if (extension == ".flac")
        format = std::make_unique<juce::FlacAudioFormat>();
    else
        return false;

    auto stream = file.createOutputStream();
    if (stream == nullptr || !stream->openedOk())
        return false;
    std::unique_ptr<juce::OutputStream> output{std::move(stream)};
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(sampleRate)
                             .withNumChannels(channels)
                             .withBitsPerSample(24);
    auto writer = format->createWriterFor(output, options);
    if (writer == nullptr)
        return false;

    juce::AudioBuffer<float> buffer{channels, frames};
    for (int channel = 0; channel < channels; ++channel)
        for (int frame = 0; frame < frames; ++frame)
            buffer.setSample(channel, frame,
                             static_cast<float>((channel + 1) * (frame + 1)) /
                                 static_cast<float>((channels + 1) * (frames + 1)));
    return writer->writeFromAudioSampleBuffer(buffer, 0, frames);
}

std::shared_ptr<const JobResult> awaitCompletion(BackgroundJobSystem& jobs) {
    std::shared_ptr<const JobResult> result;
    for (int attempt = 0; attempt < 1000 && result == nullptr; ++attempt) {
        juce::Thread::sleep(2);
        result = jobs.tryPopCompleted();
    }
    return result;
}

SampleImportRequest makeRequest(const ApplicationController& controller, const juce::File& file,
                                juce::String assetUuid, const std::size_t layerIndex = 0U) {
    return {
        JobSpec{controller.project().uuid(), controller.project().pad(0U).uuid,
                controller.project().revision(), 0, JobKind::sampleImport},
        file,
        std::move(assetUuid),
        0U,
        layerIndex,
        defaultMilestone1DecodedBudgetBytes,
    };
}
} // namespace

class Milestone1AssetTests final : public juce::UnitTest {
  public:
    Milestone1AssetTests() : juce::UnitTest("Milestone 1 assets", "PadFlow") {}

    void runTest() override {
        const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getNonexistentChildFile("padflow-m1-assets", {}, true);
        expect(directory.createDirectory());
        ApplicationController controller;
        controller.createEmptyProject("Assets", "asset-project");
        BackgroundJobSystem jobs{16U, 2U};
        SampleAssetRegistry registry{8U * 1024U * 1024U};

        beginTest("ASSET-M1-001 through ASSET-M1-003 asynchronous WAV AIFF FLAC decode");
        for (const auto& extension :
             {juce::String{".wav"}, juce::String{".aiff"}, juce::String{".flac"}}) {
            const auto file = directory.getChildFile("fixture" + extension);
            expect(writeFixture(file, 48000.0, 2, 64));
            const auto assetUuid = "asset-" + extension.trimCharactersAtStart(".");
            const auto handle =
                SampleImporter::submit(jobs, makeRequest(controller, file, assetUuid));
            expect(handle.has_value());
            const auto result = awaitCompletion(jobs);
            expect(result != nullptr);
            expect(result != nullptr && result->succeeded,
                   result != nullptr ? result->message : "No import result");
            if (result != nullptr && result->succeeded) {
                const auto* payload =
                    static_cast<const SampleImportPayload*>(result->immutablePayload.get());
                expect(payload != nullptr && payload->asset != nullptr);
                if (payload != nullptr && payload->asset != nullptr) {
                    expectEquals(static_cast<int>(payload->asset->metadata().channelCount), 2);
                    expectEquals(static_cast<juce::int64>(payload->asset->metadata().frameCount),
                                 juce::int64{64});
                    expectWithinAbsoluteError(payload->asset->metadata().sampleRate, 48000.0, 0.01);
                    expect(payload->asset->metadata().contentFingerprint.isNotEmpty());
                }
            }
        }

        beginTest("ASSET-M1-005 through ASSET-M1-008 channel, rate, and byte accounting");
        for (const auto sampleRate : {44100.0, 48000.0, 88200.0, 96000.0}) {
            const auto file = directory.getChildFile(
                "rate-" + juce::String{static_cast<int>(sampleRate)} + ".wav");
            expect(writeFixture(file, sampleRate, 1, 37));
            auto request =
                makeRequest(controller, file, "rate-" + juce::String{static_cast<int>(sampleRate)});
            CancellationToken token;
            JobProgress progress;
            const auto result = SampleImporter::decode(request, token, progress);
            expect(result != nullptr && result->succeeded,
                   result != nullptr ? result->message : "No import result");
            if (result != nullptr && result->succeeded) {
                const auto* payload =
                    static_cast<const SampleImportPayload*>(result->immutablePayload.get());
                expect(payload != nullptr);
                if (payload != nullptr) {
                    expectEquals(static_cast<int>(payload->asset->metadata().channelCount), 1);
                    expectEquals(static_cast<juce::int64>(payload->asset->metadata().frameCount),
                                 juce::int64{37});
                    expectWithinAbsoluteError(payload->asset->metadata().sampleRate, sampleRate,
                                              0.01);
                    expectEquals(static_cast<juce::int64>(payload->asset->decodedBytes()),
                                 juce::int64{148});
                }
            }
        }

        beginTest("ASSET-M1-009 unique registry accounting and live-reader replacement");
        const auto replacementFile = directory.getChildFile("replacement.wav");
        expect(writeFixture(replacementFile, 48000.0, 2, 128));
        CancellationToken replacementToken;
        JobProgress replacementProgress;
        const auto replacementResult =
            SampleImporter::decode(makeRequest(controller, replacementFile, "replacement"),
                                   replacementToken, replacementProgress);
        expect(replacementResult != nullptr && replacementResult->succeeded);
        if (replacementResult != nullptr && replacementResult->succeeded) {
            const auto* payload =
                static_cast<const SampleImportPayload*>(replacementResult->immutablePayload.get());
            expect(payload != nullptr && registry.publish(payload->asset));
            const auto retainedReader = registry.find("replacement");
            expect(retainedReader != nullptr);
            expect(payload != nullptr && registry.publish(payload->asset));
            expectEquals(static_cast<int>(registry.uniqueAssetCount()), 1);
            expectEquals(static_cast<juce::int64>(registry.usedBytes()), juce::int64{1024});
            expect(retainedReader != nullptr && retainedReader->view().interleavedData != nullptr);
            registry.clear();
        }

        beginTest("ASSET-M1-010 budget rejection leaves project and registry unchanged");
        const auto budgetState = controller.project().state();
        const auto budgetRevision = controller.project().revision();
        auto overBudget = makeRequest(controller, replacementFile, "over-budget");
        overBudget.maximumDecodedBytes = 1U;
        CancellationToken budgetToken;
        JobProgress budgetProgress;
        const auto budgetResult = SampleImporter::decode(overBudget, budgetToken, budgetProgress);
        expect(budgetResult != nullptr && !budgetResult->succeeded);
        expect(controller.project().state() == budgetState);
        expectEquals(static_cast<juce::int64>(controller.project().revision()),
                     static_cast<juce::int64>(budgetRevision));
        expectEquals(static_cast<int>(registry.uniqueAssetCount()), 0);

        beginTest("ASSET-M1-014 stale completion is discarded without mutation");
        auto staleRequest = makeRequest(controller, replacementFile, "stale");
        CancellationToken staleToken;
        JobProgress staleProgress;
        const auto staleResult = SampleImporter::decode(staleRequest, staleToken, staleProgress);
        expect(staleResult != nullptr && staleResult->succeeded);
        expect(controller.renamePad(0U, "Newer revision").wasOk());
        const auto staleState = controller.project().state();
        expect(staleResult != nullptr &&
               SampleImporter::commit(*staleResult, controller, registry).failed());
        expect(controller.project().state() == staleState);
        expectEquals(static_cast<int>(registry.uniqueAssetCount()), 0);

        beginTest("ASSET-M1-011 successful latest import commits immutable asset and layer");
        const auto latestRequest = makeRequest(controller, replacementFile, "latest", 1U);
        CancellationToken latestToken;
        JobProgress latestProgress;
        const auto latestResult =
            SampleImporter::decode(latestRequest, latestToken, latestProgress);
        expect(latestResult != nullptr && latestResult->succeeded);
        expect(latestResult != nullptr &&
               SampleImporter::commit(*latestResult, controller, registry).wasOk());
        expectEquals(controller.project().pad(0U).layers[1].assetUuid, juce::String{"latest"});
        expect(controller.project().pad(0U).layers[1].enabled);
        expect(registry.find("latest") != nullptr);
        expect(controller.undo());
        expect(controller.project().pad(0U).layers[1].assetUuid.isEmpty());
        expect(std::none_of(controller.project().state().assets.begin(),
                            controller.project().state().assets.end(),
                            [](const auto& asset) { return asset.uuid == "latest"; }));
        expect(controller.redo());
        expectEquals(controller.project().pad(0U).layers[1].assetUuid, juce::String{"latest"});

        beginTest("ASSET-M1-004 and ASSET-M1-015 corrupt/cancelled imports do not mutate");
        const auto corrupt = directory.getChildFile("corrupt.wav");
        expect(corrupt.replaceWithText("not audio"));
        CancellationToken corruptToken;
        JobProgress corruptProgress;
        const auto stateBeforeFailures = controller.project().state();
        const auto corruptResult = SampleImporter::decode(
            makeRequest(controller, corrupt, "corrupt"), corruptToken, corruptProgress);
        expect(corruptResult != nullptr && !corruptResult->succeeded);
        const auto multichannel = directory.getChildFile("unsupported-channels.wav");
        expect(writeFixture(multichannel, 48000.0, 3, 32));
        CancellationToken multichannelToken;
        JobProgress multichannelProgress;
        const auto multichannelResult =
            SampleImporter::decode(makeRequest(controller, multichannel, "multichannel"),
                                   multichannelToken, multichannelProgress);
        expect(multichannelResult != nullptr && !multichannelResult->succeeded);
        CancellationToken cancelledToken;
        cancelledToken.cancel();
        JobProgress cancelledProgress;
        const auto cancelledResult =
            SampleImporter::decode(makeRequest(controller, replacementFile, "cancelled"),
                                   cancelledToken, cancelledProgress);
        expect(cancelledResult != nullptr && !cancelledResult->succeeded);
        expect(controller.project().state() == stateBeforeFailures);

        beginTest("ASSET-M1-012 and ASSET-M1-013 unload and epoch retirement stay off callback");
        auto retainedOnUnload = registry.find("latest");
        expect(retainedOnUnload != nullptr);
        registry.clear();
        expectEquals(static_cast<int>(registry.uniqueAssetCount()), 0);
        expect(retainedOnUnload != nullptr && retainedOnUnload->view().interleavedData != nullptr);
        DeferredSampleAssetReclaimer reclaimer;
        reclaimer.retireAfterEpoch(9U, retainedOnUnload);
        retainedOnUnload.reset();
        expectEquals(static_cast<int>(reclaimer.pendingCount()), 1);
        expectEquals(static_cast<int>(reclaimer.collectAcknowledged()), 0);
        reclaimer.acknowledgeAudioEpoch(9U);
        expectEquals(static_cast<int>(reclaimer.collectAcknowledged()), 1);

        jobs.cancelOwner(controller.project().uuid());
        jobs.shutdown();
        registry.clear();
        directory.deleteRecursively();
    }
};

static Milestone1AssetTests milestone1AssetTests;
} // namespace padflow
