#include "CaptureWriter.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace padflow {
bool CaptureFifo::configure(const std::uint32_t blockCount,
                            const std::uint32_t maximumFramesPerBlock,
                            const std::uint32_t channels) {
    if (blockCount == 0U || blockCount > static_cast<std::uint32_t>(maximumBlocks) ||
        maximumFramesPerBlock == 0U || channels == 0U || channels > 2U)
        return false;

    const auto samplesPerBlock =
        static_cast<std::size_t>(maximumFramesPerBlock) * static_cast<std::size_t>(channels);
    if (samplesPerBlock > std::numeric_limits<std::size_t>::max() / blockCount)
        return false;

    storage_.assign(samplesPerBlock * static_cast<std::size_t>(blockCount), 0.0F);
    blockCount_ = blockCount;
    maximumFramesPerBlock_ = maximumFramesPerBlock;
    channels_ = channels;
    freeBlocks_.resetWhenQuiescent();
    readyBlocks_.resetWhenQuiescent();

    for (std::uint32_t index = 0U; index < blockCount_; ++index) {
        const auto blockIndex = static_cast<std::uint16_t>(index);
        if (!freeBlocks_.tryPush(blockIndex))
            return false;
    }

    return true;
}

float* CaptureFifo::beginAudioWrite(std::uint16_t& blockIndex) noexcept {
    if (!freeBlocks_.tryPop(blockIndex))
        return nullptr;

    const auto offset = static_cast<std::size_t>(blockIndex) *
                        static_cast<std::size_t>(maximumFramesPerBlock_) *
                        static_cast<std::size_t>(channels_);
    return storage_.data() + offset;
}

bool CaptureFifo::commitAudioWrite(const std::uint16_t blockIndex,
                                   const std::uint32_t validFrames) noexcept {
    if (blockIndex >= blockCount_ || validFrames > maximumFramesPerBlock_)
        return false;

    return readyBlocks_.tryPush(CaptureBlockDescriptor{blockIndex, validFrames});
}

bool CaptureFifo::tryPopReady(CaptureBlockDescriptor& descriptor) noexcept {
    return readyBlocks_.tryPop(descriptor);
}

const float* CaptureFifo::readData(const std::uint16_t blockIndex) const noexcept {
    if (blockIndex >= blockCount_)
        return nullptr;

    const auto offset = static_cast<std::size_t>(blockIndex) *
                        static_cast<std::size_t>(maximumFramesPerBlock_) *
                        static_cast<std::size_t>(channels_);
    return storage_.data() + offset;
}

bool CaptureFifo::releaseReadBlock(const std::uint16_t blockIndex) noexcept {
    return blockIndex < blockCount_ && freeBlocks_.tryPush(blockIndex);
}

std::uint32_t CaptureFifo::maximumFramesPerBlock() const noexcept {
    return maximumFramesPerBlock_;
}

std::uint32_t CaptureFifo::channelCount() const noexcept {
    return channels_;
}

CaptureSession::~CaptureSession() {
    shutdown();
}

bool CaptureSession::prepare(const CaptureSpec& spec) {
    shutdown();
    if (spec.source != CaptureSource::input || spec.destination == juce::File{} ||
        spec.target.projectUuid.trim().isEmpty() || spec.target.padUuid.trim().isEmpty() ||
        spec.target.layerUuid.trim().isEmpty() || !std::isfinite(spec.sampleRate) ||
        spec.sampleRate <= 0.0 || (spec.channels != 1U && spec.channels != 2U) ||
        spec.maximumFramesPerBlock == 0U || spec.fifoBlockCount == 0U ||
        spec.fifoBlockCount > static_cast<std::uint32_t>(CaptureFifo::maximumBlocks) ||
        static_cast<double>(spec.fifoBlockCount) * static_cast<double>(spec.maximumFramesPerBlock) <
            spec.sampleRate * 4.0 ||
        !std::isfinite(spec.thresholdDecibels) || spec.thresholdDecibels < -96.0F ||
        spec.thresholdDecibels > 0.0F || spec.preRollMilliseconds > 2000U)
        return false;
    if (!fifo_.configure(spec.fifoBlockCount, spec.maximumFramesPerBlock, spec.channels))
        return false;

    spec_ = spec;
    if (spec_.sessionUuid.trim().isEmpty())
        spec_.sessionUuid = juce::Uuid{}.toString();
    finalFile_ = spec_.destination.existsAsFile() ? spec_.destination.getNonexistentSibling(false)
                                                  : spec_.destination;
    temporaryFile_ =
        finalFile_.getSiblingFile(finalFile_.getFileName() + "." + spec_.sessionUuid + ".part");
    const auto preRollFrames = static_cast<std::uint64_t>(
        std::ceil(spec_.sampleRate * static_cast<double>(spec_.preRollMilliseconds) / 1000.0));
    if (preRollFrames >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) / spec_.channels)
        return false;
    preRollCapacityFrames_ = preRollFrames;
    preRoll_.assign(static_cast<std::size_t>(preRollFrames * spec_.channels), 0.0F);
    preRollWriteFrame_ = 0U;
    preRollValidFrames_ = 0U;
    thresholdLinear_ = std::pow(10.0F, spec_.thresholdDecibels / 20.0F);
    failureMessage_.clear();
    writerReady_.store(false, std::memory_order_release);
    stopRequested_.store(false, std::memory_order_release);
    callbackActive_.store(false, std::memory_order_release);
    incomplete_.store(false, std::memory_order_release);
    framesAccepted_.store(0U, std::memory_order_release);
    framesWritten_.store(0U, std::memory_order_release);
    overflowCount_.store(0U, std::memory_order_release);
    inputPeak_.store(0.0F, std::memory_order_release);
    state_.store(CaptureState::idle, std::memory_order_release);
    writerThread_ = std::thread([this] { writerLoop(); });

    std::unique_lock lock{controlMutex_};
    readyCondition_.wait(lock, [this] {
        return writerReady_.load(std::memory_order_acquire) ||
               state_.load(std::memory_order_acquire) == CaptureState::failed;
    });
    if (state_.load(std::memory_order_acquire) == CaptureState::failed) {
        lock.unlock();
        shutdown();
        return false;
    }
    state_.store(spec_.mode == CaptureMode::threshold ? CaptureState::waitingForThreshold
                                                      : CaptureState::armed,
                 std::memory_order_release);
    return true;
}

CaptureFifo* CaptureSession::fifo() noexcept {
    return &fifo_;
}

bool CaptureSession::startManual() noexcept {
    auto expected = CaptureState::armed;
    return spec_.mode == CaptureMode::manual &&
           state_.compare_exchange_strong(expected, CaptureState::recording,
                                          std::memory_order_acq_rel);
}

float CaptureSession::inputFramePeak(const float* const* const channels,
                                     const std::uint32_t inputChannels,
                                     const std::uint32_t frame) const noexcept {
    auto peak = 0.0F;
    for (std::uint32_t channel = 0U; channel < std::min(inputChannels, spec_.channels); ++channel)
        if (channels != nullptr && channels[channel] != nullptr) {
            const auto sample = channels[channel][frame];
            if (std::isfinite(sample))
                peak = std::max(peak, std::abs(sample));
        }
    return peak;
}

void CaptureSession::appendPreRollFrame(const float* const* const channels,
                                        const std::uint32_t inputChannels,
                                        const std::uint32_t frame) noexcept {
    if (preRollCapacityFrames_ == 0U)
        return;
    const auto destinationFrame = preRollWriteFrame_ % preRollCapacityFrames_;
    for (std::uint32_t channel = 0U; channel < spec_.channels; ++channel) {
        auto sample = 0.0F;
        if (channels != nullptr && channel < inputChannels && channels[channel] != nullptr)
            sample = channels[channel][frame];
        preRoll_[static_cast<std::size_t>(destinationFrame * spec_.channels + channel)] =
            std::isfinite(sample) ? sample : 0.0F;
    }
    ++preRollWriteFrame_;
    preRollValidFrames_ = std::min(preRollValidFrames_ + 1U, preRollCapacityFrames_);
}

void CaptureSession::flushPreRoll() noexcept {
    auto remaining = preRollValidFrames_;
    auto sourceFrame = (preRollWriteFrame_ + preRollCapacityFrames_ - preRollValidFrames_) %
                       std::max<std::uint64_t>(preRollCapacityFrames_, 1U);
    while (remaining > 0U) {
        std::uint16_t blockIndex = 0U;
        auto* destination = fifo_.beginAudioWrite(blockIndex);
        if (destination == nullptr) {
            incomplete_.store(true, std::memory_order_relaxed);
            overflowCount_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        const auto count = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(remaining, fifo_.maximumFramesPerBlock()));
        for (std::uint32_t frame = 0U; frame < count; ++frame)
            for (std::uint32_t channel = 0U; channel < spec_.channels; ++channel)
                destination[static_cast<std::size_t>(frame * spec_.channels + channel)] =
                    preRoll_[static_cast<std::size_t>(
                        ((sourceFrame + frame) % preRollCapacityFrames_) * spec_.channels +
                        channel)];
        if (!fifo_.commitAudioWrite(blockIndex, count)) {
            juce::ignoreUnused(fifo_.releaseReadBlock(blockIndex));
            incomplete_.store(true, std::memory_order_relaxed);
            overflowCount_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        framesAccepted_.fetch_add(count, std::memory_order_relaxed);
        remaining -= count;
        sourceFrame = (sourceFrame + count) % preRollCapacityFrames_;
    }
}

void CaptureSession::captureFrames(const float* const* const channels,
                                   const std::uint32_t inputChannels, std::uint32_t firstFrame,
                                   std::uint32_t frames) noexcept {
    while (frames > 0U) {
        std::uint16_t blockIndex = 0U;
        auto* destination = fifo_.beginAudioWrite(blockIndex);
        if (destination == nullptr) {
            incomplete_.store(true, std::memory_order_relaxed);
            overflowCount_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        const auto count = std::min(frames, fifo_.maximumFramesPerBlock());
        for (std::uint32_t frame = 0U; frame < count; ++frame)
            for (std::uint32_t channel = 0U; channel < spec_.channels; ++channel) {
                auto sample = 0.0F;
                if (channels != nullptr && channel < inputChannels && channels[channel] != nullptr)
                    sample = channels[channel][firstFrame + frame];
                destination[static_cast<std::size_t>(frame * spec_.channels + channel)] =
                    std::isfinite(sample) ? sample : 0.0F;
            }
        if (!fifo_.commitAudioWrite(blockIndex, count)) {
            juce::ignoreUnused(fifo_.releaseReadBlock(blockIndex));
            incomplete_.store(true, std::memory_order_relaxed);
            overflowCount_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        framesAccepted_.fetch_add(count, std::memory_order_relaxed);
        firstFrame += count;
        frames -= count;
    }
}

void CaptureSession::processInput(const float* const* const channels,
                                  const std::uint32_t channelCount,
                                  const std::uint32_t frames) noexcept {
    if (frames == 0U)
        return;
    auto currentState = state_.load(std::memory_order_acquire);
    if (currentState != CaptureState::armed && currentState != CaptureState::waitingForThreshold &&
        currentState != CaptureState::recording)
        return;

    callbackActive_.store(true, std::memory_order_release);
    currentState = state_.load(std::memory_order_acquire);
    if (currentState != CaptureState::armed && currentState != CaptureState::waitingForThreshold &&
        currentState != CaptureState::recording) {
        callbackActive_.store(false, std::memory_order_release);
        return;
    }

    auto peak = 0.0F;
    if (currentState == CaptureState::waitingForThreshold) {
        for (std::uint32_t frame = 0U; frame < frames; ++frame) {
            const auto framePeak = inputFramePeak(channels, channelCount, frame);
            peak = std::max(peak, framePeak);
            if (framePeak >= thresholdLinear_) {
                auto expected = CaptureState::waitingForThreshold;
                if (state_.compare_exchange_strong(expected, CaptureState::recording,
                                                   std::memory_order_acq_rel)) {
                    flushPreRoll();
                    captureFrames(channels, channelCount, frame, frames - frame);
                    currentState = CaptureState::recording;
                }
                break;
            }
            appendPreRollFrame(channels, channelCount, frame);
        }
    } else {
        for (std::uint32_t frame = 0U; frame < frames; ++frame)
            peak = std::max(peak, inputFramePeak(channels, channelCount, frame));
        if (currentState == CaptureState::armed)
            for (std::uint32_t frame = 0U; frame < frames; ++frame)
                appendPreRollFrame(channels, channelCount, frame);
        else if (currentState == CaptureState::recording)
            captureFrames(channels, channelCount, 0U, frames);
    }
    inputPeak_.store(peak, std::memory_order_relaxed);
    callbackActive_.store(false, std::memory_order_release);
    if (stopRequested_.load(std::memory_order_acquire) &&
        (currentState == CaptureState::recording ||
         currentState == CaptureState::waitingForThreshold || currentState == CaptureState::armed))
        juce::ignoreUnused(state_.compare_exchange_strong(currentState, CaptureState::stopping,
                                                          std::memory_order_acq_rel));
}

void CaptureSession::requestStop() noexcept {
    stopRequested_.store(true, std::memory_order_release);
    if (!callbackActive_.load(std::memory_order_acquire)) {
        auto state = state_.load(std::memory_order_acquire);
        while (state == CaptureState::recording || state == CaptureState::waitingForThreshold ||
               state == CaptureState::armed)
            if (state_.compare_exchange_weak(state, CaptureState::stopping,
                                             std::memory_order_acq_rel))
                break;
    }
}

void CaptureSession::cancel() noexcept {
    const auto state = state_.load(std::memory_order_acquire);
    if (state != CaptureState::idle && state != CaptureState::completed &&
        state != CaptureState::failed)
        state_.store(CaptureState::cancelled, std::memory_order_release);
}

CaptureStatus CaptureSession::status() const noexcept {
    CaptureStatus value;
    value.state = state_.load(std::memory_order_acquire);
    value.active = value.state == CaptureState::armed ||
                   value.state == CaptureState::waitingForThreshold ||
                   value.state == CaptureState::recording ||
                   value.state == CaptureState::stopping || value.state == CaptureState::finalizing;
    value.incomplete = incomplete_.load(std::memory_order_acquire);
    value.framesAccepted = framesAccepted_.load(std::memory_order_acquire);
    value.framesWritten = framesWritten_.load(std::memory_order_acquire);
    value.overflowCount = overflowCount_.load(std::memory_order_acquire);
    value.inputPeak = inputPeak_.load(std::memory_order_acquire);
    return value;
}

void CaptureSession::failWriter(juce::String message) {
    {
        const std::lock_guard lock{controlMutex_};
        failureMessage_ = std::move(message);
    }
    state_.store(CaptureState::failed, std::memory_order_release);
    writerReady_.store(true, std::memory_order_release);
    readyCondition_.notify_all();
}

void CaptureSession::writerLoop() {
    if (finalFile_.getParentDirectory().createDirectory().failed()) {
        failWriter("Capture destination directory could not be created");
        return;
    }
    auto stream = temporaryFile_.createOutputStream();
    if (stream == nullptr || !stream->openedOk()) {
        stream.reset();
        if (temporaryFile_.existsAsFile())
            juce::ignoreUnused(temporaryFile_.deleteFile());
        failWriter("Capture temporary file could not be opened");
        return;
    }
    std::unique_ptr<juce::OutputStream> output{std::move(stream)};
    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(spec_.sampleRate)
                             .withNumChannels(static_cast<int>(spec_.channels))
                             .withBitsPerSample(24);
    auto writer = format.createWriterFor(output, options);
    if (writer == nullptr) {
        output.reset();
        if (temporaryFile_.existsAsFile())
            juce::ignoreUnused(temporaryFile_.deleteFile());
        failWriter("Capture WAV writer could not be created");
        return;
    }
    juce::AudioBuffer<float> block{static_cast<int>(spec_.channels),
                                   static_cast<int>(spec_.maximumFramesPerBlock)};
    writerReady_.store(true, std::memory_order_release);
    readyCondition_.notify_all();

    for (;;) {
        const auto currentState = state_.load(std::memory_order_acquire);
        if (currentState == CaptureState::cancelled) {
            writer.reset();
            if (temporaryFile_.existsAsFile())
                juce::ignoreUnused(temporaryFile_.deleteFile());
            return;
        }

        CaptureBlockDescriptor descriptor;
        if (fifo_.tryPopReady(descriptor)) {
            const auto* source = fifo_.readData(descriptor.blockIndex);
            if (source == nullptr) {
                writer.reset();
                if (temporaryFile_.existsAsFile())
                    juce::ignoreUnused(temporaryFile_.deleteFile());
                failWriter("Capture FIFO returned an invalid block");
                return;
            }
            for (std::uint32_t frame = 0U; frame < descriptor.validFrames; ++frame)
                for (std::uint32_t channel = 0U; channel < spec_.channels; ++channel)
                    block.setSample(
                        static_cast<int>(channel), static_cast<int>(frame),
                        source[static_cast<std::size_t>(frame * spec_.channels + channel)]);
            const auto written = writer->writeFromAudioSampleBuffer(
                block, 0, static_cast<int>(descriptor.validFrames));
            juce::ignoreUnused(fifo_.releaseReadBlock(descriptor.blockIndex));
            if (!written) {
                failWriter("Capture WAV write failed");
                writer.reset();
                if (temporaryFile_.existsAsFile())
                    juce::ignoreUnused(temporaryFile_.deleteFile());
                return;
            }
            framesWritten_.fetch_add(descriptor.validFrames, std::memory_order_relaxed);
            continue;
        }

        if (currentState == CaptureState::stopping &&
            !callbackActive_.load(std::memory_order_acquire)) {
            state_.store(CaptureState::finalizing, std::memory_order_release);
            writer.reset();
            if (incomplete_.load(std::memory_order_acquire) ||
                framesWritten_.load(std::memory_order_acquire) == 0U) {
                if (temporaryFile_.existsAsFile())
                    juce::ignoreUnused(temporaryFile_.deleteFile());
                failWriter(incomplete_.load(std::memory_order_acquire)
                               ? "Capture overflowed; incomplete output was rejected"
                               : "Capture contains no audio");
                return;
            }
            std::unique_ptr<juce::AudioFormatReader> validation(
                format.createReaderFor(temporaryFile_.createInputStream().release(), true));
            if (validation == nullptr || validation->numChannels != spec_.channels ||
                validation->lengthInSamples !=
                    static_cast<juce::int64>(framesWritten_.load(std::memory_order_acquire)) ||
                std::abs(validation->sampleRate - spec_.sampleRate) >= 0.5) {
                validation.reset();
                juce::ignoreUnused(temporaryFile_.deleteFile());
                failWriter("Capture WAV validation failed");
                return;
            }
            validation.reset();
            if (!temporaryFile_.moveFileTo(finalFile_)) {
                juce::ignoreUnused(temporaryFile_.deleteFile());
                failWriter("Capture final publication failed");
                return;
            }
            state_.store(CaptureState::completed, std::memory_order_release);
            return;
        }
        juce::Thread::sleep(1);
    }
}

void CaptureSession::shutdown() {
    if (writerThread_.joinable()) {
        cancel();
        writerThread_.join();
    }
    if (temporaryFile_.existsAsFile())
        juce::ignoreUnused(temporaryFile_.deleteFile());
    if (state_.load(std::memory_order_acquire) != CaptureState::completed)
        state_.store(CaptureState::idle, std::memory_order_release);
}

juce::File CaptureSession::completedFile() const {
    return state_.load(std::memory_order_acquire) == CaptureState::completed ? finalFile_
                                                                             : juce::File{};
}

CaptureTarget CaptureSession::completedTarget() const {
    return state_.load(std::memory_order_acquire) == CaptureState::completed ? spec_.target
                                                                             : CaptureTarget{};
}

juce::String CaptureSession::failureMessage() const {
    const std::lock_guard lock{controlMutex_};
    return failureMessage_;
}
} // namespace padflow
