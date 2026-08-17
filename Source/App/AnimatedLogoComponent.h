#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <vector>

namespace padflow {
class AnimatedLogoComponent final : public juce::Component, private juce::Timer {
  public:
    AnimatedLogoComponent() = default;
    ~AnimatedLogoComponent() override;

    [[nodiscard]] bool loadGif(const void* data, std::size_t dataSize,
                               juce::Rectangle<int> sourceCrop);
    void setStaticImage(juce::Image image, juce::Rectangle<int> sourceCrop);
    [[nodiscard]] std::size_t frameCount() const noexcept;
    [[nodiscard]] std::size_t currentFrameIndex() const noexcept;
    void advanceFrameForTesting();

    void paint(juce::Graphics& graphics) override;

  private:
    struct Frame final {
        juce::Image image;
        int durationMilliseconds{50};
    };

    void timerCallback() override;
    void advanceFrame();

    std::vector<Frame> frames_;
    std::size_t currentFrame_{0U};
};
} // namespace padflow
