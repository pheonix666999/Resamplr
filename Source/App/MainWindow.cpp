#include "MainWindow.h"

#include <memory>

namespace padflow {
FoundationView::FoundationView() {
    productLabel_.setText("PadFlow", juce::dontSendNotification);
    productLabel_.setJustificationType(juce::Justification::centred);
    productLabel_.setFont(juce::FontOptions{42.0F, juce::Font::bold});
    productLabel_.setColour(juce::Label::textColourId, juce::Colour{0xff60d6c9});
    productLabel_.setTitle("PadFlow product name");
    addAndMakeVisible(productLabel_);

    statusLabel_.setText("Milestone 0 foundation - no sampler features are enabled",
                         juce::dontSendNotification);
    statusLabel_.setJustificationType(juce::Justification::centred);
    statusLabel_.setFont(juce::FontOptions{17.0F});
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour{0xffc8d1d8});
    statusLabel_.setTitle("Current implementation status");
    addAndMakeVisible(statusLabel_);

    setTitle("PadFlow Milestone 0 foundation view");
}

void FoundationView::paint(juce::Graphics& graphics) {
    graphics.fillAll(juce::Colour{0xff171b22});

    const auto panel = getLocalBounds().reduced(48).toFloat();
    graphics.setColour(juce::Colour{0xff232a34});
    graphics.fillRoundedRectangle(panel, 18.0F);
    graphics.setColour(juce::Colour{0xff394655});
    graphics.drawRoundedRectangle(panel, 18.0F, 1.0F);
}

void FoundationView::resized() {
    auto centre = getLocalBounds().reduced(80).withSizeKeepingCentre(720, 150);
    productLabel_.setBounds(centre.removeFromTop(86));
    statusLabel_.setBounds(centre);
}

MainWindow::MainWindow(const juce::String& title)
    : juce::DocumentWindow(title, juce::Colour{0xff171b22}, juce::DocumentWindow::allButtons,
                           true) {
    setUsingNativeTitleBar(true);
    auto content = std::make_unique<FoundationView>();
    setContentOwned(content.release(), true);
    setResizable(true, false);
    setResizeLimits(900, 600, 3840, 2160);
    centreWithSize(1180, 760);
    setVisible(true);
}

void MainWindow::closeButtonPressed() {
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
} // namespace padflow
