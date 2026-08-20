#include "PadFlowAudioProcessor.h"

#include "Plugin/PadFlowPluginEditor.h"
#include "Serialization/ProjectSerializer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace padflow {
PadFlowAudioProcessor::PadFlowAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      publisher_(runtime_.engine(), assets_), input_(controller_, runtime_.engine()),
      preview_(controller_, runtime_.preview()) {
    publisher_.publish(controller_.project().state());
}

PadFlowAudioProcessor::~PadFlowAudioProcessor() {
    jobs_.shutdown();
    runtime_.close();
    publisher_.clearWhenAudioIsStopped();
    assets_.clear();
}

void PadFlowAudioProcessor::prepareToPlay(const double sampleRate,
                                          const int maximumExpectedSamplesPerBlock) {
    runtime_.prepareHosted(sampleRate,
                           static_cast<std::uint32_t>(std::max(0, maximumExpectedSamplesPerBlock)));
}

void PadFlowAudioProcessor::releaseResources() {
    runtime_.releaseHosted();
}

void PadFlowAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    const auto frameCount = static_cast<std::uint32_t>(std::max(0, buffer.getNumSamples()));
    input_.collectHostMidi(midiMessages, frameCount, hostCommands_);

    std::array<const float*, 2U> inputChannels{};
    int inputChannelCount = 0;
    if (getBusCount(true) > 0 && getChannelCountOfBus(true, 0) > 0) {
        const auto inputBuffer = getBusBuffer(buffer, true, 0);
        inputChannelCount = std::min(2, inputBuffer.getNumChannels());
        for (int channel = 0; channel < inputChannelCount; ++channel)
            inputChannels[static_cast<std::size_t>(channel)] = inputBuffer.getReadPointer(channel);
    }

    auto outputBuffer = getBusBuffer(buffer, false, 0);
    std::array<float*, 2U> outputChannels{};
    const auto outputChannelCount = std::min(2, outputBuffer.getNumChannels());
    for (int channel = 0; channel < outputChannelCount; ++channel)
        outputChannels[static_cast<std::size_t>(channel)] = outputBuffer.getWritePointer(channel);

    runtime_.processHosted(inputChannels.data(), inputChannelCount, outputChannels.data(),
                           outputChannelCount, buffer.getNumSamples(), hostCommands_.view());
    for (int channel = outputChannelCount; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}

bool PadFlowAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto input = layouts.getMainInputChannelSet();
    return input.isDisabled() || input == juce::AudioChannelSet::mono() ||
           input == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorEditor* PadFlowAudioProcessor::createEditor() {
    return new PadFlowPluginEditor{*this};
}

bool PadFlowAudioProcessor::hasEditor() const {
    return true;
}

const juce::String PadFlowAudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool PadFlowAudioProcessor::acceptsMidi() const {
    return true;
}

bool PadFlowAudioProcessor::producesMidi() const {
    return false;
}

bool PadFlowAudioProcessor::isMidiEffect() const {
    return false;
}

double PadFlowAudioProcessor::getTailLengthSeconds() const {
    return 0.0;
}

int PadFlowAudioProcessor::getNumPrograms() {
    return 1;
}

int PadFlowAudioProcessor::getCurrentProgram() {
    return 0;
}

void PadFlowAudioProcessor::setCurrentProgram(const int index) {
    juce::ignoreUnused(index);
}

const juce::String PadFlowAudioProcessor::getProgramName(const int index) {
    juce::ignoreUnused(index);
    return "Default";
}

void PadFlowAudioProcessor::changeProgramName(const int index, const juce::String& newName) {
    juce::ignoreUnused(index, newName);
}

void PadFlowAudioProcessor::getStateInformation(juce::MemoryBlock& destinationData) {
    const auto manifest = ProjectSerializer::canonicalManifest(controller_.project());
    destinationData.setSize(manifest.getNumBytesAsUTF8());
    std::memcpy(destinationData.getData(), manifest.toRawUTF8(), destinationData.getSize());
}

void PadFlowAudioProcessor::setStateInformation(const void* const data, const int sizeInBytes) {
    if (data == nullptr || sizeInBytes <= 0)
        return;
    const auto manifest = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    auto restored = Project::createEmpty();
    if (ProjectSerializer::restoreCanonicalManifest(manifest, restored).failed())
        return;

    jobs_.cancelOwner(controller_.project().uuid());
    if (controller_.restoreProject(std::move(restored)).failed())
        return;
    input_.refreshFromProject();
    publisher_.publish(controller_.project().state());
}

ApplicationController& PadFlowAudioProcessor::controller() noexcept {
    return controller_;
}

BackgroundJobSystem& PadFlowAudioProcessor::jobs() noexcept {
    return jobs_;
}

SampleAssetRegistry& PadFlowAudioProcessor::assets() noexcept {
    return assets_;
}

AudioRuntime& PadFlowAudioProcessor::runtime() noexcept {
    return runtime_;
}

PlaybackStatePublisher& PadFlowAudioProcessor::publisher() noexcept {
    return publisher_;
}

InputRouter& PadFlowAudioProcessor::input() noexcept {
    return input_;
}

SamplePreviewController& PadFlowAudioProcessor::preview() noexcept {
    return preview_;
}
} // namespace padflow

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new padflow::PadFlowAudioProcessor();
}
