#pragma once

#include "Chopping/SliceModel.h"
#include "Model/PadModel.h"
#include "Sampling/WaveformCache.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace padflow {
class WaveformEditor final : public juce::Component {
  public:
    enum class Marker : std::uint8_t { trimStart, trimEnd, loopStart, loopEnd };

    WaveformEditor();

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(juce::Component::FocusChangeType cause) override;
    void focusLost(juce::Component::FocusChangeType cause) override;

    void setContent(std::shared_ptr<const WaveformCache> cache, SamplePlaybackSettings playback,
                    std::uint64_t frameCount, double sampleRate, bool analysisPending,
                    bool sourceMissing);
    void clear();
    void fitSource();
    void fitTrimSelection();
    void setPlaybackPosition(std::optional<std::uint64_t> frame);
    void setSlices(const SliceSet* sliceSet, std::size_t selectedSlice);

    [[nodiscard]] std::uint64_t frameCount() const noexcept;
    [[nodiscard]] SamplePlaybackSettings playback() const noexcept;
    [[nodiscard]] bool hasWaveform() const noexcept;

    std::function<void(Marker, std::uint64_t)> onMarkerCommit;
    std::function<void(std::uint64_t)> onAuditionFromFrame;
    std::function<void(std::int64_t)> onSliceMarkerAdd;
    std::function<void(std::int64_t)> onSliceMarkerDelete;
    std::function<void(std::int64_t, std::int64_t)> onSliceMarkerCommit;
    std::function<void(std::size_t)> onSliceSelected;

  private:
    [[nodiscard]] double frameToX(std::uint64_t frame) const noexcept;
    [[nodiscard]] std::uint64_t xToFrame(float x) const noexcept;
    [[nodiscard]] Marker nearestMarker(float x, bool& found) const noexcept;
    [[nodiscard]] std::uint64_t markerFrame(Marker marker) const noexcept;
    void setMarkerFrame(Marker marker, std::uint64_t frame) noexcept;
    void zoomAround(float x, double factor);
    void constrainView() noexcept;
    void paintWaveform(juce::Graphics& graphics, juce::Rectangle<float> bounds) const;
    void paintMarker(juce::Graphics& graphics, Marker marker, juce::Colour colour,
                     const juce::String& name) const;
    [[nodiscard]] std::optional<std::size_t> nearestSliceBoundary(float x) const noexcept;

    std::shared_ptr<const WaveformCache> cache_;
    SamplePlaybackSettings playback_;
    std::uint64_t frameCount_{0U};
    double sampleRate_{0.0};
    double visibleStart_{0.0};
    double visibleEnd_{1.0};
    double dragViewStart_{0.0};
    double dragViewEnd_{1.0};
    float dragStartX_{0.0F};
    Marker selectedMarker_{Marker::trimStart};
    Marker draggedMarker_{Marker::trimStart};
    bool hasSelectedMarker_{false};
    bool draggingMarker_{false};
    bool panning_{false};
    bool analysisPending_{false};
    bool sourceMissing_{false};
    std::optional<std::uint64_t> playbackPosition_;
    std::vector<SliceRegion> slices_;
    std::size_t selectedSlice_{0U};
    std::optional<std::size_t> draggedSliceBoundary_;
    std::optional<std::size_t> selectedSliceBoundary_;
    std::int64_t originalSliceBoundaryFrame_{0};
};
} // namespace padflow
