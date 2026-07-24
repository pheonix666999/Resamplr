#pragma once

#include "App/SamplerView.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace padflow {
class MainWindow final : public juce::DocumentWindow {
  public:
    MainWindow(const juce::String& title, ApplicationController& controller,
               BackgroundJobSystem& jobs, SampleAssetRegistry& assets, AudioRuntime& runtime,
               PlaybackStatePublisher& publisher, InputRouter& input,
               SamplePreviewController& preview);
    void closeButtonPressed() override;

  private:
    ApplicationController& controller_;
};
} // namespace padflow
