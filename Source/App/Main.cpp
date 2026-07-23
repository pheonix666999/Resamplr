#include "Application.h"

#include "Smoke/SmokeScenario.h"

#include <juce_events/juce_events.h>

#include <cstdio>

juce::JUCEApplicationBase* juce_CreateApplication() {
    return new padflow::Application();
}

JUCE_MAIN_FUNCTION {
    const auto arguments = juce::JUCEApplicationBase::getCommandLineParameterArray();
    if (arguments.contains("--headless-smoke-test")) {
        if (!arguments.contains("--no-audio-device")) {
            std::fputs("SMOKE failure: --no-audio-device is required\n", stderr);
            return 2;
        }

        const auto result = padflow::runSmokeScenario();
        const auto* diagnostics = result.diagnostics.toRawUTF8();
        std::fprintf(result.succeeded ? stdout : stderr, "%s\n", diagnostics);
        return result.succeeded ? 0 : 1;
    }

    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
    return juce::JUCEApplicationBase::main(JUCE_MAIN_FUNCTION_ARGS);
}
