#include "App/ApplicationController.h"
#include "Chopping/AssignmentPlan.h"

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>

namespace padflow {
namespace {
ExternalAssetReference sourceAsset() {
    return {"assign-asset", {},   "Synthetic.wav", "wav", "assign-fingerprint", 0U, 0, 1U,
            48000.0,        400U, 1600U,           false};
}

juce::Result configureSource(ApplicationController& controller) {
    JobSpec target{controller.project().uuid(), controller.project().pad(0U).uuid,
                   controller.project().revision(), 0, JobKind::sampleImport};
    return controller.commitImportedLayer(target, 0U, 0U, sourceAsset());
}

SliceSet slices(const std::size_t count) {
    SliceGenerationRequest request{"assign-set",
                                   "assign-asset",
                                   "assign-fingerprint",
                                   "source-layer",
                                   0,
                                   400,
                                   static_cast<std::int64_t>(count),
                                   SliceRemainderPolicy::include,
                                   SliceDisplayUnit::frames};
    SliceSet result;
    juce::ignoreUnused(generateEqualSlices(request, result));
    return result;
}

AssignmentRequest request(const ApplicationController& controller, const std::size_t count,
                          const std::size_t startingPad = 1U) {
    auto set = slices(count);
    set.sourceLayerUuid = controller.project().pad(0U).layers[0].uuid;
    return {"assign-session",
            controller.project().uuid(),
            controller.project().revision(),
            controller.project().pad(0U).uuid,
            controller.project().pad(0U).layers[0].uuid,
            "assign-asset",
            "assign-fingerprint",
            std::move(set),
            AssignmentDestinationMode::consecutivePads,
            startingPad,
            0U,
            false,
            false};
}
} // namespace

class Milestone3AssignmentTests final : public juce::UnitTest {
  public:
    Milestone3AssignmentTests() : juce::UnitTest("Milestone 3 assignment transaction", "PadFlow") {}

    void runTest() override {
        beginTest("ASSIGN-M3-001 through ASSIGN-M3-004 ordered pad capacity");
        ApplicationController controller;
        controller.createEmptyProject("Assignment", "assign-project");
        expect(configureSource(controller).wasOk());
        AssignmentPlan plan;
        expect(buildAssignmentPlan(controller.project().state(), request(controller, 4U), plan)
                   .wasOk());
        expectEquals(plan.destinations.size(), std::size_t{4U});
        for (std::size_t index = 0U; index < plan.destinations.size(); ++index)
            expectEquals(plan.destinations[index].globalPadIndex, index + 1U);
        expect(buildAssignmentPlan(controller.project().state(), request(controller, 2U, 63U), plan)
                   .failed());

        beginTest("ASSIGN-M3-005 through ASSIGN-M3-012 layer and conflict plans");
        auto layerRequest = request(controller, 4U);
        layerRequest.mode = AssignmentDestinationMode::consecutiveLayers;
        layerRequest.startingGlobalPad = 1U;
        layerRequest.startingLayer = 1U;
        expect(buildAssignmentPlan(controller.project().state(), layerRequest, plan).failed());
        layerRequest.continueLayersAcrossPads = true;
        expect(buildAssignmentPlan(controller.project().state(), layerRequest, plan).wasOk());
        expectEquals(plan.destinations[0].globalPadIndex, std::size_t{1U});
        expectEquals(plan.destinations[0].layerIndex, std::size_t{1U});
        expectEquals(plan.destinations[3].globalPadIndex, std::size_t{2U});
        expectEquals(plan.destinations[3].layerIndex, std::size_t{0U});

        SampleLayer occupied = controller.project().pad(2U).layers[0];
        occupied.assetUuid = "assign-asset";
        occupied.enabled = true;
        occupied.playback = {0U, 400U, 0U, 400U, false, false, false, true};
        expect(controller.setLayer(2U, 0U, occupied).wasOk());
        auto conflictRequest = request(controller, 3U);
        expect(buildAssignmentPlan(controller.project().state(), conflictRequest, plan).wasOk());
        expect(plan.destinations[1].occupied);
        expect(plan.hasUnresolvedConflicts());
        AssignmentPlan decided;
        expect(setAssignmentDecision(plan, 1U, AssignmentConflictDecision::skip, decided).wasOk());
        expect(!decided.hasUnresolvedConflicts());
        expectEquals(decided.destinations[2].globalPadIndex, std::size_t{3U});

        beginTest("ASSIGN-M3-020 through ASSIGN-M3-030 atomic commit and undo");
        const auto before = controller.project().state();
        AssignmentCommitReport report;
        expect(controller.commitSliceAssignment(decided, report).wasOk());
        expectEquals(report.assignedSliceUuids.size(), std::size_t{2U});
        expectEquals(report.skippedSliceUuids.size(), std::size_t{1U});
        expect(controller.project().pad(1U).layers[0].sliceUuid ==
               decided.destinations[0].sliceUuid);
        expect(controller.project().pad(2U).layers[0].assetUuid == occupied.assetUuid);
        expect(controller.project().pad(3U).layers[0].sliceUuid ==
               decided.destinations[2].sliceUuid);
        expect(controller.project().pad(1U).layers[0].assetUuid ==
               controller.project().pad(3U).layers[0].assetUuid);
        const auto committed = controller.project().state();
        expect(controller.undo());
        expect(controller.project().state() == before);
        expect(controller.redo());
        expect(controller.project().state() == committed);

        auto stale = request(controller, 2U, 4U);
        AssignmentPlan stalePlan;
        expect(buildAssignmentPlan(controller.project().state(), stale, stalePlan).wasOk());
        expect(controller.renamePad(10U, "Changed").wasOk());
        const auto beforeStale = controller.project().state();
        expect(controller.commitSliceAssignment(stalePlan, report).failed());
        expect(controller.project().state() == beforeStale);
    }
};

static Milestone3AssignmentTests milestone3AssignmentTests;
} // namespace padflow
