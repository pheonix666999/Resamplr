#pragma once

#include "App/ApplicationController.h"
#include "Audio/AudioRuntime.h"
#include "Audio/PlaybackStatePublisher.h"
#include "Input/InputRouter.h"
#include "Sampling/SampleAsset.h"
#include "Sampling/SamplePreviewController.h"
#include "Sequencing/Scheduler.h"
#include "Utilities/BackgroundJobSystem.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace padflow {
class PadFlowAudioProcessor final : public juce::AudioProcessor {
  public:
    PadFlowAudioProcessor();
    ~PadFlowAudioProcessor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    [[nodiscard]] bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    [[nodiscard]] juce::AudioProcessorEditor* createEditor() override;
    [[nodiscard]] bool hasEditor() const override;
    [[nodiscard]] const juce::String getName() const override;
    [[nodiscard]] bool acceptsMidi() const override;
    [[nodiscard]] bool producesMidi() const override;
    [[nodiscard]] bool isMidiEffect() const override;
    [[nodiscard]] double getTailLengthSeconds() const override;
    [[nodiscard]] int getNumPrograms() override;
    [[nodiscard]] int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    [[nodiscard]] const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destinationData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    [[nodiscard]] ApplicationController& controller() noexcept;
    [[nodiscard]] BackgroundJobSystem& jobs() noexcept;
    [[nodiscard]] SampleAssetRegistry& assets() noexcept;
    [[nodiscard]] AudioRuntime& runtime() noexcept;
    [[nodiscard]] PlaybackStatePublisher& publisher() noexcept;
    [[nodiscard]] InputRouter& input() noexcept;
    [[nodiscard]] SamplePreviewController& preview() noexcept;

  private:
    ApplicationController controller_;
    BackgroundJobSystem jobs_;
    SampleAssetRegistry assets_;
    AudioRuntime runtime_;
    PlaybackStatePublisher publisher_;
    InputRouter input_;
    SamplePreviewController preview_;
    ScheduledCommandBuffer hostCommands_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadFlowAudioProcessor)
};
} // namespace padflow
