#include "WaveformEditor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace padflow {
namespace {
constexpr auto editorBackground = 0xff11161bU;
constexpr auto editorBorder = 0xff3b4957U;
constexpr auto editorWave = 0xff8ba5b5U;
constexpr auto editorTrim = 0xff50c8bbU;
constexpr auto editorLoop = 0xffe1aa55U;
constexpr auto editorText = 0xffd8e1e8U;
constexpr float markerHitRadius = 10.0F;

[[nodiscard]] std::uint64_t roundedFrame(const double frame, const std::uint64_t maximum) noexcept {
    if (!std::isfinite(frame) || frame <= 0.0)
        return 0U;
    if (frame >= static_cast<double>(maximum))
        return maximum;
    return static_cast<std::uint64_t>(std::llround(frame));
}
} // namespace

WaveformEditor::WaveformEditor() {
    setComponentID("waveform-editor");
    setTitle("Selected layer waveform editor");
    setDescription(
        "Waveform with draggable trim and loop markers. Mouse wheel zooms; middle or right drag "
        "pans; arrow keys nudge the selected marker.");
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

void WaveformEditor::setContent(std::shared_ptr<const WaveformCache> cache,
                                SamplePlaybackSettings playback, const std::uint64_t frameCount,
                                const double sampleRate, const bool analysisPending,
                                const bool sourceMissing) {
    const auto sourceChanged =
        frameCount_ != frameCount || (cache_ != nullptr && cache != nullptr &&
                                      cache_->key().assetUuid != cache->key().assetUuid);
    cache_ = std::move(cache);
    playback_ = playback;
    frameCount_ = frameCount;
    sampleRate_ = sampleRate;
    analysisPending_ = analysisPending;
    sourceMissing_ = sourceMissing;
    if (sourceChanged || visibleEnd_ <= visibleStart_ || visibleEnd_ > frameCount_)
        fitSource();
    repaint();
}

void WaveformEditor::clear() {
    cache_.reset();
    playback_ = {};
    frameCount_ = 0U;
    sampleRate_ = 0.0;
    visibleStart_ = 0.0;
    visibleEnd_ = 1.0;
    analysisPending_ = false;
    sourceMissing_ = false;
    draggingMarker_ = false;
    panning_ = false;
    hasSelectedMarker_ = false;
    repaint();
}

void WaveformEditor::fitSource() {
    visibleStart_ = 0.0;
    visibleEnd_ = static_cast<double>(std::max<std::uint64_t>(frameCount_, 1U));
    repaint();
}

void WaveformEditor::fitTrimSelection() {
    if (frameCount_ == 0U || playback_.startFrame >= playback_.endFrame)
        return;
    const auto length = static_cast<double>(playback_.endFrame - playback_.startFrame);
    const auto padding = std::max(1.0, length * 0.08);
    visibleStart_ = static_cast<double>(playback_.startFrame) - padding;
    visibleEnd_ = static_cast<double>(playback_.endFrame) + padding;
    constrainView();
    repaint();
}

std::uint64_t WaveformEditor::frameCount() const noexcept {
    return frameCount_;
}

SamplePlaybackSettings WaveformEditor::playback() const noexcept {
    return playback_;
}

bool WaveformEditor::hasWaveform() const noexcept {
    return cache_ != nullptr;
}

double WaveformEditor::frameToX(const std::uint64_t frame) const noexcept {
    const auto width = std::max(1, getWidth() - 2);
    const auto visible = std::max(1.0, visibleEnd_ - visibleStart_);
    return 1.0 +
           (static_cast<double>(frame) - visibleStart_) / visible * static_cast<double>(width);
}

std::uint64_t WaveformEditor::xToFrame(const float x) const noexcept {
    const auto width = std::max(1, getWidth() - 2);
    const auto proportion =
        std::clamp((static_cast<double>(x) - 1.0) / static_cast<double>(width), 0.0, 1.0);
    return roundedFrame(visibleStart_ + proportion * (visibleEnd_ - visibleStart_), frameCount_);
}

std::uint64_t WaveformEditor::markerFrame(const Marker marker) const noexcept {
    switch (marker) {
    case Marker::trimStart:
        return playback_.startFrame;
    case Marker::trimEnd:
        return playback_.endFrame;
    case Marker::loopStart:
        return playback_.loopStartFrame;
    case Marker::loopEnd:
        return playback_.loopEndFrame;
    }
    return 0U;
}

WaveformEditor::Marker WaveformEditor::nearestMarker(const float x, bool& found) const noexcept {
    Marker nearest = Marker::trimStart;
    auto distance = std::numeric_limits<double>::max();
    for (const auto marker :
         {Marker::trimStart, Marker::trimEnd, Marker::loopStart, Marker::loopEnd}) {
        const auto candidate = std::abs(frameToX(markerFrame(marker)) - static_cast<double>(x));
        if (candidate < distance) {
            distance = candidate;
            nearest = marker;
        }
    }
    found = distance <= markerHitRadius;
    return nearest;
}

void WaveformEditor::setMarkerFrame(const Marker marker, const std::uint64_t frame) noexcept {
    if (frameCount_ == 0U)
        return;
    switch (marker) {
    case Marker::trimStart:
        playback_.startFrame = std::min(frame, playback_.endFrame - 1U);
        playback_.loopStartFrame = std::max(playback_.loopStartFrame, playback_.startFrame);
        if (playback_.loopStartFrame >= playback_.loopEndFrame) {
            playback_.loopStartFrame = playback_.startFrame;
            playback_.loopEndFrame = playback_.endFrame;
            playback_.loopEnabled = false;
        }
        break;
    case Marker::trimEnd:
        playback_.endFrame = std::clamp(frame, playback_.startFrame + 1U, frameCount_);
        playback_.loopEndFrame = std::min(playback_.loopEndFrame, playback_.endFrame);
        if (playback_.loopStartFrame >= playback_.loopEndFrame) {
            playback_.loopStartFrame = playback_.startFrame;
            playback_.loopEndFrame = playback_.endFrame;
            playback_.loopEnabled = false;
        }
        break;
    case Marker::loopStart:
        playback_.loopStartFrame =
            std::clamp(frame, playback_.startFrame, playback_.loopEndFrame - 1U);
        break;
    case Marker::loopEnd:
        playback_.loopEndFrame =
            std::clamp(frame, playback_.loopStartFrame + 1U, playback_.endFrame);
        break;
    }
}

void WaveformEditor::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus();
    if (frameCount_ == 0U)
        return;
    if (event.mods.isMiddleButtonDown() || event.mods.isRightButtonDown()) {
        panning_ = true;
        dragStartX_ = event.position.x;
        dragViewStart_ = visibleStart_;
        dragViewEnd_ = visibleEnd_;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    bool found = false;
    draggedMarker_ = nearestMarker(event.position.x, found);
    if (found) {
        selectedMarker_ = draggedMarker_;
        hasSelectedMarker_ = true;
        draggingMarker_ = true;
        setMarkerFrame(draggedMarker_, xToFrame(event.position.x));
        repaint();
    } else if (onAuditionFromFrame) {
        onAuditionFromFrame(xToFrame(event.position.x));
    }
}

void WaveformEditor::mouseDrag(const juce::MouseEvent& event) {
    if (panning_) {
        const auto visible = dragViewEnd_ - dragViewStart_;
        const auto delta = -static_cast<double>(event.position.x - dragStartX_) /
                           static_cast<double>(std::max(1, getWidth())) * visible;
        visibleStart_ = dragViewStart_ + delta;
        visibleEnd_ = dragViewEnd_ + delta;
        constrainView();
        repaint();
    } else if (draggingMarker_) {
        setMarkerFrame(draggedMarker_, xToFrame(event.position.x));
        repaint();
    }
}

void WaveformEditor::mouseUp(const juce::MouseEvent&) {
    if (draggingMarker_ && onMarkerCommit)
        onMarkerCommit(draggedMarker_, markerFrame(draggedMarker_));
    draggingMarker_ = false;
    panning_ = false;
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

void WaveformEditor::mouseWheelMove(const juce::MouseEvent& event,
                                    const juce::MouseWheelDetails& wheel) {
    if (frameCount_ == 0U)
        return;
    if (wheel.deltaY != 0.0F)
        zoomAround(event.position.x, wheel.deltaY > 0.0F ? 0.75 : 1.333333333);
    else if (wheel.deltaX != 0.0F) {
        const auto amount = (visibleEnd_ - visibleStart_) * -static_cast<double>(wheel.deltaX);
        visibleStart_ += amount;
        visibleEnd_ += amount;
        constrainView();
        repaint();
    }
}

bool WaveformEditor::keyPressed(const juce::KeyPress& key) {
    if (!hasSelectedMarker_ || frameCount_ == 0U)
        return false;
    const auto code = key.getKeyCode();
    if (code != juce::KeyPress::leftKey && code != juce::KeyPress::rightKey)
        return false;
    const auto visibleFrames = std::max(1.0, visibleEnd_ - visibleStart_);
    const auto coarse =
        std::max<std::uint64_t>(1U, static_cast<std::uint64_t>(visibleFrames / 100.0));
    const auto step = key.getModifiers().isShiftDown() ? coarse : 1U;
    const auto current = markerFrame(selectedMarker_);
    const auto replacement = code == juce::KeyPress::leftKey
                                 ? (current > step ? current - step : 0U)
                                 : std::min(frameCount_, current + step);
    setMarkerFrame(selectedMarker_, replacement);
    if (onMarkerCommit)
        onMarkerCommit(selectedMarker_, markerFrame(selectedMarker_));
    repaint();
    return true;
}

void WaveformEditor::focusGained(juce::Component::FocusChangeType) {
    repaint();
}

void WaveformEditor::focusLost(juce::Component::FocusChangeType) {
    repaint();
}

void WaveformEditor::zoomAround(const float x, const double factor) {
    const auto currentLength = std::max(1.0, visibleEnd_ - visibleStart_);
    const auto minimumLength = std::min<double>(frameCount_, 16.0);
    const auto newLength =
        std::clamp(currentLength * factor, std::max(1.0, minimumLength),
                   static_cast<double>(std::max<std::uint64_t>(frameCount_, 1U)));
    const auto anchor = static_cast<double>(xToFrame(x));
    const auto ratio = std::clamp(static_cast<double>(x) / std::max(1, getWidth()), 0.0, 1.0);
    visibleStart_ = anchor - ratio * newLength;
    visibleEnd_ = visibleStart_ + newLength;
    constrainView();
    repaint();
}

void WaveformEditor::constrainView() noexcept {
    const auto maximum = static_cast<double>(std::max<std::uint64_t>(frameCount_, 1U));
    auto length = std::clamp(visibleEnd_ - visibleStart_, 1.0, maximum);
    visibleStart_ = std::clamp(visibleStart_, 0.0, std::max(0.0, maximum - length));
    visibleEnd_ = visibleStart_ + length;
    if (visibleEnd_ > maximum) {
        visibleEnd_ = maximum;
        visibleStart_ = std::max(0.0, maximum - length);
    }
}

void WaveformEditor::paintWaveform(juce::Graphics& graphics,
                                   const juce::Rectangle<float> bounds) const {
    if (cache_ == nullptr || cache_->levels().empty())
        return;
    const auto framesPerPixel =
        (visibleEnd_ - visibleStart_) / static_cast<double>(std::max(1.0F, bounds.getWidth()));
    const auto level = std::find_if(cache_->levels().rbegin(), cache_->levels().rend(),
                                    [framesPerPixel](const auto& candidate) {
                                        return static_cast<double>(candidate.framesPerPeak) <=
                                               framesPerPixel * 2.0;
                                    });
    const auto& peaks = level == cache_->levels().rend() ? cache_->levels().front() : *level;
    const auto channels = std::max(1U, peaks.channelCount);
    graphics.setColour(juce::Colour{editorWave});
    for (std::uint32_t channel = 0U; channel < channels; ++channel) {
        const auto laneHeight = bounds.getHeight() / static_cast<float>(channels);
        const auto centre = bounds.getY() + (static_cast<float>(channel) + 0.5F) * laneHeight;
        for (int pixel = 0; pixel < static_cast<int>(bounds.getWidth()); ++pixel) {
            const auto frame = xToFrame(bounds.getX() + static_cast<float>(pixel));
            const auto block = static_cast<std::size_t>(frame / peaks.framesPerPeak);
            const auto* peak = peaks.peak(std::min(block, peaks.peakBlockCount() - 1U), channel);
            if (peak == nullptr)
                continue;
            const auto top = centre - std::clamp(peak->maximum, -1.0F, 1.0F) * laneHeight * 0.43F;
            const auto bottom =
                centre - std::clamp(peak->minimum, -1.0F, 1.0F) * laneHeight * 0.43F;
            graphics.drawVerticalLine(static_cast<int>(bounds.getX()) + pixel, top, bottom);
        }
    }
}

void WaveformEditor::paintMarker(juce::Graphics& graphics, const Marker marker,
                                 const juce::Colour colour, const juce::String& name) const {
    const auto x = static_cast<float>(frameToX(markerFrame(marker)));
    if (x < 0.0F || x > static_cast<float>(getWidth()))
        return;
    graphics.setColour(colour);
    graphics.drawVerticalLine(static_cast<int>(std::round(x)), 4.0F,
                              static_cast<float>(getHeight() - 4));
    graphics.fillTriangle(x - 4.0F, 3.0F, x + 4.0F, 3.0F, x, 9.0F);
    if (hasSelectedMarker_ && selectedMarker_ == marker) {
        graphics.setFont(11.0F);
        graphics.drawText(name + " " + juce::String{markerFrame(marker)},
                          juce::Rectangle<int>{static_cast<int>(x) - 48, 8, 96, 16},
                          juce::Justification::centred);
    }
}

void WaveformEditor::paint(juce::Graphics& graphics) {
    auto bounds = getLocalBounds().toFloat();
    graphics.fillAll(juce::Colour{editorBackground});
    if (frameCount_ != 0U) {
        const auto trimStart = static_cast<float>(frameToX(playback_.startFrame));
        const auto trimEnd = static_cast<float>(frameToX(playback_.endFrame));
        graphics.setColour(juce::Colour{editorTrim}.withAlpha(0.08F));
        graphics.fillRect(juce::Rectangle<float>::leftTopRightBottom(
            trimStart, 1.0F, trimEnd, static_cast<float>(getHeight() - 1)));
        paintWaveform(graphics, bounds.reduced(1.0F));
        graphics.setColour(juce::Colours::black.withAlpha(0.48F));
        graphics.fillRect(bounds.withRight(std::max(bounds.getX(), trimStart)));
        graphics.fillRect(bounds.withLeft(std::min(bounds.getRight(), trimEnd)));
        paintMarker(graphics, Marker::trimStart, juce::Colour{editorTrim}, "Start");
        paintMarker(graphics, Marker::trimEnd, juce::Colour{editorTrim}, "End");
        paintMarker(graphics, Marker::loopStart, juce::Colour{editorLoop}, "Loop in");
        paintMarker(graphics, Marker::loopEnd, juce::Colour{editorLoop}, "Loop out");
    }

    graphics.setColour(juce::Colour{editorText});
    graphics.setFont(12.0F);
    if (sourceMissing_)
        graphics.drawFittedText("Source file is missing", getLocalBounds().reduced(12),
                                juce::Justification::centred, 1);
    else if (analysisPending_)
        graphics.drawFittedText("Analysing waveform…", getLocalBounds().reduced(12),
                                juce::Justification::centred, 1);
    else if (frameCount_ == 0U)
        graphics.drawFittedText("Load a sample to edit", getLocalBounds().reduced(12),
                                juce::Justification::centred, 1);
    else if (cache_ == nullptr)
        graphics.drawFittedText("Waveform unavailable", getLocalBounds().reduced(12),
                                juce::Justification::centred, 1);

    graphics.setColour(juce::Colour{editorBorder});
    graphics.drawRoundedRectangle(bounds.reduced(0.5F), 5.0F, 1.0F);
    if (hasKeyboardFocus(true)) {
        graphics.setColour(juce::Colour{editorTrim});
        graphics.drawRoundedRectangle(bounds.reduced(1.5F), 4.0F, 1.5F);
    }
    if (sampleRate_ > 0.0 && frameCount_ > 0U) {
        const auto text = juce::String{visibleStart_ / sampleRate_, 3} + "s — " +
                          juce::String{visibleEnd_ / sampleRate_, 3} + "s";
        graphics.setColour(juce::Colour{editorText}.withAlpha(0.65F));
        graphics.setFont(10.0F);
        graphics.drawText(text, getLocalBounds().removeFromBottom(15).reduced(4, 0),
                          juce::Justification::bottomRight);
    }
}
} // namespace padflow
