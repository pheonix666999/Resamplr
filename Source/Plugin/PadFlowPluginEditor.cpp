#include "PadFlowPluginEditor.h"

#include "Plugin/PadFlowAudioProcessor.h"

#include <algorithm>

namespace padflow {
PadFlowPluginEditor::PadFlowPluginEditor(PadFlowAudioProcessor& processor)
    : AudioProcessorEditor(processor),
      samplerView_(processor.controller(), processor.jobs(), processor.assets(),
                   processor.runtime(), processor.publisher(), processor.input(),
                   processor.preview()) {
    addAndMakeVisible(samplerView_);
    setResizable(true, false);
    setResizeLimits(1180, 760, 3840, 2160);
    const auto& ui = processor.controller().project().state().ui;
    setSize(std::max(1180, ui.windowWidth), std::max(760, ui.windowHeight));
}

void PadFlowPluginEditor::resized() {
    samplerView_.setBounds(getLocalBounds());
}
} // namespace padflow
