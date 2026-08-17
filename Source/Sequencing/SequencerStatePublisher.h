#pragma once

#include "Model/PadModel.h"
#include "Sequencing/Scheduler.h"
#include "Sequencing/Transport.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>

namespace padflow {
class SequencerStatePublisher final {
  public:
    SequencerStatePublisher(PatternScheduler& scheduler, TransportEngine& transport) noexcept;

    [[nodiscard]] juce::Result publish(const ProjectState& project, std::uint32_t sampleRate);
    [[nodiscard]] std::size_t collectAcknowledged();
    [[nodiscard]] std::size_t retainedSnapshotCount() const noexcept;
    void clearWhenAudioIsStopped() noexcept;

  private:
    struct PublishedState final {
        SchedulerSnapshot scheduler;
        TransportConfiguration transport;
    };

    PatternScheduler& scheduler_;
    TransportEngine& transport_;
    std::deque<std::unique_ptr<PublishedState>> states_;
    std::uint64_t nextGeneration_{1U};
};
} // namespace padflow
