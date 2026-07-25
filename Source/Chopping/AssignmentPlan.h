#pragma once

#include "Chopping/SliceModel.h"
#include "Model/PadModel.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace padflow {
enum class AssignmentDestinationMode : std::uint8_t { consecutivePads, consecutiveLayers };
enum class AssignmentConflictDecision : std::uint8_t { unresolved, replace, skip };

struct AssignmentRequest final {
    juce::String sessionUuid;
    juce::String projectUuid;
    std::uint64_t expectedProjectRevision{0U};
    juce::String sourcePadUuid;
    juce::String sourceLayerUuid;
    juce::String sourceAssetUuid;
    juce::String sourceFingerprint;
    SliceSet sliceSet;
    AssignmentDestinationMode mode{AssignmentDestinationMode::consecutivePads};
    std::size_t startingGlobalPad{0U};
    std::size_t startingLayer{0U};
    bool continueLayersAcrossPads{false};
    bool wrapAfterBankD{false};

    [[nodiscard]] friend bool operator==(const AssignmentRequest&,
                                         const AssignmentRequest&) = default;
};

struct AssignmentDestination final {
    std::size_t sliceIndex{0U};
    juce::String sliceUuid;
    std::int64_t startFrame{0};
    std::int64_t endFrame{0};
    std::size_t globalPadIndex{0U};
    std::size_t layerIndex{0U};
    juce::String expectedPadUuid;
    juce::String expectedLayerUuid;
    bool replacesWholePad{false};
    bool occupied{false};
    juce::String existingContentSummary;
    AssignmentConflictDecision decision{AssignmentConflictDecision::replace};

    [[nodiscard]] friend bool operator==(const AssignmentDestination&,
                                         const AssignmentDestination&) = default;
};

struct AssignmentPlan final {
    AssignmentRequest request;
    juce::String destinationBankUuid;
    std::vector<AssignmentDestination> destinations;
    std::size_t requiredDestinations{0U};
    std::size_t availableDestinations{0U};
    bool overflow{false};

    [[nodiscard]] bool hasUnresolvedConflicts() const noexcept;
    [[nodiscard]] friend bool operator==(const AssignmentPlan&, const AssignmentPlan&) = default;
};

struct AssignmentCommitReport final {
    std::vector<juce::String> assignedSliceUuids;
    std::vector<juce::String> skippedSliceUuids;
};

[[nodiscard]] juce::Result buildAssignmentPlan(const ProjectState& project,
                                               const AssignmentRequest& request,
                                               AssignmentPlan& output);
[[nodiscard]] juce::Result setAssignmentDecision(const AssignmentPlan& plan,
                                                 std::size_t destinationIndex,
                                                 AssignmentConflictDecision decision,
                                                 AssignmentPlan& output);
} // namespace padflow
