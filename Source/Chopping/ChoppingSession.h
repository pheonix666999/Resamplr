#pragma once

#include "SliceModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace padflow {
enum class ChoppingSessionState : std::uint8_t {
    idle,
    preparing,
    analysing,
    ready,
    previewing,
    awaitingOverwriteDecision,
    committing,
    completed,
    cancelled,
    failed
};

struct ChoppingSessionTarget final {
    juce::String sessionUuid;
    juce::String projectUuid;
    juce::String targetPadUuid;
    juce::String targetLayerUuid;
    std::uint64_t targetRevision{0U};
    juce::String sourceAssetUuid;
    juce::String sourceFingerprint;
    std::int64_t trimStart{0};
    std::int64_t trimEnd{0};

    [[nodiscard]] friend bool operator==(const ChoppingSessionTarget&,
                                         const ChoppingSessionTarget&) = default;
};

class ChoppingSession final {
  public:
    [[nodiscard]] juce::Result begin(ChoppingSessionTarget target);
    void cancel();

    [[nodiscard]] juce::Result regenerateEqual(std::int64_t sliceCount);
    [[nodiscard]] juce::Result regenerateFixed(std::int64_t lengthFrames,
                                               SliceRemainderPolicy remainderPolicy,
                                               SliceDisplayUnit displayUnit);
    [[nodiscard]] juce::Result addMarker(std::int64_t sourceFrame);
    [[nodiscard]] juce::Result deleteMarker(std::int64_t sourceFrame);
    [[nodiscard]] juce::Result moveMarker(std::int64_t sourceFrame, std::int64_t requestedFrame);
    [[nodiscard]] juce::Result clearInternalMarkers();
    [[nodiscard]] bool undoSessionEdit();
    [[nodiscard]] bool redoSessionEdit();

    [[nodiscard]] bool isCurrentTarget(const juce::String& projectUuid,
                                       const juce::String& sourceAssetUuid,
                                       const juce::String& targetLayerUuid,
                                       std::uint64_t revision) const noexcept;
    [[nodiscard]] ChoppingSessionState state() const noexcept;
    [[nodiscard]] const ChoppingSessionTarget& target() const noexcept;
    [[nodiscard]] const std::optional<SliceSet>& provisionalSliceSet() const noexcept;
    [[nodiscard]] std::size_t selectedSlice() const noexcept;
    [[nodiscard]] const juce::String& lastError() const noexcept;
    [[nodiscard]] bool canUndoSessionEdit() const noexcept;
    [[nodiscard]] bool canRedoSessionEdit() const noexcept;

  private:
    [[nodiscard]] SliceGenerationRequest requestFor(std::int64_t amount) const;
    [[nodiscard]] std::vector<std::int64_t> boundaries() const;
    [[nodiscard]] juce::Result applyProvisional(SliceSet candidate);
    [[nodiscard]] juce::Result applyManualBoundaries(std::vector<std::int64_t> candidate);
    [[nodiscard]] juce::Result fail(juce::String message);

    ChoppingSessionTarget target_;
    ChoppingSessionState state_{ChoppingSessionState::idle};
    std::optional<SliceSet> provisional_;
    std::size_t selectedSlice_{0U};
    juce::String lastError_;
    std::vector<SliceSet> undoHistory_;
    std::vector<SliceSet> redoHistory_;
};
} // namespace padflow
