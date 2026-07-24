#pragma once

#include "App/ApplicationController.h"
#include "Audio/PreviewPlayer.h"
#include "Sampling/SampleImporter.h"

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace padflow {
class SamplePreviewController final {
  public:
    SamplePreviewController(ApplicationController& controller, PreviewPlayer& player) noexcept;

    [[nodiscard]] std::optional<JobHandle>
    begin(BackgroundJobSystem& jobs, const juce::File& sourceFile,
          std::uint64_t maximumDecodedBytes = defaultMilestone1DecodedBudgetBytes);
    [[nodiscard]] juce::Result commit(const JobResult& result);
    [[nodiscard]] juce::Result setVolume(float volume);
    [[nodiscard]] bool stop() noexcept;
    [[nodiscard]] std::size_t collectRetired();

    [[nodiscard]] juce::String lastError() const;
    [[nodiscard]] std::uint64_t pendingRevision() const noexcept;

  private:
    void retireActive(std::uint64_t epoch);

    ApplicationController& controller_;
    PreviewPlayer& player_;
    DeferredSampleAssetReclaimer reclaimer_;
    std::shared_ptr<const SampleAsset> activeAsset_;
    std::optional<JobHandle> pending_;
    juce::String lastError_;
    std::uint64_t revision_{0U};
};
} // namespace padflow
