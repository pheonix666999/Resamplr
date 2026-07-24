#pragma once

#include "ApplicationController.h"
#include "Audio/AudioRuntime.h"
#include "Audio/PlaybackStatePublisher.h"
#include "Input/InputRouter.h"
#include "MainWindow.h"
#include "Sampling/SampleAsset.h"
#include "Sampling/SamplePreviewController.h"
#include "Utilities/BackgroundJobSystem.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace padflow {
class Application final : public juce::JUCEApplication {
  public:
    [[nodiscard]] const juce::String getApplicationName() override;
    [[nodiscard]] const juce::String getApplicationVersion() override;
    [[nodiscard]] bool moreThanOneInstanceAllowed() override;

    void initialise(const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override;
    void anotherInstanceStarted(const juce::String& commandLine) override;

  private:
    std::unique_ptr<ApplicationController> controller_;
    std::unique_ptr<BackgroundJobSystem> jobs_;
    std::unique_ptr<SampleAssetRegistry> assets_;
    std::unique_ptr<PlaybackStatePublisher> publisher_;
    std::unique_ptr<AudioRuntime> runtime_;
    std::unique_ptr<InputRouter> input_;
    std::unique_ptr<SamplePreviewController> preview_;
    std::unique_ptr<MainWindow> mainWindow_;
};
} // namespace padflow
