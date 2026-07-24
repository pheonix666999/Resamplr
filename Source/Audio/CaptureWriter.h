#pragma once

#include "AudioCommandQueue.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace padflow {
enum class CaptureSource : std::uint8_t { input, master, pad, effectBus, skipback };
enum class CaptureMode : std::uint8_t { manual, threshold };
enum class CaptureState : std::uint8_t {
    idle,
    armed,
    waitingForThreshold,
    recording,
    stopping,
    finalizing,
    completed,
    cancelled,
    failed
};

struct CaptureTarget final {
    juce::String projectUuid;
    juce::String padUuid;
    juce::String layerUuid;
    std::uint64_t projectRevision{0U};

    [[nodiscard]] friend bool operator==(const CaptureTarget&, const CaptureTarget&) = default;
};

struct CaptureSpec final {
    CaptureSource source{CaptureSource::input};
    juce::File destination;
    double sampleRate{0.0};
    std::uint32_t channels{0U};
    std::uint32_t maximumFramesPerBlock{0U};
    std::uint32_t fifoBlockCount{0U};
    CaptureMode mode{CaptureMode::manual};
    float thresholdDecibels{-24.0F};
    std::uint32_t preRollMilliseconds{0U};
    juce::String sessionUuid;
    CaptureTarget target;
};

struct CaptureStatus final {
    bool active{false};
    bool incomplete{false};
    CaptureState state{CaptureState::idle};
    std::uint64_t framesAccepted{0U};
    std::uint64_t framesWritten{0U};
    std::uint64_t overflowCount{0U};
    float inputPeak{0.0F};
};

struct CaptureBlockDescriptor final {
    std::uint16_t blockIndex{0U};
    std::uint32_t validFrames{0U};
};

class CaptureFifo final {
  public:
    static constexpr std::size_t maximumBlocks = 1024U;

    [[nodiscard]] bool configure(std::uint32_t blockCount, std::uint32_t maximumFramesPerBlock,
                                 std::uint32_t channels);

    // Audio-thread side. These methods do not allocate, lock, or perform file I/O.
    [[nodiscard]] float* beginAudioWrite(std::uint16_t& blockIndex) noexcept;
    [[nodiscard]] bool commitAudioWrite(std::uint16_t blockIndex,
                                        std::uint32_t validFrames) noexcept;

    // Writer-thread side.
    [[nodiscard]] bool tryPopReady(CaptureBlockDescriptor& descriptor) noexcept;
    [[nodiscard]] const float* readData(std::uint16_t blockIndex) const noexcept;
    [[nodiscard]] bool releaseReadBlock(std::uint16_t blockIndex) noexcept;

    [[nodiscard]] std::uint32_t maximumFramesPerBlock() const noexcept;
    [[nodiscard]] std::uint32_t channelCount() const noexcept;

  private:
    std::vector<float> storage_;
    std::uint32_t blockCount_{0U};
    std::uint32_t maximumFramesPerBlock_{0U};
    std::uint32_t channels_{0U};
    SpscQueue<std::uint16_t, maximumBlocks> freeBlocks_;
    SpscQueue<CaptureBlockDescriptor, maximumBlocks> readyBlocks_;
};

class ICaptureWriter {
  public:
    virtual ~ICaptureWriter() = default;
    [[nodiscard]] virtual bool prepare(const CaptureSpec& spec) = 0;
    [[nodiscard]] virtual CaptureFifo* fifo() noexcept = 0;
    [[nodiscard]] virtual bool startManual() noexcept = 0;
    virtual void processInput(const float* const* channels, std::uint32_t channelCount,
                              std::uint32_t frames) noexcept = 0;
    virtual void requestStop() noexcept = 0;
    virtual void cancel() noexcept = 0;
    [[nodiscard]] virtual CaptureStatus status() const noexcept = 0;
};

class CaptureSession final : public ICaptureWriter {
  public:
    CaptureSession() = default;
    ~CaptureSession() override;

    [[nodiscard]] bool prepare(const CaptureSpec& spec) override;
    [[nodiscard]] CaptureFifo* fifo() noexcept override;
    [[nodiscard]] bool startManual() noexcept override;
    void processInput(const float* const* channels, std::uint32_t channelCount,
                      std::uint32_t frames) noexcept override;
    void requestStop() noexcept override;
    void cancel() noexcept override;
    [[nodiscard]] CaptureStatus status() const noexcept override;

    void shutdown();
    [[nodiscard]] juce::File completedFile() const;
    [[nodiscard]] CaptureTarget completedTarget() const;
    [[nodiscard]] juce::String failureMessage() const;

  private:
    void writerLoop();
    void captureFrames(const float* const* channels, std::uint32_t inputChannels,
                       std::uint32_t firstFrame, std::uint32_t frames) noexcept;
    void flushPreRoll() noexcept;
    void appendPreRollFrame(const float* const* channels, std::uint32_t inputChannels,
                            std::uint32_t frame) noexcept;
    [[nodiscard]] float inputFramePeak(const float* const* channels, std::uint32_t inputChannels,
                                       std::uint32_t frame) const noexcept;
    void failWriter(juce::String message);

    CaptureFifo fifo_;
    CaptureSpec spec_;
    std::vector<float> preRoll_;
    std::uint64_t preRollCapacityFrames_{0U};
    std::uint64_t preRollWriteFrame_{0U};
    std::uint64_t preRollValidFrames_{0U};
    juce::File finalFile_;
    juce::File temporaryFile_;
    mutable std::mutex controlMutex_;
    std::condition_variable readyCondition_;
    std::thread writerThread_;
    juce::String failureMessage_;
    std::atomic<CaptureState> state_{CaptureState::idle};
    std::atomic<bool> writerReady_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> callbackActive_{false};
    std::atomic<bool> incomplete_{false};
    std::atomic<std::uint64_t> framesAccepted_{0U};
    std::atomic<std::uint64_t> framesWritten_{0U};
    std::atomic<std::uint64_t> overflowCount_{0U};
    std::atomic<float> inputPeak_{0.0F};
    float thresholdLinear_{0.0F};
};
} // namespace padflow
