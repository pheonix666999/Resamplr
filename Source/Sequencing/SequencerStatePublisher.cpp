#include "SequencerStatePublisher.h"

#include <algorithm>

namespace padflow {
SequencerStatePublisher::SequencerStatePublisher(PatternScheduler& scheduler,
                                                 TransportEngine& transport) noexcept
    : scheduler_(scheduler), transport_(transport) {}

juce::Result SequencerStatePublisher::publish(const ProjectState& project,
                                              const std::uint32_t sampleRate) {
    const auto* pattern =
        findPattern(project.sequencer.patterns, project.sequencer.patterns.selectedPatternUuid);
    if (pattern == nullptr)
        return juce::Result::fail("Selected pattern was not found");
    TempoMap tempoMap;
    if (const auto result = tempoMap.replacePoints(project.sequencer.tempoPoints); result.failed())
        return result;

    auto state = std::make_unique<PublishedState>();
    const auto generation = nextGeneration_++;
    if (const auto result =
            makeSchedulerSnapshot(*pattern, tempoMap, project.sequencer.probabilitySeed, sampleRate,
                                  generation, state->scheduler);
        result.failed())
        return result;
    const auto patternLength =
        MusicalTime{pattern->length.wholePpqTicks, pattern->length.fractionalTickQ16};
    if (const auto result = makeTransportConfiguration(
            tempoMap, pattern->timeSignature, patternLength, sampleRate, generation,
            project.sequencer.transport.metronomeVolume, state->transport);
        result.failed())
        return result;

    if (!transport_.publishConfiguration(&state->transport))
        return juce::Result::fail("Transport configuration could not be published");
    scheduler_.publishSnapshot(&state->scheduler);
    juce::ignoreUnused(transport_.setLoopEnabled(project.sequencer.transport.loopEnabled));
    juce::ignoreUnused(
        transport_.setMetronomeEnabled(project.sequencer.transport.metronomeEnabled));
    juce::ignoreUnused(transport_.setCountInBars(
        static_cast<CountInBars>(project.sequencer.transport.countInBars)));
    states_.push_back(std::move(state));
    return juce::Result::ok();
}

std::size_t SequencerStatePublisher::collectAcknowledged() {
    const auto schedulerGeneration = scheduler_.acknowledgedSnapshotGeneration();
    const auto transportGeneration = transport_.snapshot().configurationGeneration;
    const auto acknowledged = std::min(schedulerGeneration, transportGeneration);
    std::size_t collected = 0U;
    while (states_.size() > 1U && states_.front()->scheduler.generation <= acknowledged) {
        states_.pop_front();
        ++collected;
    }
    return collected;
}

std::size_t SequencerStatePublisher::retainedSnapshotCount() const noexcept {
    return states_.size();
}

void SequencerStatePublisher::clearWhenAudioIsStopped() noexcept {
    scheduler_.publishSnapshot(nullptr);
    transport_.clearConfigurationWhenQuiescent();
    states_.clear();
}
} // namespace padflow
