#include "App/ApplicationController.h"
#include "App/ProductInfo.h"
#include "Audio/AudioCommandQueue.h"
#include "Audio/CaptureWriter.h"
#include "Model/MusicalTime.h"
#include "Model/Project.h"
#include "Sampling/SampleAsset.h"
#include "Serialization/ProjectSerializer.h"
#include "Smoke/SmokeScenario.h"
#include "Utilities/BackgroundJobSystem.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <limits>
#include <memory>
#include <vector>

namespace padflow {
class FoundationTests final : public juce::UnitTest {
  public:
    FoundationTests() : juce::UnitTest("Milestone 0 foundation", "PadFlow") {}

    void runTest() override {
        beginTest("MODEL-001 product metadata and empty schema-v1 project");
        expectEquals(juce::String{product::name.data()}, juce::String{"PadFlow"});
        const auto project = Project::createEmpty("Unit Test", "fixed-project-uuid");
        expectEquals(project.schemaVersion(), 1);
        expectEquals(project.uuid(), juce::String{"fixed-project-uuid"});
        expectEquals(ppqTicksPerQuarterNote, std::int64_t{960});
        expect(MusicalTime{12, 34U} == MusicalTime{12, 34U});
        ApplicationController controller;
        controller.createEmptyProject("Controller", "controller-project");
        expect(controller.isCurrentJobTarget(
            JobSpec{"controller-project", "controller-project", 0U, 0, JobKind::generic}));
        expect(!controller.isCurrentJobTarget(
            JobSpec{"controller-project", "controller-project", 1U, 0, JobKind::generic}));

        beginTest("SAVE-001 canonical manifest and semantic round trip");
        const auto temporaryDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                            .getNonexistentChildFile("padflow-test", {}, true);
        expect(temporaryDirectory.createDirectory());
        const auto destination = temporaryDirectory.getChildFile("roundtrip.padflow");
        const auto manifestA = ProjectSerializer::canonicalManifest(project);
        const auto manifestB = ProjectSerializer::canonicalManifest(project);
        expectEquals(manifestA, manifestB);
        const auto saveResult = ProjectSerializer::save(project, destination);
        expect(saveResult.succeeded, saveResult.message);
        auto loaded = Project::createEmpty();
        const auto loadResult = ProjectSerializer::load(destination, loaded);
        expect(loadResult.wasOk(), loadResult.getErrorMessage());
        expectEquals(loaded.uuid(), project.uuid());
        expectEquals(loaded.name(), project.name());
        temporaryDirectory.deleteRecursively();

        beginTest("THREAD-001 bounded SPSC command queue");
        SpscQueue<AudioCommand, 2U> queue;
        expect(queue.tryPush(AudioCommand{AudioCommandType::stopAll, 1U, 2U, 3.0F}));
        expect(queue.tryPush(AudioCommand{AudioCommandType::publishProject, 4U, 5U, 6.0F}));
        expect(!queue.tryPush(AudioCommand{}));
        AudioCommand command;
        expect(queue.tryPop(command));
        expect(command.type == AudioCommandType::stopAll);
        expect(queue.tryPop(command));
        expect(!queue.tryPop(command));

        beginTest("THREAD-002 background job immutable completion");
        BackgroundJobSystem jobs{4U, 1U};
        JobSpec spec{"owner", "target", 7U, 0, JobKind::generic};
        const auto handle =
            jobs.submit(spec, [spec](const CancellationToken& token, JobProgress& progress) {
                progress.set(1.0F);
                return std::make_shared<const JobResult>(
                    JobResult{spec, !token.isCancellationRequested(), "complete", {}});
            });
        expect(handle.has_value());
        expect(handle.has_value() && handle->progress != nullptr);
        std::shared_ptr<const JobResult> completed;
        for (int attempt = 0; attempt < 100 && completed == nullptr; ++attempt) {
            juce::Thread::sleep(2);
            completed = jobs.tryPopCompleted();
        }
        expect(completed != nullptr);
        expect(completed != nullptr && completed->succeeded);
        expect(completed != nullptr && completed->target.targetRevision == 7U);
        jobs.shutdown();

        beginTest("THREAD-002B bounded jobs and cooperative cancellation");
        BackgroundJobSystem boundedJobs{1U, 1U};
        const auto cancelled =
            boundedJobs.submit(spec, [spec](const CancellationToken& token, JobProgress& progress) {
                for (int attempt = 0; attempt < 50 && !token.isCancellationRequested(); ++attempt)
                    juce::Thread::sleep(1);
                progress.set(1.0F);
                return std::make_shared<const JobResult>(
                    JobResult{spec, !token.isCancellationRequested(), "cancel check", {}});
            });
        expect(cancelled.has_value());
        expect(!boundedJobs
                    .submit(spec, [](const CancellationToken&,
                                     JobProgress&) { return std::make_shared<const JobResult>(); })
                    .has_value());
        if (cancelled.has_value())
            cancelled->cancel();
        completed.reset();
        for (int attempt = 0; attempt < 100 && completed == nullptr; ++attempt) {
            juce::Thread::sleep(2);
            completed = boundedJobs.tryPopCompleted();
        }
        expect(completed != nullptr && !completed->succeeded);
        boundedJobs.shutdown();

        beginTest("ASSET-001 immutable PCM asset and byte accounting");
        SampleAssetMetadata metadata;
        metadata.assetUuid = "asset";
        metadata.displayName = "Synthetic";
        metadata.sampleRate = 48000.0;
        metadata.channelCount = 2U;
        metadata.frameCount = 4U;
        metadata.provenance = "test";
        auto asset = SampleAsset::create(metadata, std::vector<float>(8U, 0.25F));
        expectEquals(static_cast<juce::int64>(asset->decodedBytes()), juce::int64{32});
        expectEquals(static_cast<juce::int64>(estimateDecodedBytes(4U, 2U)), juce::int64{32});
        expectEquals(static_cast<juce::int64>(defaultDecodedSampleBudgetBytes),
                     juce::int64{2147483648LL});
        SampleAssetRegistry defaultRegistry;
        expectEquals(static_cast<juce::int64>(defaultRegistry.budgetBytes()),
                     static_cast<juce::int64>(
                         clampConfiguredDecodedSampleBudgetBytes(defaultDecodedSampleBudgetBytes)));
        expect(clampConfiguredDecodedSampleBudgetBytes(1U) >= minimumDecodedSampleBudgetBytes);
        expect(clampConfiguredDecodedSampleBudgetBytes(std::numeric_limits<std::uint64_t>::max()) <=
               maximumDecodedSampleBudgetBytes);
        const auto view = asset->view();
        expect(view.interleavedData != nullptr && view.frameCount == 4U && view.channelCount == 2U);
        DeferredSampleAssetReclaimer reclaimer;
        reclaimer.retireAfterEpoch(2U, asset);
        expectEquals(static_cast<int>(reclaimer.pendingCount()), 1);
        reclaimer.acknowledgeAudioEpoch(2U);
        expectEquals(static_cast<int>(reclaimer.collectAcknowledged()), 1);

        beginTest("THREAD-003 preallocated capture FIFO lifecycle");
        CaptureFifo fifo;
        expect(fifo.configure(2U, 16U, 2U));
        std::uint16_t blockIndex = 0U;
        auto* writeData = fifo.beginAudioWrite(blockIndex);
        expect(writeData != nullptr);
        if (writeData != nullptr)
            writeData[0] = 0.5F;
        expect(fifo.commitAudioWrite(blockIndex, 1U));
        CaptureBlockDescriptor descriptor;
        expect(fifo.tryPopReady(descriptor));
        const auto* readData = fifo.readData(descriptor.blockIndex);
        expect(readData != nullptr && readData[0] == 0.5F);
        expect(fifo.releaseReadBlock(descriptor.blockIndex));

        beginTest("UIHEADLESS-001 shared deterministic smoke scenario");
        const auto smoke = runSmokeScenario();
        expect(smoke.succeeded, smoke.diagnostics);
    }
};

static FoundationTests foundationTests;
} // namespace padflow
