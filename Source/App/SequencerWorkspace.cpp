#include "SequencerWorkspace.h"

#include <algorithm>
#include <cmath>

namespace padflow {
namespace {
constexpr int laneHeight = 28;
constexpr int laneLabelWidth = 92;
constexpr int cellWidth = 42;
constexpr int visibleStepCount = 16;
constexpr auto background = 0xff11161cU;
constexpr auto panel = 0xff222b34U;
constexpr auto gridLine = 0xff3a4651U;
constexpr auto active = 0xff52c9baU;
constexpr auto playhead = 0xffe4ad55U;

void configureSlider(juce::Slider& slider, const char* id, const double minimum,
                     const double maximum, const double interval) {
    slider.setComponentID(id);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 74, 22);
    slider.setRange(minimum, maximum, interval);
}
} // namespace

SequencerWorkspace::StepGrid::StepGrid(SequencerWorkspace& owner) noexcept : owner_(owner) {
    setComponentID("sequencer-step-grid");
    setTitle("Forty-eight pad lane step grid");
    setDescription("Click a cell to create or remove a sequence event");
}

void SequencerWorkspace::StepGrid::paint(juce::Graphics& graphics) {
    graphics.fillAll(juce::Colour{background});
    const auto* pattern = owner_.selectedPattern();
    if (pattern == nullptr)
        return;
    const auto resolution = resolutionTicks(pattern->stepResolution);
    for (std::size_t lane = 0; lane < totalPadCount; ++lane) {
        const auto y = static_cast<int>(lane) * laneHeight;
        graphics.setColour(juce::Colour{panel}.withAlpha(lane % 2U == 0U ? 0.8F : 0.55F));
        graphics.fillRect(0, y, getWidth(), laneHeight - 1);
        graphics.setColour(juce::Colours::white.withAlpha(0.72F));
        graphics.drawText("B" + juce::String{static_cast<int>(lane / padsPerBank + 1U)} + " · " +
                              owner_.controller_.project().pad(lane).name,
                          6, y, laneLabelWidth - 10, laneHeight, juce::Justification::centredLeft,
                          true);
        for (int step = 0; step < visibleStepCount; ++step) {
            const auto x = laneLabelWidth + step * cellWidth;
            graphics.setColour(juce::Colour{gridLine}.withAlpha(step % 4 == 0 ? 0.95F : 0.6F));
            graphics.drawRect(x, y, cellWidth, laneHeight - 1);
            const auto tick = static_cast<std::int64_t>(step) * resolution;
            const auto found = std::find_if(
                pattern->events.begin(), pattern->events.end(), [&](const auto& event) {
                    return event.stablePadOrder == lane && event.start.wholePpqTicks == tick &&
                           event.start.fractionalTickQ16 == 0U;
                });
            if (found != pattern->events.end()) {
                const auto selected =
                    found->uuid ==
                    owner_.controller_.project().state().sequencer.ui.selectedEventUuid;
                graphics.setColour(juce::Colour{active}.withAlpha(selected ? 1.0F : 0.76F));
                graphics.fillRoundedRectangle(static_cast<float>(x + 5), static_cast<float>(y + 5),
                                              static_cast<float>(cellWidth - 10),
                                              static_cast<float>(laneHeight - 11), 4.0F);
            }
        }
    }
    const auto& state = owner_.controller_.project().state();
    const auto microBpm = state.sequencer.tempoPoints.front().microBpm;
    const auto sampleRate = std::max(1.0, owner_.runtime_.status().sampleRate);
    const auto patternFrames = static_cast<std::int64_t>(std::llround(
        static_cast<long double>(pattern->length.wholePpqTicks) * 60.0L *
        static_cast<long double>(sampleRate) * 1'000'000.0L /
        (static_cast<long double>(microBpm) * static_cast<long double>(ppqTicksPerQuarterNote))));
    if (patternFrames > 0) {
        const auto position = owner_.displayedPlayheadFrame() % patternFrames;
        const auto x = laneLabelWidth +
                       static_cast<int>(static_cast<long double>(position) *
                                        static_cast<long double>(visibleStepCount * cellWidth) /
                                        static_cast<long double>(patternFrames));
        graphics.setColour(juce::Colour{playhead});
        graphics.drawVerticalLine(x, 0.0F, static_cast<float>(getHeight()));
    }
}

void SequencerWorkspace::StepGrid::mouseDown(const juce::MouseEvent& event) {
    if (event.x < laneLabelWidth || event.y < 0)
        return;
    const auto lane = static_cast<std::size_t>(event.y / laneHeight);
    const auto step = static_cast<std::size_t>((event.x - laneLabelWidth) / cellWidth);
    juce::ignoreUnused(owner_.toggleStep(lane, step));
}

SequencerWorkspace::SequencerWorkspace(ApplicationController& controller, AudioRuntime& runtime,
                                       InputRouter& input)
    : controller_(controller), runtime_(runtime), input_(input), grid_(*this) {
    setComponentID("sequencer-workspace");
    setTitle("PadFlow Sequencer");
    setDescription("Transport, patterns, forty-eight pad lanes, and event editing");
    for (auto* component : std::array<juce::Component*, 18U>{
             &closeButton_, &playButton_, &stopButton_, &recordButton_, &panicButton_, &loopButton_,
             &metronomeButton_, &countInBox_, &tempoSlider_, &patternBox_, &patternName_,
             &addPatternButton_, &duplicatePatternButton_, &deletePatternButton_,
             &clearPatternButton_, &velocitySlider_, &probabilitySlider_, &ratchetSlider_})
        addAndMakeVisible(*component);
    addAndMakeVisible(nudgeSlider_);
    addAndMakeVisible(gridViewport_);
    addAndMakeVisible(statusLabel_);
    gridViewport_.setViewedComponent(&grid_, false);
    gridViewport_.setScrollBarsShown(true, true);
    grid_.setSize(laneLabelWidth + visibleStepCount * cellWidth, laneHeight * totalPadCount);

    closeButton_.setComponentID("sequencer-close");
    playButton_.setComponentID("transport-play");
    stopButton_.setComponentID("transport-stop");
    recordButton_.setComponentID("transport-record");
    panicButton_.setComponentID("transport-panic");
    loopButton_.setComponentID("transport-loop");
    metronomeButton_.setComponentID("transport-metronome");
    countInBox_.setComponentID("transport-count-in");
    countInBox_.addItemList({"Count-in Off", "Count-in 1 bar", "Count-in 2 bars"}, 1);
    configureSlider(tempoSlider_, "transport-bpm", 20.0, 300.0, 0.001);
    tempoSlider_.setTextValueSuffix(" BPM");
    patternBox_.setComponentID("pattern-selector");
    patternName_.setComponentID("pattern-name");
    configureSlider(velocitySlider_, "event-velocity", 1.0, 127.0, 1.0);
    configureSlider(probabilitySlider_, "event-probability", 0.0, 100.0, 0.1);
    probabilitySlider_.setTextValueSuffix(" %");
    configureSlider(ratchetSlider_, "event-ratchet", 1.0, 16.0, 1.0);
    configureSlider(nudgeSlider_, "event-nudge", -240.0, 240.0, 1.0);
    nudgeSlider_.setTextValueSuffix(" ticks");

    closeButton_.onClick = [this] {
        if (onClose)
            onClose();
    };
    playButton_.onClick = [this] {
        applyResult(runtime_.transport().play() ? juce::Result::ok()
                                                : juce::Result::fail("Transport queue is full"));
    };
    stopButton_.onClick = [this] {
        applyResult(runtime_.transport().stop() ? juce::Result::ok()
                                                : juce::Result::fail("Transport queue is full"));
    };
    recordButton_.onClick = [this] {
        if (recorder_.isActive())
            juce::ignoreUnused(finishLiveRecording());
        else
            juce::ignoreUnused(beginLiveRecording(PatternRecordMode::overdub));
    };
    panicButton_.onClick = [this] {
        cancelLiveRecording();
        juce::ignoreUnused(runtime_.transport().panic());
    };
    loopButton_.onClick = [this] {
        auto preferences = controller_.project().state().sequencer.transport;
        preferences.loopEnabled = loopButton_.getToggleState();
        applyResult(controller_.setTransportPreferences(preferences));
    };
    metronomeButton_.onClick = [this] {
        auto preferences = controller_.project().state().sequencer.transport;
        preferences.metronomeEnabled = metronomeButton_.getToggleState();
        applyResult(controller_.setTransportPreferences(preferences));
    };
    countInBox_.onChange = [this] {
        if (refreshing_)
            return;
        auto preferences = controller_.project().state().sequencer.transport;
        preferences.countInBars = static_cast<std::uint8_t>(countInBox_.getSelectedItemIndex());
        applyResult(controller_.setTransportPreferences(preferences));
    };
    tempoSlider_.onDragEnd = [this] {
        applyResult(controller_.setTempo(
            static_cast<std::int64_t>(std::llround(tempoSlider_.getValue() * 1'000'000.0))));
    };
    patternBox_.onChange = [this] {
        if (!refreshing_ && patternBox_.getSelectedItemIndex() >= 0)
            applyResult(controller_.selectPattern(
                controller_.project()
                    .state()
                    .sequencer.patterns
                    .patterns[static_cast<std::size_t>(patternBox_.getSelectedItemIndex())]
                    .uuid));
    };
    patternName_.onReturnKey = [this] {
        applyResult(controller_.renameSelectedPattern(patternName_.getText()));
    };
    addPatternButton_.onClick = [this] { applyResult(controller_.createPattern("Pattern")); };
    duplicatePatternButton_.onClick = [this] {
        applyResult(controller_.duplicateSelectedPattern());
    };
    deletePatternButton_.onClick = [this] { applyResult(controller_.deleteSelectedPattern()); };
    clearPatternButton_.onClick = [this] { applyResult(controller_.clearSelectedPattern()); };
    velocitySlider_.onDragEnd = [this] { updateSelectedEventFromControls(); };
    probabilitySlider_.onDragEnd = [this] { updateSelectedEventFromControls(); };
    ratchetSlider_.onDragEnd = [this] { updateSelectedEventFromControls(); };
    nudgeSlider_.onDragEnd = [this] { updateSelectedEventFromControls(); };
    refresh();
    input_.setPatternRecorder(&recorder_, &runtime_.transport());
    startTimerHz(30);
}

SequencerWorkspace::~SequencerWorkspace() {
    stopTimer();
    input_.setPatternRecorder(nullptr, nullptr);
    gridViewport_.setViewedComponent(nullptr, false);
}

void SequencerWorkspace::paint(juce::Graphics& graphics) {
    graphics.fillAll(juce::Colour{background});
    graphics.setColour(juce::Colour{active});
    graphics.setFont(juce::FontOptions{22.0F, juce::Font::bold});
    graphics.drawText("PADFLOW SEQUENCER", 16, 8, 300, 32, juce::Justification::centredLeft);
}

void SequencerWorkspace::resized() {
    auto area = getLocalBounds().reduced(14);
    auto title = area.removeFromTop(38);
    closeButton_.setBounds(title.removeFromRight(150));
    auto transport = area.removeFromTop(42);
    for (auto* button : {&playButton_, &stopButton_, &recordButton_, &panicButton_})
        button->setBounds(transport.removeFromLeft(72).reduced(3));
    loopButton_.setBounds(transport.removeFromLeft(74));
    metronomeButton_.setBounds(transport.removeFromLeft(104));
    countInBox_.setBounds(transport.removeFromLeft(132).reduced(3));
    tempoSlider_.setBounds(transport.removeFromLeft(220).reduced(3));
    auto patterns = area.removeFromTop(42);
    patternBox_.setBounds(patterns.removeFromLeft(170).reduced(3));
    patternName_.setBounds(patterns.removeFromLeft(190).reduced(3));
    for (auto* button : {&addPatternButton_, &duplicatePatternButton_, &deletePatternButton_,
                         &clearPatternButton_})
        button->setBounds(patterns.removeFromLeft(92).reduced(3));
    auto editor = area.removeFromBottom(76);
    velocitySlider_.setBounds(editor.removeFromLeft(230).reduced(4));
    probabilitySlider_.setBounds(editor.removeFromLeft(250).reduced(4));
    ratchetSlider_.setBounds(editor.removeFromLeft(220).reduced(4));
    nudgeSlider_.setBounds(editor.removeFromLeft(240).reduced(4));
    statusLabel_.setBounds(editor.reduced(4));
    gridViewport_.setBounds(area.reduced(3));
}

const Pattern* SequencerWorkspace::selectedPattern() const noexcept {
    const auto& state = controller_.project().state();
    return findPattern(state.sequencer.patterns, state.sequencer.patterns.selectedPatternUuid);
}

const SequenceEvent* SequencerWorkspace::selectedEvent() const noexcept {
    const auto* pattern = selectedPattern();
    if (pattern == nullptr)
        return nullptr;
    const auto& uuid = controller_.project().state().sequencer.ui.selectedEventUuid;
    const auto found = std::find_if(pattern->events.begin(), pattern->events.end(),
                                    [&](const auto& event) { return event.uuid == uuid; });
    return found == pattern->events.end() ? nullptr : &*found;
}

bool SequencerWorkspace::toggleStep(const std::size_t lane, const std::size_t step) {
    const auto* pattern = selectedPattern();
    if (pattern == nullptr || lane >= totalPadCount || step >= visibleStepCount)
        return false;
    const auto ticks = static_cast<std::int64_t>(step) * resolutionTicks(pattern->stepResolution);
    const auto found =
        std::find_if(pattern->events.begin(), pattern->events.end(), [&](const auto& event) {
            return event.stablePadOrder == lane && event.start == MusicalTime{ticks, 0U};
        });
    juce::Result result = juce::Result::ok();
    if (found != pattern->events.end()) {
        result = controller_.deleteSelectedEvent(found->uuid);
    } else {
        SequenceEvent event;
        event.padUuid = controller_.project().pad(lane).uuid;
        event.stablePadOrder = static_cast<std::uint16_t>(lane);
        event.start = {ticks, 0U};
        event.duration = {resolutionTicks(pattern->stepResolution), 0U};
        event.ratchetSpacing = event.duration;
        result = controller_.addEventToSelectedPattern(std::move(event));
    }
    applyResult(result);
    return result.wasOk();
}

bool SequencerWorkspace::setSelectedEventVelocity(const std::uint8_t velocity) {
    const auto* event = selectedEvent();
    if (event == nullptr)
        return false;
    auto replacement = *event;
    replacement.velocity = velocity;
    const auto result = controller_.updateSelectedEvent(std::move(replacement));
    applyResult(result);
    return result.wasOk();
}

bool SequencerWorkspace::setSelectedEventProbability(const ProbabilityQ32 probability) {
    const auto* event = selectedEvent();
    if (event == nullptr)
        return false;
    auto replacement = *event;
    replacement.probability = probability;
    const auto result = controller_.updateSelectedEvent(std::move(replacement));
    applyResult(result);
    return result.wasOk();
}

bool SequencerWorkspace::setSelectedEventRatchet(const std::uint8_t count,
                                                 const MusicalDuration spacing) {
    const auto* event = selectedEvent();
    if (event == nullptr)
        return false;
    auto replacement = *event;
    replacement.ratchetCount = count;
    replacement.ratchetSpacing = spacing;
    const auto result = controller_.updateSelectedEvent(std::move(replacement));
    applyResult(result);
    return result.wasOk();
}

bool SequencerWorkspace::setSelectedEventNudge(const MicroOffsetQ16 nudge) {
    const auto* event = selectedEvent();
    if (event == nullptr)
        return false;
    auto replacement = *event;
    replacement.microOffset = nudge;
    const auto result = controller_.updateSelectedEvent(std::move(replacement));
    applyResult(result);
    return result.wasOk();
}

bool SequencerWorkspace::stepRecordPad(const std::size_t lane, const std::uint8_t velocity) {
    const auto* selected = selectedPattern();
    if (selected == nullptr || lane >= totalPadCount)
        return false;
    auto replacement = *selected;
    auto stepCursor = controller_.project().state().sequencer.ui.stepCursorTicks;
    auto result = PatternRecorder::stepRecord(replacement, controller_.project().pad(lane).uuid,
                                              static_cast<std::uint16_t>(lane), velocity,
                                              stepCursor, controller_.project().revision() + 1U);
    if (result.wasOk())
        result = controller_.replaceSelectedPattern(std::move(replacement), "Step record note");
    if (result.wasOk()) {
        auto ui = controller_.project().state().sequencer.ui;
        ui.stepCursorTicks = stepCursor;
        result = controller_.setSequencerUiState(std::move(ui));
    }
    applyResult(result);
    return result.wasOk();
}

bool SequencerWorkspace::beginLiveRecording(const PatternRecordMode mode) {
    const auto status = runtime_.status();
    const auto sampleRate = status.sampleRate > 0.0
                                ? static_cast<std::uint32_t>(std::llround(status.sampleRate))
                                : 48'000U;
    auto result = recorder_.beginTake(controller_.project().state(), mode, sampleRate);
    if (result.wasOk() && !runtime_.transport().record()) {
        recorder_.cancel();
        result = juce::Result::fail("Transport queue is full");
    }
    applyResult(result);
    return result.wasOk();
}

bool SequencerWorkspace::finishLiveRecording() {
    if (!recorder_.isActive())
        return false;
    juce::ignoreUnused(runtime_.transport().stop());
    Pattern resultPattern;
    auto result =
        recorder_.finishTake(runtime_.transport().snapshot().framePosition, resultPattern);
    if (result.wasOk())
        result =
            controller_.replaceSelectedPattern(std::move(resultPattern), "Record pattern take");
    applyResult(result);
    return result.wasOk();
}

void SequencerWorkspace::cancelLiveRecording() noexcept {
    recorder_.cancel();
    juce::ignoreUnused(runtime_.transport().stop());
}

void SequencerWorkspace::updateSelectedEventFromControls() {
    const auto* event = selectedEvent();
    if (event == nullptr)
        return;
    auto replacement = *event;
    replacement.velocity = static_cast<std::uint8_t>(std::llround(velocitySlider_.getValue()));
    replacement.probability = static_cast<ProbabilityQ32>(std::llround(
        probabilitySlider_.getValue() * static_cast<double>(probabilityQ32Maximum) / 100.0));
    replacement.ratchetCount = static_cast<std::uint8_t>(std::llround(ratchetSlider_.getValue()));
    replacement.ratchetSpacing = {
        std::max<std::int64_t>(1, replacement.duration.wholePpqTicks /
                                      static_cast<std::int64_t>(replacement.ratchetCount)),
        0U};
    replacement.microOffset = MicroOffsetQ16::fromWholeTicks(
        static_cast<std::int16_t>(std::llround(nudgeSlider_.getValue())));
    applyResult(controller_.updateSelectedEvent(std::move(replacement)));
}

void SequencerWorkspace::applyResult(juce::Result result) {
    statusLabel_.setText(result.wasOk() ? "Ready" : result.getErrorMessage(),
                         juce::dontSendNotification);
    if (result.wasOk()) {
        refresh();
        if (onProjectChanged)
            onProjectChanged();
    }
}

void SequencerWorkspace::refresh() {
    const juce::ScopedValueSetter<bool> guard{refreshing_, true};
    const auto& state = controller_.project().state();
    patternBox_.clear(juce::dontSendNotification);
    int selectedId = 0;
    for (std::size_t index = 0; index < state.sequencer.patterns.patterns.size(); ++index) {
        const auto& pattern = state.sequencer.patterns.patterns[index];
        patternBox_.addItem(pattern.name, static_cast<int>(index + 1U));
        if (pattern.uuid == state.sequencer.patterns.selectedPatternUuid)
            selectedId = static_cast<int>(index + 1U);
    }
    patternBox_.setSelectedId(selectedId, juce::dontSendNotification);
    const auto* pattern = selectedPattern();
    patternName_.setText(pattern != nullptr ? pattern->name : juce::String{}, false);
    const auto microBpm = state.sequencer.tempoPoints.empty()
                              ? defaultTempoMicroBpm
                              : state.sequencer.tempoPoints.front().microBpm;
    tempoSlider_.setValue(static_cast<double>(microBpm) / 1'000'000.0, juce::dontSendNotification);
    loopButton_.setToggleState(state.sequencer.transport.loopEnabled, juce::dontSendNotification);
    metronomeButton_.setToggleState(state.sequencer.transport.metronomeEnabled,
                                    juce::dontSendNotification);
    countInBox_.setSelectedItemIndex(state.sequencer.transport.countInBars,
                                     juce::dontSendNotification);
    const auto* event = selectedEvent();
    const auto enabled = event != nullptr;
    for (auto* slider : {&velocitySlider_, &probabilitySlider_, &ratchetSlider_, &nudgeSlider_})
        slider->setEnabled(enabled);
    if (event != nullptr) {
        velocitySlider_.setValue(event->velocity, juce::dontSendNotification);
        probabilitySlider_.setValue(static_cast<double>(event->probability) * 100.0 /
                                        static_cast<double>(probabilityQ32Maximum),
                                    juce::dontSendNotification);
        ratchetSlider_.setValue(event->ratchetCount, juce::dontSendNotification);
        nudgeSlider_.setValue(static_cast<double>(event->microOffset.rawValue) /
                                  static_cast<double>(subTickUnitsPerTick),
                              juce::dontSendNotification);
    }
    grid_.repaint();
}

void SequencerWorkspace::timerCallback() {
    juce::ignoreUnused(recorder_.drain());
    displayedPlayheadFrame_ = runtime_.transport().snapshot().framePosition;
    grid_.repaint();
}

std::int64_t SequencerWorkspace::displayedPlayheadFrame() const noexcept {
    return displayedPlayheadFrame_;
}
} // namespace padflow
