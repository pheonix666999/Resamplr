#include "AnimatedLogoComponent.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace padflow {
namespace {
constexpr auto fallbackFrameDurationMilliseconds = 50;
constexpr auto maximumGifFrames = 256U;
constexpr auto maximumGifDimension = 4096U;
constexpr auto maximumGifPixels = 16U * 1024U * 1024U;
constexpr auto storedFrameWidth = 96;
constexpr auto storedFrameHeight = 128;

std::uint16_t readLittleEndian16(const std::uint8_t* const data) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[0]) |
                                      (static_cast<std::uint16_t>(data[1]) << 8U));
}

bool skipSubBlocks(const std::uint8_t* const data, const std::size_t dataSize,
                   std::size_t& position) noexcept {
    while (position < dataSize) {
        const auto blockSize = static_cast<std::size_t>(data[position++]);
        if (blockSize == 0U)
            return true;
        if (blockSize > dataSize - position)
            return false;
        position += blockSize;
    }
    return false;
}

juce::Colour gifBackgroundColour(const std::uint8_t* const data,
                                 const std::size_t globalColourCount,
                                 const std::size_t prefixSize) noexcept {
    if (globalColourCount == 0U || prefixSize < 13U)
        return juce::Colours::transparentBlack;
    const auto backgroundIndex = static_cast<std::size_t>(data[11]);
    if (backgroundIndex >= globalColourCount)
        return juce::Colours::transparentBlack;
    const auto paletteOffset = 13U + backgroundIndex * 3U;
    if (paletteOffset + 2U >= prefixSize)
        return juce::Colours::transparentBlack;
    return juce::Colour{data[paletteOffset], data[paletteOffset + 1U], data[paletteOffset + 2U]};
}
} // namespace

AnimatedLogoComponent::~AnimatedLogoComponent() {
    stopTimer();
}

bool AnimatedLogoComponent::loadGif(const void* const sourceData, const std::size_t dataSize,
                                    const juce::Rectangle<int> sourceCrop) {
    stopTimer();
    frames_.clear();
    currentFrame_ = 0U;

    const auto* const data = static_cast<const std::uint8_t*>(sourceData);
    if (data == nullptr || dataSize < 14U || data[0] != 'G' || data[1] != 'I' || data[2] != 'F')
        return false;

    const auto logicalWidth = static_cast<std::size_t>(readLittleEndian16(data + 6U));
    const auto logicalHeight = static_cast<std::size_t>(readLittleEndian16(data + 8U));
    if (logicalWidth == 0U || logicalHeight == 0U || logicalWidth > maximumGifDimension ||
        logicalHeight > maximumGifDimension || logicalWidth > maximumGifPixels / logicalHeight)
        return false;

    const auto packedFields = data[10];
    const std::size_t globalColourCount =
        (packedFields & 0x80U) != 0U ? (1U << ((packedFields & 0x07U) + 1U)) : 0U;
    const auto globalColourBytes = globalColourCount * 3U;
    if (globalColourBytes > dataSize - 13U)
        return false;
    const std::size_t prefixSize = 13U + globalColourBytes;
    std::size_t position = prefixSize;

    juce::Image canvas{juce::Image::ARGB, static_cast<int>(logicalWidth),
                       static_cast<int>(logicalHeight), true};
    const auto background = gifBackgroundColour(data, globalColourCount, prefixSize);
    {
        juce::Graphics graphics{canvas};
        graphics.fillAll(background);
    }

    const auto logicalBounds = canvas.getBounds();
    auto crop = sourceCrop.getIntersection(logicalBounds);
    if (crop.isEmpty())
        crop = logicalBounds;

    std::size_t controlStart = 0U;
    std::size_t controlEnd = 0U;
    int disposalMethod = 0;
    int durationMilliseconds = fallbackFrameDurationMilliseconds;

    while (position < dataSize && frames_.size() < maximumGifFrames) {
        const auto introducer = data[position];
        if (introducer == 0x3bU)
            break;

        if (introducer == 0x21U) {
            if (position + 2U > dataSize)
                return false;
            const auto extensionStart = position;
            const auto extensionLabel = data[position + 1U];
            position += 2U;
            if (!skipSubBlocks(data, dataSize, position))
                return false;
            if (extensionLabel == 0xf9U && position - extensionStart >= 8U &&
                data[extensionStart + 2U] >= 4U) {
                controlStart = extensionStart;
                controlEnd = position;
                disposalMethod = static_cast<int>((data[extensionStart + 3U] >> 2U) & 0x07U);
                const auto delayCentiseconds = readLittleEndian16(data + extensionStart + 4U);
                durationMilliseconds = delayCentiseconds == 0U
                                           ? fallbackFrameDurationMilliseconds
                                           : std::max(10, static_cast<int>(delayCentiseconds) * 10);
            }
            continue;
        }

        if (introducer != 0x2cU) {
            ++position;
            continue;
        }

        if (position + 10U > dataSize)
            return false;
        const auto imageStart = position;
        const auto left = static_cast<int>(readLittleEndian16(data + position + 1U));
        const auto top = static_cast<int>(readLittleEndian16(data + position + 3U));
        const auto imageWidth = static_cast<int>(readLittleEndian16(data + position + 5U));
        const auto imageHeight = static_cast<int>(readLittleEndian16(data + position + 7U));
        const auto imagePackedFields = data[position + 9U];
        position += 10U;

        const auto localColourCount =
            (imagePackedFields & 0x80U) != 0U ? (1U << ((imagePackedFields & 0x07U) + 1U)) : 0U;
        const auto localColourBytes = localColourCount * 3U;
        if (localColourBytes > dataSize - position)
            return false;
        position += localColourBytes;
        if (position >= dataSize)
            return false;
        ++position;
        if (!skipSubBlocks(data, dataSize, position))
            return false;
        const auto imageEnd = position;

        juce::MemoryOutputStream frameData;
        frameData.write(data, 6U);
        const std::uint8_t localLogicalSize[]{
            static_cast<std::uint8_t>(static_cast<unsigned int>(imageWidth) & 0xffU),
            static_cast<std::uint8_t>((static_cast<unsigned int>(imageWidth) >> 8U) & 0xffU),
            static_cast<std::uint8_t>(static_cast<unsigned int>(imageHeight) & 0xffU),
            static_cast<std::uint8_t>((static_cast<unsigned int>(imageHeight) >> 8U) & 0xffU)};
        frameData.write(localLogicalSize, sizeof(localLogicalSize));
        frameData.write(data + 10U, prefixSize - 10U);
        if (controlEnd > controlStart)
            frameData.write(data + controlStart, controlEnd - controlStart);
        frameData.write(data + imageStart, 1U);
        const std::uint8_t localOrigin[]{0U, 0U, 0U, 0U};
        frameData.write(localOrigin, sizeof(localOrigin));
        frameData.write(data + imageStart + 5U, imageEnd - imageStart - 5U);
        const std::uint8_t trailer = 0x3bU;
        frameData.write(&trailer, 1U);
        auto decoded =
            juce::ImageFileFormat::loadFrom(frameData.getData(), frameData.getDataSize());
        if (!decoded.isValid() || decoded.getWidth() != imageWidth ||
            decoded.getHeight() != imageHeight)
            return false;

        juce::Image previousCanvas;
        if (disposalMethod == 3)
            previousCanvas = canvas.createCopy();
        {
            juce::Graphics graphics{canvas};
            graphics.drawImageAt(decoded, left, top);
        }

        auto stored = canvas.getClippedImage(crop).rescaled(storedFrameWidth, storedFrameHeight,
                                                            juce::Graphics::highResamplingQuality);
        frames_.push_back({std::move(stored), durationMilliseconds});

        if (disposalMethod == 2) {
            juce::Graphics graphics{canvas};
            graphics.setColour(background);
            graphics.fillRect(
                juce::Rectangle<int>{left, top, imageWidth, imageHeight}.getIntersection(
                    logicalBounds));
        } else if (disposalMethod == 3 && previousCanvas.isValid()) {
            canvas = std::move(previousCanvas);
        }

        controlStart = 0U;
        controlEnd = 0U;
        disposalMethod = 0;
        durationMilliseconds = fallbackFrameDurationMilliseconds;
    }

    if (frames_.empty())
        return false;
    if (frames_.size() > 1U)
        startTimer(frames_.front().durationMilliseconds);
    repaint();
    return true;
}

void AnimatedLogoComponent::setStaticImage(juce::Image image,
                                           const juce::Rectangle<int> sourceCrop) {
    stopTimer();
    frames_.clear();
    currentFrame_ = 0U;
    if (!image.isValid()) {
        repaint();
        return;
    }
    auto crop = sourceCrop.getIntersection(image.getBounds());
    if (crop.isEmpty())
        crop = image.getBounds();
    frames_.push_back({image.getClippedImage(crop).rescaled(storedFrameWidth, storedFrameHeight,
                                                            juce::Graphics::highResamplingQuality),
                       fallbackFrameDurationMilliseconds});
    repaint();
}

std::size_t AnimatedLogoComponent::frameCount() const noexcept {
    return frames_.size();
}

std::size_t AnimatedLogoComponent::currentFrameIndex() const noexcept {
    return currentFrame_;
}

void AnimatedLogoComponent::advanceFrameForTesting() {
    advanceFrame();
}

void AnimatedLogoComponent::paint(juce::Graphics& graphics) {
    if (frames_.empty())
        return;
    graphics.drawImageWithin(frames_[currentFrame_].image, 0, 0, getWidth(), getHeight(),
                             juce::RectanglePlacement::centred, false);
}

void AnimatedLogoComponent::timerCallback() {
    advanceFrame();
    if (frames_.size() > 1U)
        startTimer(frames_[currentFrame_].durationMilliseconds);
}

void AnimatedLogoComponent::advanceFrame() {
    if (frames_.size() < 2U)
        return;
    currentFrame_ = (currentFrame_ + 1U) % frames_.size();
    repaint();
}
} // namespace padflow
