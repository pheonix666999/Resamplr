#include "ChoppingWorkspace.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace padflow {
namespace {
constexpr auto background = 0xff15191fU;
constexpr auto panel = 0xff222a33U;
constexpr auto text = 0xffd8e1e8U;
constexpr auto teal = 0xff50c8bbU;
constexpr auto magenta = 0xffdb65b5U;

void identify(juce::Component& component, const juce::String& id, const juce::String& title) {
    component.setComponentID(id);
    component.setTitle(title);
    component.setName(title);
}
} // namespace

ChoppingWorkspace::ChoppingWorkspace(ApplicationController& controller, BackgroundJobSystem& jobs,
                                     SampleAssetRegistry& assets, PreviewPlayer& previewPlayer)
    : controller_(controller), jobs_(jobs), assets_(assets), audition_(previewPlayer) {
    setComponentID("chopping-workspace");
    setTitle("PadFlow chopping workspace");
    setDescription("Non-destructive equal, fixed, transient, manual, and lazy sample chopping");
    configureControls();
    refresh();
}

ChoppingWorkspace::~ChoppingWorkspace() {
    cancel();
}

void ChoppingWorkspace::configureControls() {
    title_.setText("SAMPLE CHOPPING", juce::dontSendNotification);
    title_.setFont(juce::FontOptions{22.0F, juce::Font::bold});
    title_.setColour(juce::Label::textColourId, juce::Colour{teal});
    addAndMakeVisible(title_);

    modeBox_.addItemList({"Equal", "Fixed Length", "Transient", "Manual", "Lazy"}, 1);
    fixedUnit_.addItemList({"Frames", "Milliseconds"}, 1);
    remainderPolicy_.addItemList({"Include remainder", "Discard remainder"}, 1);
    destinationBank_.addItemList({"Bank A", "Bank B", "Bank C", "Bank D"}, 1);
    destinationMode_.addItemList({"Consecutive pads", "Consecutive layers"}, 1);
    overwritePolicy_.addItemList({"Replace occupied", "Skip occupied"}, 1);
    modeBox_.setSelectedId(1, juce::dontSendNotification);
    fixedUnit_.setSelectedId(1, juce::dontSendNotification);
    remainderPolicy_.setSelectedId(1, juce::dontSendNotification);
    destinationBank_.setSelectedId(1, juce::dontSendNotification);
    destinationMode_.setSelectedId(1, juce::dontSendNotification);
    overwritePolicy_.setSelectedId(1, juce::dontSendNotification);

    const auto configureSlider = [this](juce::Slider& slider, const juce::String& id,
                                        const juce::String& name, const double minimum,
                                        const double maximum, const double interval,
                                        const double initial) {
        identify(slider, id, name);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
        slider.setRange(minimum, maximum, interval);
        slider.setValue(initial, juce::dontSendNotification);
        addAndMakeVisible(slider);
    };
    configureSlider(sliceCount_, "chop-slice-count", "Equal slice count", 1.0, 4096.0, 1.0, 4.0);
    configureSlider(fixedLength_, "chop-fixed-length", "Fixed length", 1.0, 1000000.0, 1.0, 240.0);
    configureSlider(sensitivity_, "chop-transient-sensitivity", "Transient sensitivity", 0.0, 1.0,
                    0.01, 0.5);
    configureSlider(minimumDuration_, "chop-minimum-duration", "Minimum slice frames", 1.0,
                    1000000.0, 1.0, 64.0);
    configureSlider(attackLookBack_, "chop-attack-lookback", "Attack look-back frames", 0.0,
                    1000000.0, 1.0, 0.0);
    configureSlider(destinationPad_, "chop-destination-pad", "Starting pad", 1.0, 16.0, 1.0, 2.0);
    configureSlider(destinationLayer_, "chop-destination-layer", "Starting layer", 1.0, 4.0, 1.0,
                    1.0);
    configureSlider(markerGridFrames_, "chop-marker-grid", "Marker grid frames", 0.0, 10000.0, 1.0,
                    0.0);
    configureSlider(conflictIndex_, "chop-conflict-index", "Conflict destination", 1.0, 4096.0, 1.0,
                    1.0);

    for (auto* combo : {&modeBox_, &fixedUnit_, &remainderPolicy_, &destinationBank_,
                        &destinationMode_, &overwritePolicy_})
        addAndMakeVisible(*combo);
    identify(modeBox_, "chop-mode", "Chopping mode");
    identify(fixedUnit_, "chop-fixed-unit", "Fixed length unit");
    identify(remainderPolicy_, "chop-remainder-policy", "Remainder policy");
    identify(destinationBank_, "chop-destination-bank", "Destination bank");
    identify(destinationMode_, "chop-destination-mode", "Destination mode");
    identify(overwritePolicy_, "chop-overwrite-policy", "Overwrite policy");

    for (auto* button :
         {&analyseButton_, &addMarkerButton_, &deleteMarkerButton_, &clearMarkersButton_,
          &undoSessionButton_, &redoSessionButton_, &previousSliceButton_, &nextSliceButton_,
          &auditionSelectedButton_, &auditionAllButton_, &stopAuditionButton_, &lazyStartButton_,
          &placeLazyMarkerButton_, &lazyStopButton_, &previewAssignmentButton_,
          &commitAssignmentButton_, &replaceConflictButton_, &skipConflictButton_,
          &cancelButton_}) {
        addAndMakeVisible(*button);
        button->setColour(juce::TextButton::buttonColourId, juce::Colour{panel}.brighter(0.08F));
    }
    identify(analyseButton_, "chop-analyse", "Analyse transients");
    identify(addMarkerButton_, "chop-add-marker", "Add manual marker");
    identify(deleteMarkerButton_, "chop-delete-marker", "Delete selected marker");
    identify(clearMarkersButton_, "chop-clear-markers", "Clear internal markers");
    identify(previousSliceButton_, "chop-previous-slice", "Previous slice");
    identify(nextSliceButton_, "chop-next-slice", "Next slice");
    identify(auditionSelectedButton_, "chop-audition-selected", "Audition selected slice");
    identify(auditionAllButton_, "chop-audition-all", "Audition all slices");
    identify(stopAuditionButton_, "chop-audition-stop", "Stop audition");
    identify(lazyStartButton_, "chop-lazy-start", "Start lazy chop");
    identify(placeLazyMarkerButton_, "chop-lazy-marker", "Place lazy marker");
    identify(lazyStopButton_, "chop-lazy-stop", "Stop lazy chop");
    identify(previewAssignmentButton_, "chop-preview-assignment", "Preview assignment");
    identify(commitAssignmentButton_, "chop-commit-assignment", "Commit assignment");
    identify(replaceConflictButton_, "chop-replace-conflict", "Replace selected conflict");
    identify(skipConflictButton_, "chop-skip-conflict", "Skip selected conflict");
    identify(cancelButton_, "chop-cancel", "Cancel chopping");

    for (auto* toggle : {&continueAcrossPads_, &stayInEditor_, &markerZeroSnap_})
        addAndMakeVisible(*toggle);
    identify(continueAcrossPads_, "chop-continue-layers", "Continue layers across pads");
    identify(stayInEditor_, "chop-stay-editor", "Stay in editor after assignment");
    identify(markerZeroSnap_, "chop-marker-zero-snap", "Snap manual markers to zero crossings");
    stayInEditor_.setToggleState(true, juce::dontSendNotification);

    for (auto* label : {&sliceReadout_, &assignmentReadout_, &statusLabel_}) {
        label->setColour(juce::Label::textColourId, juce::Colour{text});
        label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(*label);
    }
    identify(sliceReadout_, "chop-slice-readout", "Selected slice readout");
    identify(assignmentReadout_, "chop-assignment-readout", "Assignment preview readout");
    identify(statusLabel_, "chop-status", "Chopping status");

    modeBox_.onChange = [this] {
        setMode(static_cast<SliceAlgorithm>(modeBox_.getSelectedId() - 1));
    };
    sliceCount_.onValueChange = [this] {
        if (mode_ == SliceAlgorithm::equal)
            showResult(generateEqual(static_cast<std::int64_t>(sliceCount_.getValue())),
                       "Equal slices regenerated");
    };
    analyseButton_.onClick = [this] {
        TransientAnalysisParameters parameters;
        parameters.sensitivity = static_cast<float>(sensitivity_.getValue());
        parameters.minimumSliceFrames = static_cast<std::int64_t>(minimumDuration_.getValue());
        parameters.attackLookBackFrames = static_cast<std::int64_t>(attackLookBack_.getValue());
        juce::ignoreUnused(submitTransientAnalysis(parameters));
    };
    addMarkerButton_.onClick = [this] {
        const auto* set =
            session_.provisionalSliceSet().has_value() ? &*session_.provisionalSliceSet() : nullptr;
        if (set != nullptr && !set->slices.empty()) {
            const auto& slice = set->slices[session_.selectedSlice()];
            showResult(addMarker(slice.startFrame + (slice.endFrame - slice.startFrame) / 2),
                       "Marker added");
        }
    };
    deleteMarkerButton_.onClick = [this] {
        showResult(deleteMarker(selectedBoundary()), "Marker deleted");
    };
    clearMarkersButton_.onClick = [this] {
        showResult(session_.clearInternalMarkers(), "Internal markers cleared");
        publishSlices();
        refresh();
    };
    undoSessionButton_.onClick = [this] { juce::ignoreUnused(undoSessionEdit()); };
    redoSessionButton_.onClick = [this] { juce::ignoreUnused(redoSessionEdit()); };
    previousSliceButton_.onClick = [this] { juce::ignoreUnused(selectPreviousSlice()); };
    nextSliceButton_.onClick = [this] { juce::ignoreUnused(selectNextSlice()); };
    auditionSelectedButton_.onClick = [this] { juce::ignoreUnused(auditionSelected()); };
    auditionAllButton_.onClick = [this] { juce::ignoreUnused(auditionAll()); };
    stopAuditionButton_.onClick = [this] { stopAudition(); };
    lazyStartButton_.onClick = [this] {
        if (!session_.provisionalSliceSet().has_value())
            return;
        const auto& set = *session_.provisionalSliceSet();
        juce::ignoreUnused(startLazy({set.sourceTrimStart, set.sourceTrimEnd,
                                      static_cast<std::int64_t>(minimumDuration_.getValue()), 0}));
    };
    placeLazyMarkerButton_.onClick = [this] {
        juce::ignoreUnused(captureLazyMarker(LazyMarkerSource::control));
    };
    lazyStopButton_.onClick = [this] { stopLazy(); };
    previewAssignmentButton_.onClick = [this] {
        const auto bank =
            static_cast<std::size_t>(std::max(1, destinationBank_.getSelectedId()) - 1);
        const auto pad = static_cast<std::size_t>(destinationPad_.getValue() - 1.0);
        const auto layer = static_cast<std::size_t>(destinationLayer_.getValue() - 1.0);
        const auto destinationMode = destinationMode_.getSelectedId() == 2
                                         ? AssignmentDestinationMode::consecutiveLayers
                                         : AssignmentDestinationMode::consecutivePads;
        auto result = previewAssignment(destinationMode, bank * padsPerBank + pad, layer,
                                        continueAcrossPads_.getToggleState());
        if (result.wasOk())
            result = resolveAllConflicts(overwritePolicy_.getSelectedId() == 2
                                             ? AssignmentConflictDecision::skip
                                             : AssignmentConflictDecision::replace);
        showResult(result, "Assignment preview ready");
    };
    replaceConflictButton_.onClick = [this] {
        showResult(resolveConflict(static_cast<std::size_t>(conflictIndex_.getValue() - 1.0),
                                   AssignmentConflictDecision::replace),
                   "Selected conflict will be replaced");
    };
    skipConflictButton_.onClick = [this] {
        showResult(resolveConflict(static_cast<std::size_t>(conflictIndex_.getValue() - 1.0),
                                   AssignmentConflictDecision::skip),
                   "Selected conflict will be skipped");
    };
    commitAssignmentButton_.onClick = [this] {
        AssignmentCommitReport report;
        showResult(commitAssignment(report),
                   "Assigned " + juce::String{static_cast<int>(report.assignedSliceUuids.size())} +
                       " slices");
    };
    cancelButton_.onClick = [this] { cancel(); };
}

void ChoppingWorkspace::paint(juce::Graphics& graphics) {
    graphics.fillAll(juce::Colour{background});
    graphics.setColour(juce::Colour{panel});
    graphics.fillRoundedRectangle(getLocalBounds().reduced(6).toFloat(), 10.0F);
    graphics.setColour(juce::Colour{magenta}.withAlpha(0.7F));
    graphics.drawRoundedRectangle(getLocalBounds().reduced(6).toFloat(), 10.0F, 1.5F);
}

void ChoppingWorkspace::resized() {
    auto area = getLocalBounds().reduced(16);
    title_.setBounds(area.removeFromTop(32));
    auto top = area.removeFromTop(30);
    modeBox_.setBounds(top.removeFromLeft(120).reduced(2));
    sliceCount_.setBounds(top.removeFromLeft(140).reduced(2));
    fixedLength_.setBounds(top.removeFromLeft(150).reduced(2));
    fixedUnit_.setBounds(top.removeFromLeft(100).reduced(2));
    remainderPolicy_.setBounds(top.reduced(2));
    auto analysis = area.removeFromTop(30);
    sensitivity_.setBounds(analysis.removeFromLeft(180).reduced(2));
    minimumDuration_.setBounds(analysis.removeFromLeft(190).reduced(2));
    attackLookBack_.setBounds(analysis.removeFromLeft(180).reduced(2));
    analyseButton_.setBounds(analysis.removeFromLeft(90).reduced(2));
    auto markers = area.removeFromTop(30);
    for (auto* button :
         {&addMarkerButton_, &deleteMarkerButton_, &clearMarkersButton_, &undoSessionButton_,
          &redoSessionButton_, &previousSliceButton_, &nextSliceButton_})
        button->setBounds(markers.removeFromLeft(90).reduced(2));
    auto markerOptions = area.removeFromTop(28);
    markerZeroSnap_.setBounds(markerOptions.removeFromLeft(180));
    markerGridFrames_.setBounds(markerOptions.removeFromLeft(220).reduced(2));
    sliceReadout_.setBounds(area.removeFromTop(28));
    auto audition = area.removeFromTop(32);
    for (auto* button : {&auditionSelectedButton_, &auditionAllButton_, &stopAuditionButton_,
                         &lazyStartButton_, &placeLazyMarkerButton_, &lazyStopButton_})
        button->setBounds(audition.removeFromLeft(103).reduced(2));
    area.removeFromTop(8);
    auto destination = area.removeFromTop(32);
    destinationBank_.setBounds(destination.removeFromLeft(95).reduced(2));
    destinationPad_.setBounds(destination.removeFromLeft(130).reduced(2));
    destinationMode_.setBounds(destination.removeFromLeft(135).reduced(2));
    destinationLayer_.setBounds(destination.removeFromLeft(130).reduced(2));
    overwritePolicy_.setBounds(destination.removeFromLeft(130).reduced(2));
    auto options = area.removeFromTop(30);
    continueAcrossPads_.setBounds(options.removeFromLeft(230));
    stayInEditor_.setBounds(options.removeFromLeft(180));
    assignmentReadout_.setBounds(area.removeFromTop(52));
    auto conflict = area.removeFromTop(30);
    conflictIndex_.setBounds(conflict.removeFromLeft(220).reduced(2));
    replaceConflictButton_.setBounds(conflict.removeFromLeft(150).reduced(2));
    skipConflictButton_.setBounds(conflict.removeFromLeft(130).reduced(2));
    auto actions = area.removeFromTop(34);
    previewAssignmentButton_.setBounds(actions.removeFromLeft(170).reduced(2));
    commitAssignmentButton_.setBounds(actions.removeFromLeft(170).reduced(2));
    cancelButton_.setBounds(actions.removeFromLeft(100).reduced(2));
    statusLabel_.setBounds(area.removeFromTop(30));
}

juce::Result ChoppingWorkspace::beginForLayer(const std::size_t globalPadIndex,
                                              const std::size_t layerIndex) {
    if (globalPadIndex >= totalPadCount || layerIndex >= minimumLayersPerPad)
        return juce::Result::fail("Chopping source is outside the project");
    const auto& pad = controller_.project().pad(globalPadIndex);
    const auto& layer = pad.layers[layerIndex];
    const auto reference = std::find_if(
        controller_.project().state().assets.begin(), controller_.project().state().assets.end(),
        [&](const auto& item) { return item.uuid == layer.assetUuid; });
    if (!layer.enabled || reference == controller_.project().state().assets.end() ||
        reference->missing || assets_.find(layer.assetUuid) == nullptr)
        return juce::Result::fail("Load and resolve a sample before chopping");
    const auto playback = resolveSamplePlaybackSettings(layer, reference->frameCount);
    assignmentPlan_.reset();
    auto result = session_.begin({juce::Uuid{}.toString(), controller_.project().uuid(), pad.uuid,
                                  layer.uuid, controller_.project().revision(), layer.assetUuid,
                                  reference->contentFingerprint,
                                  static_cast<std::int64_t>(playback.startFrame),
                                  static_cast<std::int64_t>(playback.endFrame)});
    if (result.wasOk())
        result = generateEqual(static_cast<std::int64_t>(sliceCount_.getValue()));
    showResult(result, "Chopping session ready");
    return result;
}

void ChoppingWorkspace::setMode(const SliceAlgorithm mode) {
    mode_ = mode;
    modeBox_.setSelectedId(static_cast<int>(mode) + 1, juce::dontSendNotification);
    assignmentPlan_.reset();
    if (mode_ == SliceAlgorithm::equal)
        showResult(generateEqual(static_cast<std::int64_t>(sliceCount_.getValue())), "Equal mode");
    else if (mode_ == SliceAlgorithm::fixedLength)
        showResult(generateFixed(fixedLength_.getValue(),
                                 fixedUnit_.getSelectedId() == 2 ? SliceDisplayUnit::milliseconds
                                                                 : SliceDisplayUnit::frames,
                                 remainderPolicy_.getSelectedId() == 2
                                     ? SliceRemainderPolicy::discard
                                     : SliceRemainderPolicy::include),
                   "Fixed-length mode");
    else {
        status_ =
            mode_ == SliceAlgorithm::transient ? "Transient mode: adjust parameters and Analyse"
            : mode_ == SliceAlgorithm::manual  ? "Manual mode: add, drag, nudge, or delete markers"
                                               : "Lazy mode: start playback and place markers";
        refresh();
    }
}

SliceAlgorithm ChoppingWorkspace::mode() const noexcept {
    return mode_;
}

juce::Result ChoppingWorkspace::generateEqual(const std::int64_t count) {
    assignmentPlan_.reset();
    const auto result = session_.regenerateEqual(count);
    if (result.wasOk()) {
        mode_ = SliceAlgorithm::equal;
        publishSlices();
    }
    refresh();
    return result;
}

juce::Result ChoppingWorkspace::generateFixed(const double displayLength,
                                              const SliceDisplayUnit unit,
                                              const SliceRemainderPolicy remainder) {
    const auto* reference = sourceReference();
    if (reference == nullptr || !std::isfinite(displayLength) || displayLength <= 0.0)
        return juce::Result::fail("Fixed length is invalid");
    const auto frames = unit == SliceDisplayUnit::milliseconds
                            ? static_cast<std::int64_t>(std::llround(
                                  displayLength * reference->sourceSampleRate / 1000.0))
                            : static_cast<std::int64_t>(std::llround(displayLength));
    assignmentPlan_.reset();
    const auto result = session_.regenerateFixed(frames, remainder, unit);
    if (result.wasOk()) {
        mode_ = SliceAlgorithm::fixedLength;
        publishSlices();
    }
    refresh();
    return result;
}

bool ChoppingWorkspace::submitTransientAnalysis(TransientAnalysisParameters parameters) {
    const auto asset = sourceAsset();
    if (asset == nullptr || !session_.provisionalSliceSet().has_value())
        return false;
    if (analysisJob_.has_value())
        analysisJob_->cancel();
    const auto& target = session_.target();
    SliceGenerationRequest slices{makeStableUuid(target.sessionUuid + ":slice-set"),
                                  target.sourceAssetUuid,
                                  target.sourceFingerprint,
                                  target.targetLayerUuid,
                                  target.trimStart,
                                  target.trimEnd,
                                  1,
                                  SliceRemainderPolicy::include,
                                  SliceDisplayUnit::frames};
    auto handle =
        TransientAnalysis::submit(jobs_, {{target.projectUuid, target.sourceAssetUuid,
                                           target.targetRevision, 0, JobKind::transientAnalysis},
                                          std::move(slices),
                                          parameters,
                                          asset});
    if (!handle.has_value()) {
        status_ = "Transient analysis queue is full";
        refresh();
        return false;
    }
    analysisJob_ = std::move(handle);
    mode_ = SliceAlgorithm::transient;
    juce::ignoreUnused(session_.markAnalysing());
    status_ = "Analysing transients…";
    refresh();
    return true;
}

bool ChoppingWorkspace::handleCompletedJob(const JobResult& result) {
    if (result.target.kind != JobKind::transientAnalysis)
        return false;
    analysisJob_.reset();
    const auto accepted = session_.acceptTransientResult(result);
    status_ = accepted.wasOk() ? "Transient markers ready" : accepted.getErrorMessage();
    if (accepted.wasOk()) {
        mode_ = SliceAlgorithm::transient;
        assignmentPlan_.reset();
        publishSlices();
    }
    refresh();
    return true;
}

juce::Result ChoppingWorkspace::addMarker(const std::int64_t frame) {
    assignmentPlan_.reset();
    const auto result = session_.addMarker(snapMarker(frame));
    if (result.wasOk()) {
        mode_ = SliceAlgorithm::manual;
        publishSlices();
    }
    refresh();
    return result;
}

juce::Result ChoppingWorkspace::deleteMarker(const std::int64_t frame) {
    assignmentPlan_.reset();
    const auto result = session_.deleteMarker(frame);
    if (result.wasOk()) {
        mode_ = SliceAlgorithm::manual;
        publishSlices();
    }
    refresh();
    return result;
}

juce::Result ChoppingWorkspace::moveMarker(const std::int64_t frame,
                                           const std::int64_t replacement) {
    assignmentPlan_.reset();
    const auto result = session_.moveMarker(frame, snapMarker(replacement));
    if (result.wasOk()) {
        mode_ = SliceAlgorithm::manual;
        publishSlices();
    }
    refresh();
    return result;
}

bool ChoppingWorkspace::undoSessionEdit() {
    const auto changed = session_.undoSessionEdit();
    if (changed) {
        assignmentPlan_.reset();
        publishSlices();
        refresh();
    }
    return changed;
}

bool ChoppingWorkspace::redoSessionEdit() {
    const auto changed = session_.redoSessionEdit();
    if (changed) {
        assignmentPlan_.reset();
        publishSlices();
        refresh();
    }
    return changed;
}

bool ChoppingWorkspace::selectSlice(const std::size_t index) {
    const auto selected = session_.selectSlice(index);
    if (selected) {
        publishSlices();
        refresh();
    }
    return selected;
}

bool ChoppingWorkspace::selectPreviousSlice() {
    const auto selected = session_.selectPreviousSlice();
    if (selected) {
        publishSlices();
        refresh();
    }
    return selected;
}

bool ChoppingWorkspace::selectNextSlice() {
    const auto selected = session_.selectNextSlice();
    if (selected) {
        publishSlices();
        refresh();
    }
    return selected;
}

bool ChoppingWorkspace::auditionSelected() {
    const auto asset = sourceAsset();
    const auto& set = session_.provisionalSliceSet();
    if (asset == nullptr || !set.has_value() || set->slices.empty())
        return false;
    juce::ignoreUnused(session_.markPreviewing());
    const auto started = audition_.startSelected(asset, set->slices[session_.selectedSlice()]);
    status_ = started ? "Auditioning selected slice" : "Slice audition queue is full";
    refresh();
    return started;
}

bool ChoppingWorkspace::auditionAll() {
    const auto asset = sourceAsset();
    const auto& set = session_.provisionalSliceSet();
    if (asset == nullptr || !set.has_value() || set->slices.empty())
        return false;
    juce::ignoreUnused(session_.markPreviewing());
    const auto started = audition_.startSequential(asset, set->slices);
    status_ = started ? "Auditioning all slices in order" : "Slice audition queue is full";
    refresh();
    return started;
}

void ChoppingWorkspace::stopAudition() {
    juce::ignoreUnused(audition_.stop());
    status_ = "Audition stopped";
    refresh();
}

bool ChoppingWorkspace::startLazy(const LazyCaptureSettings settings) {
    const auto asset = sourceAsset();
    if (asset == nullptr || lazyCapture_.start(settings).failed() ||
        !audition_.startLazy(asset, settings.trimStart, settings.trimEnd)) {
        lazyCapture_.stop();
        status_ = "Lazy chop could not start";
        refresh();
        return false;
    }
    lazySettings_ = settings;
    mode_ = SliceAlgorithm::lazy;
    status_ = "Lazy chop active — pads, keys, MIDI, and Place Marker add boundaries";
    refresh();
    return true;
}

bool ChoppingWorkspace::captureLazyMarker(const LazyMarkerSource source) {
    const auto frame = static_cast<std::int64_t>(audition_.sourceFramePosition());
    const auto accepted = lazyCapture_.captureFromAudioThread(frame, source);
    if (!accepted)
        status_ = "Lazy marker queue is full or inactive";
    refresh();
    return accepted;
}

LazyDrainResult ChoppingWorkspace::drainLazyMarkers() {
    const auto result = lazyCapture_.drainToSession(session_);
    if (result.accepted > 0U) {
        if (lazySettings_.has_value())
            juce::ignoreUnused(session_.markCurrentSetLazy(lazySettings_->minimumSliceFrames,
                                                           lazySettings_->quantizeFrames));
        assignmentPlan_.reset();
        publishSlices();
        status_ = "Lazy markers committed to provisional slices";
    }
    refresh();
    return result;
}

void ChoppingWorkspace::stopLazy() {
    lazyCapture_.stop();
    juce::ignoreUnused(audition_.stop());
    juce::ignoreUnused(drainLazyMarkers());
    status_ = "Lazy chop stopped; review provisional markers";
    refresh();
}

juce::Result ChoppingWorkspace::previewAssignment(const AssignmentDestinationMode destinationMode,
                                                  const std::size_t startingGlobalPad,
                                                  const std::size_t startingLayer,
                                                  const bool continueAcrossPads) {
    if (!session_.provisionalSliceSet().has_value())
        return juce::Result::fail("No provisional slices are available");
    const auto& target = session_.target();
    AssignmentRequest request{target.sessionUuid,
                              target.projectUuid,
                              controller_.project().revision(),
                              target.targetPadUuid,
                              target.targetLayerUuid,
                              target.sourceAssetUuid,
                              target.sourceFingerprint,
                              *session_.provisionalSliceSet(),
                              destinationMode,
                              startingGlobalPad,
                              startingLayer,
                              continueAcrossPads,
                              false};
    AssignmentPlan candidate;
    const auto result = buildAssignmentPlan(controller_.project().state(), request, candidate);
    if (result.wasOk())
        assignmentPlan_ = std::move(candidate);
    refresh();
    return result;
}

juce::Result ChoppingWorkspace::resolveAllConflicts(const AssignmentConflictDecision decision) {
    if (!assignmentPlan_.has_value())
        return juce::Result::fail("Preview an assignment first");
    auto candidate = *assignmentPlan_;
    for (std::size_t index = 0U; index < candidate.destinations.size(); ++index) {
        if (!candidate.destinations[index].occupied)
            continue;
        AssignmentPlan resolved;
        const auto result = setAssignmentDecision(candidate, index, decision, resolved);
        if (result.failed())
            return result;
        candidate = std::move(resolved);
    }
    assignmentPlan_ = std::move(candidate);
    refresh();
    return juce::Result::ok();
}

juce::Result ChoppingWorkspace::resolveConflict(const std::size_t destinationIndex,
                                                const AssignmentConflictDecision decision) {
    if (!assignmentPlan_.has_value())
        return juce::Result::fail("Preview an assignment first");
    AssignmentPlan candidate;
    const auto result =
        setAssignmentDecision(*assignmentPlan_, destinationIndex, decision, candidate);
    if (result.wasOk())
        assignmentPlan_ = std::move(candidate);
    refresh();
    return result;
}

juce::Result ChoppingWorkspace::commitAssignment(AssignmentCommitReport& report) {
    if (!assignmentPlan_.has_value())
        return juce::Result::fail("Preview and resolve the assignment first");
    const auto result = controller_.commitSliceAssignment(*assignmentPlan_, report);
    if (result.failed())
        return result;
    assignmentPlan_.reset();
    status_ = "Assignment committed as one undoable project edit";
    if (onProjectCommitted)
        onProjectCommitted();
    if (!stayInEditor_.getToggleState())
        cancel();
    refresh();
    return result;
}

void ChoppingWorkspace::cancel() {
    if (analysisJob_.has_value())
        analysisJob_->cancel();
    analysisJob_.reset();
    lazyCapture_.stop();
    lazySettings_.reset();
    juce::ignoreUnused(audition_.stop());
    assignmentPlan_.reset();
    session_.cancel();
    publishSlices();
    status_ = "Chopping cancelled — project unchanged";
    refresh();
    if (onCancel)
        onCancel();
}

void ChoppingWorkspace::service() {
    audition_.service();
    if (lazyCapture_.active())
        juce::ignoreUnused(drainLazyMarkers());
    if (analysisJob_.has_value()) {
        status_ = "Analysing transients… " +
                  juce::String{static_cast<int>(analysisJob_->progress->snapshot() * 100.0F)} + "%";
        refresh();
    }
}

const ChoppingSession& ChoppingWorkspace::session() const noexcept {
    return session_;
}

const std::optional<AssignmentPlan>& ChoppingWorkspace::assignmentPlan() const noexcept {
    return assignmentPlan_;
}

bool ChoppingWorkspace::analysisPending() const noexcept {
    return analysisJob_.has_value();
}

bool ChoppingWorkspace::lazyActive() const noexcept {
    return lazyCapture_.active();
}

LazyMarkerCapture& ChoppingWorkspace::lazyMarkerCapture() noexcept {
    return lazyCapture_;
}

juce::String ChoppingWorkspace::statusText() const {
    return status_;
}

void ChoppingWorkspace::refresh() {
    const auto& set = session_.provisionalSliceSet();
    if (set.has_value() && !set->slices.empty()) {
        const auto index = std::min(session_.selectedSlice(), set->slices.size() - 1U);
        const auto& slice = set->slices[index];
        sliceReadout_.setText("Slice " + juce::String{static_cast<int>(index + 1U)} + " / " +
                                  juce::String{static_cast<int>(set->slices.size())} + "     [" +
                                  juce::String{slice.startFrame} + ", " +
                                  juce::String{slice.endFrame} + ")     " +
                                  juce::String{slice.endFrame - slice.startFrame} + " frames",
                              juce::dontSendNotification);
    } else {
        sliceReadout_.setText("No provisional slices", juce::dontSendNotification);
    }
    if (assignmentPlan_.has_value()) {
        conflictIndex_.setRange(
            1.0,
            static_cast<double>(std::max<std::size_t>(1U, assignmentPlan_->destinations.size())),
            1.0);
        const auto occupied = static_cast<int>(std::count_if(
            assignmentPlan_->destinations.begin(), assignmentPlan_->destinations.end(),
            [](const auto& destination) { return destination.occupied; }));
        assignmentReadout_.setText(
            juce::String{static_cast<int>(assignmentPlan_->requiredDestinations)} + " required / " +
                juce::String{static_cast<int>(assignmentPlan_->availableDestinations)} +
                " available; " + juce::String{occupied} + " occupied destination" +
                (occupied == 1 ? "" : "s") +
                (assignmentPlan_->hasUnresolvedConflicts() ? " require decisions" : " resolved"),
            juce::dontSendNotification);
    } else {
        assignmentReadout_.setText("Preview does not modify the project",
                                   juce::dontSendNotification);
    }
    statusLabel_.setText(status_, juce::dontSendNotification);
    undoSessionButton_.setEnabled(session_.canUndoSessionEdit());
    redoSessionButton_.setEnabled(session_.canRedoSessionEdit());
    commitAssignmentButton_.setEnabled(assignmentPlan_.has_value() &&
                                       !assignmentPlan_->hasUnresolvedConflicts());
    repaint();
}

void ChoppingWorkspace::publishSlices() {
    if (!onSlicesChanged)
        return;
    const auto& set = session_.provisionalSliceSet();
    onSlicesChanged(set.has_value() ? &*set : nullptr, session_.selectedSlice());
}

void ChoppingWorkspace::showResult(const juce::Result& result, juce::String success) {
    status_ = result.wasOk() ? std::move(success) : result.getErrorMessage();
    refresh();
}

const ExternalAssetReference* ChoppingWorkspace::sourceReference() const noexcept {
    const auto& state = controller_.project().state();
    const auto found =
        std::find_if(state.assets.begin(), state.assets.end(), [&](const auto& reference) {
            return reference.uuid == session_.target().sourceAssetUuid;
        });
    return found == state.assets.end() ? nullptr : &*found;
}

std::shared_ptr<const SampleAsset> ChoppingWorkspace::sourceAsset() const {
    return assets_.find(session_.target().sourceAssetUuid);
}

std::int64_t ChoppingWorkspace::selectedBoundary() const noexcept {
    const auto& set = session_.provisionalSliceSet();
    if (!set.has_value() || set->slices.empty())
        return session_.target().trimStart;
    const auto index = std::min(session_.selectedSlice(), set->slices.size() - 1U);
    return index == 0U ? set->slices[index].endFrame : set->slices[index].startFrame;
}

std::int64_t ChoppingWorkspace::snapMarker(std::int64_t frame) const noexcept {
    const auto& target = session_.target();
    frame = std::clamp(frame, target.trimStart + 1, target.trimEnd - 1);
    const auto grid = static_cast<std::int64_t>(markerGridFrames_.getValue());
    if (grid > 0) {
        const auto offset = frame - target.trimStart;
        auto bucket = offset / grid;
        if (offset % grid >= grid - grid / 2)
            ++bucket;
        bucket = std::min(bucket, (target.trimEnd - target.trimStart - 1) / grid);
        frame =
            std::clamp(target.trimStart + bucket * grid, target.trimStart + 1, target.trimEnd - 1);
    }
    if (!markerZeroSnap_.getToggleState())
        return frame;
    const auto asset = sourceAsset();
    if (asset == nullptr)
        return frame;
    const auto view = asset->view();
    auto best = frame;
    auto bestMagnitude = std::numeric_limits<float>::max();
    const auto first = std::max(target.trimStart + 1, frame - 128);
    const auto last = std::min(target.trimEnd - 1, frame + 128);
    for (auto candidate = first; candidate <= last; ++candidate) {
        auto magnitude = 0.0F;
        const auto offset = static_cast<std::uint64_t>(candidate) * view.channelCount;
        for (std::uint32_t channel = 0U; channel < view.channelCount; ++channel)
            magnitude += std::abs(view.interleavedData[offset + channel]);
        if (magnitude < bestMagnitude) {
            bestMagnitude = magnitude;
            best = candidate;
        }
    }
    return best;
}
} // namespace padflow
