#include "MainWindow.h"

#include <memory>

namespace padflow {
MainWindow::MainWindow(const juce::String& title, ApplicationController& controller,
                       BackgroundJobSystem& jobs, SampleAssetRegistry& assets,
                       AudioRuntime& runtime, PlaybackStatePublisher& publisher, InputRouter& input,
                       SamplePreviewController& preview)
    : juce::DocumentWindow(title, juce::Colour{0xff171b22}, juce::DocumentWindow::allButtons, true),
      controller_(controller) {
    setUsingNativeTitleBar(true);
    auto content =
        std::make_unique<SamplerView>(controller, jobs, assets, runtime, publisher, input, preview);
    setContentOwned(content.release(), true);
    setResizable(true, false);
    setResizeLimits(1180, 760, 3840, 2160);
    const auto& state = controller_.project().state().ui;
    if (state.windowX >= 0 && state.windowY >= 0)
        setBounds(state.windowX, state.windowY, std::max(1180, state.windowWidth),
                  std::max(760, state.windowHeight));
    else
        centreWithSize(std::max(1180, state.windowWidth), std::max(760, state.windowHeight));
    setVisible(true);
}

void MainWindow::closeButtonPressed() {
    auto state = controller_.project().state().ui;
    const auto bounds = getBounds();
    state.windowX = bounds.getX();
    state.windowY = bounds.getY();
    state.windowWidth = bounds.getWidth();
    state.windowHeight = bounds.getHeight();
    juce::ignoreUnused(controller_.setUiState(state));
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
} // namespace padflow
