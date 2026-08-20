#pragma once

#include "App/SamplerView.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace padflow {
class PadFlowAudioProcessor;

class PadFlowPluginEditor final : public juce::AudioProcessorEditor {
  public:
    explicit PadFlowPluginEditor(PadFlowAudioProcessor& processor);
    ~PadFlowPluginEditor() override = default;

    void resized() override;

  private:
    SamplerView samplerView_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadFlowPluginEditor)
};
} // namespace padflow
