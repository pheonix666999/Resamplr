#include "LazyMarkerCapture.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace padflow {
juce::Result LazyMarkerCapture::start(const LazyCaptureSettings settings) {
    if (settings.trimStart < 0 || settings.trimStart >= settings.trimEnd ||
        settings.minimumSliceFrames <= 0 || settings.quantizeFrames < 0)
        return juce::Result::fail("Lazy capture settings are invalid");
    if (active_.load(std::memory_order_acquire))
        return juce::Result::fail("Lazy capture is already active");

    queue_.resetWhenQuiescent();
    overflowCount_.store(0U, std::memory_order_relaxed);
    trimStart_.store(settings.trimStart, std::memory_order_relaxed);
    trimEnd_.store(settings.trimEnd, std::memory_order_relaxed);
    minimumSliceFrames_.store(settings.minimumSliceFrames, std::memory_order_relaxed);
    quantizeFrames_.store(settings.quantizeFrames, std::memory_order_relaxed);
    active_.store(true, std::memory_order_release);
    return juce::Result::ok();
}

void LazyMarkerCapture::stop() noexcept {
    active_.store(false, std::memory_order_release);
}

void LazyMarkerCapture::resetWhenQuiescent() noexcept {
    active_.store(false, std::memory_order_release);
    queue_.resetWhenQuiescent();
}

bool LazyMarkerCapture::captureFromAudioThread(const std::int64_t sourceFrame,
                                               const LazyMarkerSource source) noexcept {
    if (!active_.load(std::memory_order_acquire))
        return false;
    const LazyMarkerEvent event{quantizeAndClamp(sourceFrame), source};
    if (queue_.tryPush(event))
        return true;
    overflowCount_.fetch_add(1U, std::memory_order_relaxed);
    return false;
}

LazyDrainResult LazyMarkerCapture::drainToSession(ChoppingSession& session) {
    std::vector<LazyMarkerEvent> events;
    events.reserve(queue_.size());
    LazyMarkerEvent event;
    while (queue_.tryPop(event))
        events.push_back(event);
    std::stable_sort(events.begin(), events.end(), [](const auto& first, const auto& second) {
        return first.sourceFrame < second.sourceFrame;
    });

    LazyDrainResult result;
    result.overflowCount = overflowCount_.load(std::memory_order_acquire);
    const auto minimum = minimumSliceFrames_.load(std::memory_order_relaxed);
    for (const auto& captured : events) {
        const auto& provisional = session.provisionalSliceSet();
        if (!provisional.has_value()) {
            ++result.rejected;
            continue;
        }
        auto tooClose = false;
        for (const auto& slice : provisional->slices) {
            if (std::abs(captured.sourceFrame - slice.startFrame) < minimum ||
                std::abs(captured.sourceFrame - slice.endFrame) < minimum) {
                tooClose = true;
                break;
            }
        }
        if (!tooClose && session.addMarker(captured.sourceFrame).wasOk())
            ++result.accepted;
        else
            ++result.rejected;
    }
    return result;
}

bool LazyMarkerCapture::active() const noexcept {
    return active_.load(std::memory_order_acquire);
}

std::size_t LazyMarkerCapture::queuedEventCount() const noexcept {
    return queue_.size();
}

std::uint64_t LazyMarkerCapture::overflowCount() const noexcept {
    return overflowCount_.load(std::memory_order_acquire);
}

std::int64_t LazyMarkerCapture::quantizeAndClamp(const std::int64_t sourceFrame) const noexcept {
    const auto trimStart = trimStart_.load(std::memory_order_relaxed);
    const auto trimEnd = trimEnd_.load(std::memory_order_relaxed);
    auto frame = std::clamp(sourceFrame, trimStart, trimEnd);
    const auto grid = quantizeFrames_.load(std::memory_order_relaxed);
    if (grid > 0) {
        const auto offset = frame - trimStart;
        auto bucket = offset / grid;
        const auto remainder = offset % grid;
        if (remainder >= grid - grid / 2)
            ++bucket;
        const auto maximumOffset = trimEnd - trimStart;
        frame = bucket > maximumOffset / grid ? trimEnd : trimStart + bucket * grid;
    }
    return frame;
}
} // namespace padflow
