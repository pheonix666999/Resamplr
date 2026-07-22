#include "SmokeScenario.h"

#include "App/ApplicationController.h"
#include "App/ProductInfo.h"
#include "Model/Project.h"
#include "Serialization/ProjectSerializer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <numbers>

namespace padflow {
SmokeResult runSmokeScenario() {
    constexpr auto sampleRate = 48000.0;
    constexpr auto frameCount = 4800;
    constexpr auto frequency = 440.0;
    constexpr auto amplitude = 0.25F;

    auto temporaryDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getNonexistentChildFile("padflow-smoke", {}, true);
    if (!temporaryDirectory.createDirectory())
        return {false, "SMOKE failure: could not create temporary directory"};

    const auto cleanup = juce::ScopeGuard([&] { temporaryDirectory.deleteRecursively(); });
    const auto projectFile = temporaryDirectory.getChildFile("smoke.padflow");

    ApplicationController controller;
    controller.createEmptyProject("Smoke Project", "00000000-0000-4000-8000-000000000001");
    const auto& project = controller.project();
    const auto saveResult = ProjectSerializer::save(project, projectFile);
    if (!saveResult.succeeded)
        return {false, "SMOKE failure: " + saveResult.message};

    auto loaded = Project::createEmpty();
    const auto loadResult = ProjectSerializer::load(projectFile, loaded);
    if (loadResult.failed() || loaded.uuid() != project.uuid() || loaded.name() != project.name())
        return {false, "SMOKE failure: schema-v1 round trip failed"};

    juce::AudioBuffer<float> rendered{1, frameCount};
    auto* samples = rendered.getWritePointer(0);
    double sumSquares = 0.0;
    for (int frame = 0; frame < frameCount; ++frame) {
        const auto phase =
            2.0 * std::numbers::pi * frequency * static_cast<double>(frame) / sampleRate;
        const auto sample = amplitude * static_cast<float>(std::sin(phase));
        samples[frame] = sample;
        if (!std::isfinite(sample))
            return {false, "SMOKE failure: offline render produced a non-finite sample"};
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
    }

    const auto rms = std::sqrt(sumSquares / static_cast<double>(frameCount));
    if (rendered.getNumSamples() != frameCount || rms < 0.1 || rms > 0.2)
        return {false, "SMOKE failure: offline render duration or non-silence check failed"};

    juce::ignoreUnused(cleanup);
    return {true,
            "SMOKE success: metadata, schema-v1 round trip, and synthetic offline render passed"};
}
} // namespace padflow
