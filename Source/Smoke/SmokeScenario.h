#pragma once

#include <juce_core/juce_core.h>

namespace padflow {
struct SmokeResult final {
    bool succeeded{false};
    juce::String diagnostics;
};

[[nodiscard]] SmokeResult runSmokeScenario();
} // namespace padflow
