#pragma once

#include "ApplicationController.h"
#include "MainWindow.h"

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
    std::unique_ptr<MainWindow> mainWindow_;
};
} // namespace padflow
