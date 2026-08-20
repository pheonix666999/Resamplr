#include "Plugin/PadFlowAudioProcessor.h"

#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

int main() {
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    auto processor = std::make_unique<padflow::PadFlowAudioProcessor>();
    processor->setRateAndBufferSizeDetails(48000.0, 512);
    processor->prepareToPlay(48000.0, 512);

    std::vector<float> pcm(8192U, 0.25F);
    padflow::SampleAssetMetadata metadata;
    metadata.assetUuid = "plugin-smoke-asset";
    metadata.displayName = "Plug-in Smoke";
    metadata.sampleRate = 48000.0;
    metadata.channelCount = 2U;
    metadata.frameCount = 4096U;
    metadata.contentFingerprint = "plugin-smoke-fingerprint";
    const auto asset = padflow::SampleAsset::create(std::move(metadata), std::move(pcm));
    if (asset == nullptr || !processor->assets().publish(asset)) {
        std::fputs("PLUGIN-001 failure: synthetic asset publication failed\n", stderr);
        return 1;
    }
    padflow::ExternalAssetReference reference;
    reference.uuid = "plugin-smoke-asset";
    reference.originalName = "plugin-smoke.wav";
    reference.format = "WAV";
    reference.contentFingerprint = "plugin-smoke-fingerprint";
    reference.channels = 2U;
    reference.sourceSampleRate = 48000.0;
    reference.frameCount = 4096U;
    reference.decodedBytes = 8192U * sizeof(float);
    reference.missing = true;
    const auto& project = processor->controller().project();
    const padflow::JobSpec target{project.uuid(), project.pad(0U).uuid, project.revision(), 0,
                                  padflow::JobKind::sampleImport};
    if (processor->controller()
            .commitImportedLayer(target, 0U, 0U, std::move(reference))
            .failed()) {
        std::fputs("PLUGIN-001 failure: synthetic model assignment failed\n", stderr);
        return 1;
    }
    auto parameters = processor->controller().project().pad(0U).parameters;
    parameters.playbackMode = padflow::PlaybackMode::gate;
    if (processor->controller().setPadParameters(0U, parameters).failed()) {
        std::fputs("PLUGIN-001 failure: gate-mode setup failed\n", stderr);
        return 1;
    }
    processor->publisher().publish(processor->controller().project().state());

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor{processor->createEditor()};
        if (editor == nullptr) {
            std::fputs("PLUGIN-001 failure: editor creation failed\n", stderr);
            return 1;
        }
    }

    juce::AudioBuffer<float> buffer{2, 512};
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)), 0);
    processor->processBlock(buffer, midi);
    bool renderedAudio = false;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
            if (!std::isfinite(buffer.getSample(channel, frame))) {
                std::fputs("PLUGIN-001 failure: non-finite audio output\n", stderr);
                return 1;
            } else {
                renderedAudio = renderedAudio || std::abs(buffer.getSample(channel, frame)) > 0.0F;
            }
    if (!renderedAudio) {
        std::fputs("PLUGIN-001 failure: host MIDI rendered silence after editor close\n", stderr);
        return 1;
    }

    buffer.clear();
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
    processor->processBlock(buffer, midi);
    if (std::abs(buffer.getSample(0, buffer.getNumSamples() - 1)) == 0.0F) {
        std::fputs("REGRESSION-PLUGIN-002 failure: same-frame release ran after retrigger\n",
                   stderr);
        return 1;
    }

    juce::MemoryBlock state;
    processor->getStateInformation(state);
    if (state.getSize() == 0U) {
        std::fputs("PLUGIN-001 failure: empty plug-in state\n", stderr);
        return 1;
    }
    processor->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    processor->releaseResources();
    std::puts("PLUGIN-001 processor, editor lifecycle, MIDI, audio, and state smoke passed");
    return 0;
}
