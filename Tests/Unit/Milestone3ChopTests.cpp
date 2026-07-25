#include "Chopping/ChoppingSession.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <limits>

namespace padflow {
namespace {
SliceGenerationRequest request(const std::int64_t start, const std::int64_t end,
                               const std::int64_t amount) {
    return {"m3-set",
            "m3-asset",
            "m3-fingerprint",
            "m3-layer",
            start,
            end,
            amount,
            SliceRemainderPolicy::include,
            SliceDisplayUnit::frames};
}

ChoppingSessionTarget target(const std::int64_t start = 10, const std::int64_t end = 110) {
    return {"m3-session", "m3-project",     "m3-pad", "m3-layer", 17U,
            "m3-asset",   "m3-fingerprint", start,    end};
}
} // namespace

class Milestone3ChopTests final : public juce::UnitTest {
  public:
    Milestone3ChopTests() : juce::UnitTest("Milestone 3 slice model and session") {}

    void runTest() override {
        beginTest("CHOP-M3-001 through CHOP-M3-006 validate stable slice regions");
        SliceSet valid;
        expect(generateEqualSlices(request(10, 20, 2), valid).wasOk());
        expect(validateSliceSet(valid).wasOk());
        auto invalid = valid;
        invalid.slices.front().endFrame = invalid.slices.front().startFrame;
        expect(validateSliceSet(invalid).failed());
        invalid = valid;
        invalid.slices.front().startFrame = 9;
        expect(validateSliceSet(invalid).failed());
        invalid = valid;
        invalid.slices[1].startFrame = invalid.slices[0].endFrame - 1;
        expect(validateSliceSet(invalid).failed());
        expect(valid.slices[0].uuid.isNotEmpty());
        expect(valid.slices[0].uuid != valid.slices[1].uuid);
        SliceSet oneFrame;
        expect(generateEqualSlices(request(42, 43, 1), oneFrame).wasOk());
        expectEquals(oneFrame.slices.front().startFrame, std::int64_t{42});
        expectEquals(oneFrame.slices.front().endFrame, std::int64_t{43});

        beginTest("CHOP-M3-010 through CHOP-M3-020 exact equal division");
        SliceSet equal;
        expect(generateEqualSlices(request(100, 110, 1), equal).wasOk());
        expectEquals(equal.slices.size(), std::size_t{1U});
        expect(generateEqualSlices(request(100, 110, 4), equal).wasOk());
        expectEquals(equal.slices.size(), std::size_t{4U});
        expectEquals(equal.slices[0].startFrame, std::int64_t{100});
        expectEquals(equal.slices[0].endFrame, std::int64_t{102});
        expectEquals(equal.slices[1].endFrame, std::int64_t{105});
        expectEquals(equal.slices[2].endFrame, std::int64_t{107});
        expectEquals(equal.slices[3].endFrame, std::int64_t{110});
        for (std::size_t index = 1U; index < equal.slices.size(); ++index)
            expectEquals(equal.slices[index - 1U].endFrame, equal.slices[index].startFrame);
        expect(generateEqualSlices(request(0, 8, 8), equal).wasOk());
        for (const auto& slice : equal.slices)
            expectEquals(slice.endFrame - slice.startFrame, std::int64_t{1});
        const auto unchanged = equal;
        expect(generateEqualSlices(request(0, 8, 9), equal).failed());
        expect(equal == unchanged);
        const auto maximum = std::numeric_limits<std::int64_t>::max();
        expect(generateEqualSlices(request(maximum - 100, maximum, 7), equal).wasOk());
        expectEquals(equal.slices.front().startFrame, maximum - 100);
        expectEquals(equal.slices.back().endFrame, maximum);
        expect(generateEqualSlices(request(99, 100, 1), equal).wasOk());

        beginTest("CHOP-M3-030 through CHOP-M3-036 fixed length and remainder policy");
        SliceSet fixed;
        auto fixedRequest = request(0, 12, 4);
        expect(generateFixedLengthSlices(fixedRequest, fixed).wasOk());
        expectEquals(fixed.slices.size(), std::size_t{3U});
        fixedRequest = request(0, 10, 4);
        expect(generateFixedLengthSlices(fixedRequest, fixed).wasOk());
        expectEquals(fixed.slices.size(), std::size_t{3U});
        expectEquals(fixed.slices.back().startFrame, std::int64_t{8});
        expectEquals(fixed.slices.back().endFrame, std::int64_t{10});
        fixedRequest.remainderPolicy = SliceRemainderPolicy::discard;
        expect(generateFixedLengthSlices(fixedRequest, fixed).wasOk());
        expectEquals(fixed.slices.size(), std::size_t{2U});
        expectEquals(fixed.slices.back().endFrame, std::int64_t{8});
        auto invalidFixed = request(0, 10, 0);
        expect(generateFixedLengthSlices(invalidFixed, fixed).failed());
        invalidFixed.amount = -4;
        expect(generateFixedLengthSlices(invalidFixed, fixed).failed());
        invalidFixed.amount = 11;
        expect(generateFixedLengthSlices(invalidFixed, fixed).failed());

        beginTest("CHOP-M3-040 through CHOP-M3-048 manual markers and session undo");
        ChoppingSession session;
        expect(session.begin(target()).wasOk());
        expectEquals(static_cast<int>(session.state()),
                     static_cast<int>(ChoppingSessionState::ready));
        expect(session.addMarker(40).wasOk());
        expect(session.provisionalSliceSet().has_value());
        expectEquals(session.provisionalSliceSet()->slices.size(), std::size_t{2U});
        expect(session.addMarker(40).failed());
        expect(session.addMarker(10).failed());
        expect(session.addMarker(110).failed());
        expect(session.deleteMarker(40).wasOk());
        expectEquals(session.provisionalSliceSet()->slices.size(), std::size_t{1U});
        expect(session.addMarker(35).wasOk());
        expect(session.addMarker(80).wasOk());
        expect(session.moveMarker(35, 100).wasOk());
        expectEquals(session.provisionalSliceSet()->slices[0].endFrame, std::int64_t{79});
        expectEquals(session.provisionalSliceSet()->slices.front().startFrame, std::int64_t{10});
        expectEquals(session.provisionalSliceSet()->slices.back().endFrame, std::int64_t{110});
        const auto beforeMoveUndo = *session.provisionalSliceSet();
        expect(session.undoSessionEdit());
        expect(session.provisionalSliceSet()->slices[0].endFrame == 35);
        expect(session.redoSessionEdit());
        expect(*session.provisionalSliceSet() == beforeMoveUndo);
        expect(session.deleteMarker(10).failed());
        expect(session.clearInternalMarkers().wasOk());
        expectEquals(session.provisionalSliceSet()->slices.size(), std::size_t{1U});
        expect(session.isCurrentTarget("m3-project", "m3-asset", "m3-layer", 17U));
        expect(!session.isCurrentTarget("m3-project", "changed", "m3-layer", 17U));
        session.cancel();
        expectEquals(static_cast<int>(session.state()),
                     static_cast<int>(ChoppingSessionState::cancelled));
        expect(!session.provisionalSliceSet().has_value());
        expect(!session.canUndoSessionEdit());
    }
};

static Milestone3ChopTests milestone3ChopTests;
} // namespace padflow
