#pragma once

#include "AudioCommandQueue.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace padflow {
enum class CaptureSource : std::uint8_t { input, master, pad, effectBus, skipback };

struct CaptureSpec final {
    CaptureSource source{CaptureSource::input};
    juce::File destination;
    double sampleRate{0.0};
    std::uint32_t channels{0U};
    std::uint32_t maximumFramesPerBlock{0U};
    std::uint32_t fifoBlockCount{0U};
};

struct CaptureStatus final {
    bool active{false};
    bool incomplete{false};
    std::uint64_t framesAccepted{0U};
    std::uint64_t overflowCount{0U};
};

struct CaptureBlockDescriptor final {
    std::uint16_t blockIndex{0U};
    std::uint32_t validFrames{0U};
};

class CaptureFifo final {
  public:
    static constexpr std::size_t maximumBlocks = 256U;

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
    virtual void requestStop() noexcept = 0;
    virtual void cancel() noexcept = 0;
    [[nodiscard]] virtual CaptureStatus status() const noexcept = 0;
};
} // namespace padflow
