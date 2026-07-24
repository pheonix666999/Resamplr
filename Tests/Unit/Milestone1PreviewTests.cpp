#include "Sampling/SamplePreviewController.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <memory>

namespace padflow {
namespace {
bool writePreviewFixture(const juce::File& file) {
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
                             .withSampleRate(48000.0)
                             .withNumChannels(2)
                             .withBitsPerSample(24);
    auto writer = format->createWriterFor(output, options);
    if (writer == nullptr)
        return false;

    juce::AudioBuffer<float> buffer{2, 128};
    for (int frame = 0; frame < buffer.getNumSamples(); ++frame) {
        const auto sample = 0.5F * std::sin(static_cast<float>(frame) * 0.1F);
        buffer.setSample(0, frame, sample);
        buffer.setSample(1, frame, -sample);
    }
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

std::shared_ptr<const JobResult> awaitPreviewCompletion(BackgroundJobSystem& jobs) {
    std::shared_ptr<const JobResult> result;
    for (int attempt = 0; attempt < 1000 && result == nullptr; ++attempt) {
        juce::Thread::sleep(2);
        result = jobs.tryPopCompleted();
    }
    return result;
}
} // namespace

class Milestone1PreviewTests final : public juce::UnitTest {
  public:
    Milestone1PreviewTests() : juce::UnitTest("Milestone 1 preview", "PadFlow") {}

    void runTest() override {
        const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getNonexistentChildFile("padflow-m1-preview", {}, true);
        expect(directory.createDirectory());
        ApplicationController controller;
        controller.createEmptyProject("Preview", "preview-project");
        const auto initialProject = controller.project().state();
        PreviewPlayer player;
        player.prepare(48000.0);
        SamplePreviewController preview{controller, player};
        BackgroundJobSystem jobs{8U, 1U};
        std::array<float, 64U> left{};
        std::array<float, 64U> right{};

        beginTest("PREVIEW-M1-001 WAV, AIFF, and FLAC preview before assignment");
        for (const auto& extension :
             {juce::String{".wav"}, juce::String{".aiff"}, juce::String{".flac"}}) {
            const auto file = directory.getChildFile("preview" + extension);
            expect(writePreviewFixture(file));
            expect(preview.begin(jobs, file).has_value());
            const auto result = awaitPreviewCompletion(jobs);
            expect(result != nullptr);
            expect(result != nullptr && preview.commit(*result).wasOk(),
                   result != nullptr ? result->message : "No preview result");
            left.fill(0.0F);
            right.fill(0.0F);
            player.processAdd(left.data(), right.data(), left.size());
            expect(player.isActive());
            bool nonSilent = false;
            for (std::size_t frame = 0; frame < left.size(); ++frame) {
                expect(std::isfinite(left[frame]) && std::isfinite(right[frame]));
                nonSilent = nonSilent || std::abs(left[frame]) > 0.00001F;
            }
            expect(nonSilent);
            expect(preview.stop());
            player.processAdd(left.data(), right.data(), left.size());
            expect(!player.isActive());
            expect(preview.collectRetired() > 0U);
        }
        expect(controller.project().state().banks == initialProject.banks);

        beginTest("PREVIEW-M1-002 file change and stop release the fixed preview voice");
        const auto first = directory.getChildFile("first.wav");
        const auto second = directory.getChildFile("second.aiff");
        expect(writePreviewFixture(first));
        expect(writePreviewFixture(second));
        expect(preview.begin(jobs, first).has_value());
        auto result = awaitPreviewCompletion(jobs);
        expect(result != nullptr && preview.commit(*result).wasOk());
        player.processAdd(left.data(), right.data(), left.size());
        expect(player.isActive());
        expect(preview.begin(jobs, second).has_value());
        player.processAdd(left.data(), right.data(), left.size());
        expect(!player.isActive());
        result = awaitPreviewCompletion(jobs);
        expect(result != nullptr && preview.commit(*result).wasOk());
        player.processAdd(left.data(), right.data(), left.size());
        expect(player.isActive());

        beginTest("PREVIEW-M1-003 failed preview is silent and reports an error");
        const auto corrupt = directory.getChildFile("corrupt.wav");
        expect(corrupt.replaceWithText("not audio"));
        expect(preview.begin(jobs, corrupt).has_value());
        player.processAdd(left.data(), right.data(), left.size());
        result = awaitPreviewCompletion(jobs);
        expect(result != nullptr && preview.commit(*result).failed());
        left.fill(0.0F);
        right.fill(0.0F);
        player.processAdd(left.data(), right.data(), left.size());
        expect(!player.isActive());
        expect(preview.lastError().isNotEmpty());
        for (std::size_t frame = 0; frame < left.size(); ++frame)
            expect(left[frame] == 0.0F && right[frame] == 0.0F);

        beginTest("PREVIEW-M1-004 volume is bounded, persistent, and callback-safe");
        expect(preview.setVolume(-0.1F).failed());
        expect(preview.setVolume(1.1F).failed());
        expect(preview.setVolume(0.25F).wasOk());
        expectWithinAbsoluteError(player.volume(), 0.25F, 0.00001F);
        expectWithinAbsoluteError(controller.project().state().ui.previewVolume, 0.25F, 0.00001F);

        juce::ignoreUnused(preview.stop());
        player.processAdd(left.data(), right.data(), left.size());
        juce::ignoreUnused(preview.collectRetired());
        jobs.shutdown();
        expect(directory.deleteRecursively());
    }
};

static Milestone1PreviewTests milestone1PreviewTests;
} // namespace padflow
