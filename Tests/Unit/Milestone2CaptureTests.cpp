#include "Audio/CaptureWriter.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace padflow {
namespace {
CaptureSpec captureSpec(const juce::File& destination, const std::uint32_t channels,
                        const CaptureMode mode) {
    CaptureSpec spec;
    spec.destination = destination;
    spec.sampleRate = 1000.0;
    spec.channels = channels;
    spec.maximumFramesPerBlock = 16U;
    spec.fifoBlockCount = 250U;
    spec.mode = mode;
    spec.thresholdDecibels = -12.0F;
    spec.sessionUuid = juce::Uuid{}.toString();
    spec.target = CaptureTarget{"project", "pad", "layer", 7U};
    return spec;
}

CaptureStatus waitForTerminal(CaptureSession& session) {
    for (int attempt = 0; attempt < 5000; ++attempt) {
        const auto status = session.status();
        if (status.state == CaptureState::completed || status.state == CaptureState::cancelled ||
            status.state == CaptureState::failed)
            return status;
        juce::Thread::sleep(1);
    }
    return session.status();
}

std::unique_ptr<juce::AudioFormatReader> openWave(const juce::File& file) {
    juce::WavAudioFormat format;
    return std::unique_ptr<juce::AudioFormatReader>(
        format.createReaderFor(file.createInputStream().release(), true));
}
} // namespace

class Milestone2CaptureTests final : public juce::UnitTest {
  public:
    Milestone2CaptureTests() : juce::UnitTest("Milestone 2 input capture", "PadFlow") {}

    void runTest() override {
        const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getNonexistentChildFile("padflow-capture-tests", {}, true);
        expect(root.createDirectory());

        beginTest("RECORD-M2-001 preallocates at least four seconds");
        CaptureSession invalidCapacity;
        auto invalidSpec = captureSpec(root.getChildFile("invalid.wav"), 1U, CaptureMode::manual);
        invalidSpec.fifoBlockCount = 249U;
        expect(!invalidCapacity.prepare(invalidSpec));
        auto commonDeviceSpec =
            captureSpec(root.getChildFile("common-device.wav"), 2U, CaptureMode::manual);
        commonDeviceSpec.sampleRate = 48000.0;
        commonDeviceSpec.maximumFramesPerBlock = 512U;
        commonDeviceSpec.fifoBlockCount = 375U;
        expect(commonDeviceSpec.fifoBlockCount <= CaptureFifo::maximumBlocks);
        CaptureSession commonDevice;
        expect(commonDevice.prepare(commonDeviceSpec));
        commonDevice.cancel();
        commonDevice.shutdown();

        beginTest("RECORD-M2-002 manual start and stop finalizes exact mono frames");
        CaptureSession manual;
        const auto manualDestination = root.getChildFile("manual.wav");
        expect(manual.prepare(captureSpec(manualDestination, 1U, CaptureMode::manual)));
        expectEquals(static_cast<int>(manual.status().state),
                     static_cast<int>(CaptureState::armed));
        expect(manual.startManual());
        std::array<float, 64U> mono{};
        for (std::size_t frame = 0U; frame < mono.size(); ++frame)
            mono[frame] = 0.5F * std::sin(static_cast<float>(frame) * 0.1F);
        const std::array<const float*, 1U> monoChannels{mono.data()};
        manual.processInput(monoChannels.data(), 1U, static_cast<std::uint32_t>(mono.size()));
        manual.requestStop();
        const auto manualStatus = waitForTerminal(manual);
        expectEquals(static_cast<int>(manualStatus.state),
                     static_cast<int>(CaptureState::completed));
        expectEquals(static_cast<juce::int64>(manualStatus.framesAccepted), juce::int64{64});
        expectEquals(static_cast<juce::int64>(manualStatus.framesWritten), juce::int64{64});
        expect(manual.completedTarget() == CaptureTarget{"project", "pad", "layer", 7U});

        beginTest("RECORD-M2-008, RECORD-M2-010 and RECORD-M2-011 valid mono WAV");
        auto manualReader = openWave(manual.completedFile());
        expect(manualReader != nullptr);
        if (manualReader != nullptr) {
            expectEquals(static_cast<int>(manualReader->numChannels), 1);
            expectEquals(manualReader->lengthInSamples, juce::int64{64});
            expectWithinAbsoluteError(manualReader->sampleRate, 1000.0, 0.01);
        }

        beginTest("RECORD-M2-003 through RECORD-M2-007 threshold and wrapped pre-roll");
        CaptureSession threshold;
        auto thresholdConfiguration =
            captureSpec(root.getChildFile("threshold.wav"), 1U, CaptureMode::threshold);
        thresholdConfiguration.preRollMilliseconds = 10U;
        expect(threshold.prepare(thresholdConfiguration));
        std::array<float, 20U> below{};
        for (std::size_t frame = 0U; frame < below.size(); ++frame)
            below[frame] = 0.01F + static_cast<float>(frame) * 0.001F;
        const std::array<const float*, 1U> belowChannels{below.data()};
        threshold.processInput(belowChannels.data(), 1U, static_cast<std::uint32_t>(below.size()));
        expectEquals(static_cast<int>(threshold.status().state),
                     static_cast<int>(CaptureState::waitingForThreshold));
        std::array<float, 8U> crossing{0.031F, 0.032F, 0.033F, 0.5F, 0.4F, 0.3F, 0.2F, 0.1F};
        const std::array<const float*, 1U> crossingChannels{crossing.data()};
        threshold.processInput(crossingChannels.data(), 1U,
                               static_cast<std::uint32_t>(crossing.size()));
        expectEquals(static_cast<int>(threshold.status().state),
                     static_cast<int>(CaptureState::recording));
        threshold.requestStop();
        const auto thresholdStatus = waitForTerminal(threshold);
        expectEquals(static_cast<int>(thresholdStatus.state),
                     static_cast<int>(CaptureState::completed));
        expectEquals(static_cast<juce::int64>(thresholdStatus.framesWritten), juce::int64{15});
        auto thresholdReader = openWave(threshold.completedFile());
        expect(thresholdReader != nullptr);
        if (thresholdReader != nullptr) {
            juce::AudioBuffer<float> captured{1, 15};
            expect(thresholdReader->read(&captured, 0, 15, 0, true, false));
            expectWithinAbsoluteError(captured.getSample(0, 3), below[16], 0.0001F);
            expectWithinAbsoluteError(captured.getSample(0, 9), crossing[2], 0.0001F);
            expectWithinAbsoluteError(captured.getSample(0, 10), crossing[3], 0.0001F);
        }

        beginTest("RECORD-M2-009 stereo input preserves both channels");
        CaptureSession stereo;
        expect(
            stereo.prepare(captureSpec(root.getChildFile("stereo.wav"), 2U, CaptureMode::manual)));
        expect(stereo.startManual());
        std::array<float, 16U> left{};
        std::array<float, 16U> right{};
        left.fill(0.25F);
        right.fill(-0.5F);
        const std::array<const float*, 2U> stereoChannels{left.data(), right.data()};
        stereo.processInput(stereoChannels.data(), 2U, 16U);
        stereo.requestStop();
        expectEquals(static_cast<int>(waitForTerminal(stereo).state),
                     static_cast<int>(CaptureState::completed));
        auto stereoReader = openWave(stereo.completedFile());
        expect(stereoReader != nullptr);
        if (stereoReader != nullptr)
            expectEquals(static_cast<int>(stereoReader->numChannels), 2);

        beginTest("THREAD-M2-003 and THREAD-M2-004 FIFO exhaustion is bounded");
        CaptureFifo directFifo;
        expect(directFifo.configure(2U, 4U, 1U));
        std::uint16_t firstIndex = 0U;
        std::uint16_t secondIndex = 0U;
        std::uint16_t unavailableIndex = 0U;
        expect(directFifo.beginAudioWrite(firstIndex) != nullptr);
        expect(directFifo.beginAudioWrite(secondIndex) != nullptr);
        expect(directFifo.beginAudioWrite(unavailableIndex) == nullptr);
        expect(directFifo.releaseReadBlock(firstIndex));
        expect(directFifo.releaseReadBlock(secondIndex));

        beginTest("RECORD-M2-012 and RECORD-M2-013 reject incomplete overflow output");
        CaptureSession overflow;
        const auto overflowDestination = root.getChildFile("overflow.wav");
        expect(overflow.prepare(captureSpec(overflowDestination, 1U, CaptureMode::manual)));
        std::vector<std::uint16_t> heldBlocks;
        heldBlocks.reserve(250U);
        for (std::uint32_t block = 0U; block < 250U; ++block) {
            std::uint16_t index = 0U;
            expect(overflow.fifo()->beginAudioWrite(index) != nullptr);
            heldBlocks.push_back(index);
        }
        expect(overflow.startManual());
        overflow.processInput(monoChannels.data(), 1U, 16U);
        expect(overflow.status().incomplete);
        expect(overflow.status().overflowCount > 0U);
        for (const auto index : heldBlocks)
            expect(overflow.fifo()->releaseReadBlock(index));
        overflow.requestStop();
        expectEquals(static_cast<int>(waitForTerminal(overflow).state),
                     static_cast<int>(CaptureState::failed));
        expect(!overflowDestination.existsAsFile());

        beginTest("RECORD-M2-014 cancellation removes partial output");
        CaptureSession cancelled;
        const auto cancelledDestination = root.getChildFile("cancelled.wav");
        expect(cancelled.prepare(captureSpec(cancelledDestination, 1U, CaptureMode::manual)));
        expect(cancelled.startManual());
        cancelled.processInput(monoChannels.data(), 1U, 16U);
        cancelled.cancel();
        expectEquals(static_cast<int>(waitForTerminal(cancelled).state),
                     static_cast<int>(CaptureState::cancelled));
        expect(!cancelledDestination.existsAsFile());

        beginTest("RECORD-M2-015 collision-safe final filename");
        CaptureSession collision;
        expect(collision.prepare(captureSpec(manualDestination, 1U, CaptureMode::manual)));
        expect(collision.startManual());
        collision.processInput(monoChannels.data(), 1U, 16U);
        collision.requestStop();
        expectEquals(static_cast<int>(waitForTerminal(collision).state),
                     static_cast<int>(CaptureState::completed));
        expect(collision.completedFile() != manualDestination);
        expect(collision.completedFile().existsAsFile());

        manual.shutdown();
        threshold.shutdown();
        stereo.shutdown();
        overflow.shutdown();
        cancelled.shutdown();
        collision.shutdown();
        expect(root.deleteRecursively());
    }
};

static Milestone2CaptureTests milestone2CaptureTests;
} // namespace padflow
