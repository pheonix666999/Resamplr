#include "PlaybackStatePublisher.h"

namespace padflow {
PlaybackStatePublisher::PlaybackStatePublisher(PlaybackEngine& engine,
                                               const SampleAssetRegistry& assets) noexcept
    : engine_(engine), assets_(assets) {}

void PlaybackStatePublisher::publish(const ProjectState& project) {
    auto snapshot = std::make_unique<PlaybackSnapshot>(makePlaybackSnapshot(project, assets_));
    snapshot->generation = nextGeneration_++;
    engine_.publishSnapshot(snapshot.get());
    snapshots_.push_back(std::move(snapshot));
}

std::size_t PlaybackStatePublisher::collectAcknowledged() {
    const auto acknowledged = engine_.acknowledgedSnapshotGeneration();
    std::size_t collected = 0U;
    while (snapshots_.size() > 1U && snapshots_.front()->generation <= acknowledged) {
        snapshots_.pop_front();
        ++collected;
    }
    return collected;
}

std::size_t PlaybackStatePublisher::retainedSnapshotCount() const noexcept {
    return snapshots_.size();
}

void PlaybackStatePublisher::clearWhenAudioIsStopped() noexcept {
    engine_.publishSnapshot(nullptr);
    snapshots_.clear();
}
} // namespace padflow
