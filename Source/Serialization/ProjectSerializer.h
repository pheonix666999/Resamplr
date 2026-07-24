#pragma once

#include "Model/Project.h"

#include <juce_core/juce_core.h>

namespace padflow {
struct SerializationResult final {
    bool succeeded{false};
    juce::String message;
};

class ProjectSerializer final {
  public:
    [[nodiscard]] static juce::String canonicalManifest(const Project& project);
    [[nodiscard]] static juce::Result restoreCanonicalManifest(const juce::String& manifest,
                                                               Project& project);
    [[nodiscard]] static SerializationResult save(const Project& project,
                                                  const juce::File& destination);
    [[nodiscard]] static juce::Result load(const juce::File& source, Project& project);
};
} // namespace padflow
