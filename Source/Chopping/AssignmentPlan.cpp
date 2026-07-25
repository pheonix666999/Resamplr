#include "AssignmentPlan.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace padflow {
namespace {
const ExternalAssetReference* findAsset(const ProjectState& project, const juce::String& uuid) {
    const auto found = std::find_if(project.assets.begin(), project.assets.end(),
                                    [&](const auto& asset) { return asset.uuid == uuid; });
    return found == project.assets.end() ? nullptr : std::addressof(*found);
}

const Pad* findPad(const ProjectState& project, const juce::String& uuid) {
    for (const auto& bank : project.banks)
        for (const auto& pad : bank.pads)
            if (pad.uuid == uuid)
                return std::addressof(pad);
    return nullptr;
}

bool padOccupied(const Pad& pad) {
    return std::any_of(pad.layers.begin(), pad.layers.end(),
                       [](const auto& layer) { return layer.enabled; });
}

juce::String contentSummary(const ProjectState& project, const Pad& pad,
                            const std::size_t layerIndex, const bool wholePad) {
    const auto& layer = pad.layers[layerIndex];
    if (wholePad && padOccupied(pad))
        return pad.name + " (occupied pad)";
    if (!layer.enabled)
        return "Empty";
    const auto* asset = findAsset(project, layer.assetUuid);
    return pad.name + " / Layer " + juce::String{static_cast<int>(layerIndex + 1U)} + " / " +
           (asset != nullptr ? asset->originalName : juce::String{"Missing asset"});
}
} // namespace

bool AssignmentPlan::hasUnresolvedConflicts() const noexcept {
    return std::any_of(destinations.begin(), destinations.end(), [](const auto& destination) {
        return destination.occupied &&
               destination.decision == AssignmentConflictDecision::unresolved;
    });
}

juce::Result buildAssignmentPlan(const ProjectState& project, const AssignmentRequest& request,
                                 AssignmentPlan& output) {
    if (request.sessionUuid.isEmpty() || request.projectUuid != project.projectUuid ||
        request.sourcePadUuid.isEmpty() || request.sourceLayerUuid.isEmpty() ||
        request.sourceAssetUuid.isEmpty() || request.sourceFingerprint.isEmpty())
        return juce::Result::fail("Assignment request identity is incomplete");
    if (request.startingGlobalPad >= totalPadCount || request.startingLayer >= minimumLayersPerPad)
        return juce::Result::fail("Assignment destination is outside the project");
    if (request.wrapAfterBankD)
        return juce::Result::fail("Bank D wrap requires a future explicit workflow");
    if (const auto validation = validateSliceSet(
            request.sliceSet,
            request.sliceSet.algorithm != SliceAlgorithm::fixedLength ||
                request.sliceSet.parameters.remainderPolicy != SliceRemainderPolicy::discard);
        validation.failed())
        return validation;
    if (request.sliceSet.sourceAssetUuid != request.sourceAssetUuid ||
        request.sliceSet.sourceFingerprint != request.sourceFingerprint ||
        request.sliceSet.sourceLayerUuid != request.sourceLayerUuid)
        return juce::Result::fail("Assignment source does not match the slice set");

    const auto* sourcePad = findPad(project, request.sourcePadUuid);
    const auto* sourceAsset = findAsset(project, request.sourceAssetUuid);
    if (sourcePad == nullptr || sourceAsset == nullptr ||
        sourceAsset->contentFingerprint != request.sourceFingerprint)
        return juce::Result::fail("Assignment immutable source is stale");
    const auto sourceLayer =
        std::find_if(sourcePad->layers.begin(), sourcePad->layers.end(), [&](const auto& layer) {
            return layer.uuid == request.sourceLayerUuid &&
                   layer.assetUuid == request.sourceAssetUuid && layer.enabled;
        });
    if (sourceLayer == sourcePad->layers.end())
        return juce::Result::fail("Assignment source layer is stale");

    AssignmentPlan candidate;
    candidate.request = request;
    candidate.requiredDestinations = request.sliceSet.slices.size();
    candidate.destinationBankUuid = project.banks[request.startingGlobalPad / padsPerBank].uuid;
    if (request.mode == AssignmentDestinationMode::consecutivePads)
        candidate.availableDestinations = totalPadCount - request.startingGlobalPad;
    else if (request.continueLayersAcrossPads)
        candidate.availableDestinations =
            (totalPadCount - request.startingGlobalPad) * minimumLayersPerPad -
            request.startingLayer;
    else
        candidate.availableDestinations = minimumLayersPerPad - request.startingLayer;
    candidate.overflow = candidate.requiredDestinations > candidate.availableDestinations;
    if (candidate.overflow)
        return juce::Result::fail("Assignment has insufficient destination capacity");

    candidate.destinations.reserve(request.sliceSet.slices.size());
    for (std::size_t index = 0U; index < request.sliceSet.slices.size(); ++index) {
        std::size_t globalPad = request.startingGlobalPad;
        std::size_t layer = request.startingLayer;
        auto wholePad = false;
        if (request.mode == AssignmentDestinationMode::consecutivePads) {
            globalPad += index;
            layer = 0U;
            wholePad = true;
        } else if (request.continueLayersAcrossPads) {
            const auto linearLayer = request.startingLayer + index;
            globalPad += linearLayer / minimumLayersPerPad;
            layer = linearLayer % minimumLayersPerPad;
        } else {
            layer += index;
        }
        const auto& pad = project.banks[globalPad / padsPerBank].pads[globalPad % padsPerBank];
        if (pad.uuid == request.sourcePadUuid)
            return juce::Result::fail("Assignment cannot replace its active source pad");
        const auto occupied = wholePad ? padOccupied(pad) : pad.layers[layer].enabled;
        const auto& slice = request.sliceSet.slices[index];
        candidate.destinations.push_back({index, slice.uuid, slice.startFrame, slice.endFrame,
                                          globalPad, layer, pad.uuid, pad.layers[layer].uuid,
                                          wholePad, occupied,
                                          contentSummary(project, pad, layer, wholePad),
                                          occupied ? AssignmentConflictDecision::unresolved
                                                   : AssignmentConflictDecision::replace});
    }
    output = std::move(candidate);
    return juce::Result::ok();
}

juce::Result setAssignmentDecision(const AssignmentPlan& plan, const std::size_t destinationIndex,
                                   const AssignmentConflictDecision decision,
                                   AssignmentPlan& output) {
    if (destinationIndex >= plan.destinations.size())
        return juce::Result::fail("Assignment conflict index is invalid");
    if (decision == AssignmentConflictDecision::unresolved)
        return juce::Result::fail("Assignment conflict must be replaced or skipped");
    auto candidate = plan;
    auto& destination = candidate.destinations[destinationIndex];
    if (!destination.occupied && decision == AssignmentConflictDecision::skip)
        return juce::Result::fail("Empty destinations do not require skip decisions");
    destination.decision = decision;
    output = std::move(candidate);
    return juce::Result::ok();
}
} // namespace padflow
