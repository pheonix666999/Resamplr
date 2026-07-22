#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace padflow {
class FoundationView final : public juce::Component {
  public:
    FoundationView();
    void paint(juce::Graphics& graphics) override;
    void resized() override;

  private:
    juce::Label productLabel_;
    juce::Label statusLabel_;
};

class MainWindow final : public juce::DocumentWindow {
  public:
    explicit MainWindow(const juce::String& title);
    void closeButtonPressed() override;
};
} // namespace padflow
