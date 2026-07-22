#include "CaptureWriter.h"

#include <limits>

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
} // namespace padflow
