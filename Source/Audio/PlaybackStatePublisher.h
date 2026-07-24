#pragma once

#include "Audio/PlaybackEngine.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>

namespace padflow {
class PlaybackStatePublisher final {
  public:
    PlaybackStatePublisher(PlaybackEngine& engine, const SampleAssetRegistry& assets) noexcept;

    void publish(const ProjectState& project);
    [[nodiscard]] std::size_t collectAcknowledged();
    [[nodiscard]] std::size_t retainedSnapshotCount() const noexcept;
    void clearWhenAudioIsStopped() noexcept;

  private:
    PlaybackEngine& engine_;
    const SampleAssetRegistry& assets_;
    std::deque<std::unique_ptr<PlaybackSnapshot>> snapshots_;
    std::uint64_t nextGeneration_{1U};
};
} // namespace padflow
