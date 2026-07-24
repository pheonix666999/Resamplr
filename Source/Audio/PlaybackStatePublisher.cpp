#include "PlaybackStatePublisher.h"

#include <algorithm>

namespace padflow {
PlaybackStatePublisher::PlaybackStatePublisher(PlaybackEngine& engine,
                                               const SampleAssetRegistry& assets) noexcept
    : engine_(engine), assets_(assets) {}

void PlaybackStatePublisher::publish(const ProjectState& project) {
    auto snapshot = std::make_unique<PublishedSnapshot>();
    snapshot->playback = makePlaybackSnapshot(project, assets_);
    snapshot->playback.generation = nextGeneration_++;
    for (const auto& bank : project.banks)
        for (const auto& pad : bank.pads)
            for (const auto& layer : pad.layers) {
                const auto asset = assets_.find(layer.assetUuid);
                if (asset != nullptr &&
                    std::find(snapshot->retainedAssets.begin(), snapshot->retainedAssets.end(),
                              asset) == snapshot->retainedAssets.end())
                    snapshot->retainedAssets.push_back(asset);
            }
    engine_.publishSnapshot(&snapshot->playback);
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
