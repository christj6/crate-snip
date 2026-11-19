#pragma once

#include "RegionAudio.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <optional>

namespace cratesnip
{

juce::String formatTime (double seconds);

class WaveformView final : public juce::Component,
                           private juce::ChangeListener,
                           private juce::ScrollBar::Listener
{
public:
    WaveformView (juce::AudioFormatManager&, const std::vector<Region>&);
    ~WaveformView() override;

    void setTrack (const juce::File&, juce::int64 frames, double rate, std::optional<double> playheadSeconds);
    void setOpenError (juce::String);
    void zoom (double factor);
    void fitTrack();
    void showPlayhead();
    void setPreviewFrame (std::optional<juce::int64>);

    void setActiveRegion (int);
    void revealRegion (int);

    bool hasTrack() const noexcept { return totalFrames > 0; }
    bool hasPlayhead() const noexcept { return playheadFrame.has_value(); }

    std::function<void (int)> onRegionSelected;
    std::function<void (FrameRange)> onRegionCreated;
    std::function<void (int, FrameRange)> onRegionResized;

private:
    enum class Drag { none, newRegion, start, end };

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newStart) override;

    void setView (double start, double length);
    void zoomAt (double factor, double fraction);
    const Region* getRegion (int) const;
    float xForFrame (juce::int64 frame) const;
    juce::int64 frameAtX (float x) const;
    FrameRange regionFromDrag (float x) const;
    juce::Rectangle<int> plot() const;

    const std::vector<Region>& regions;
    juce::AudioThumbnailCache cache { 1 };
    juce::AudioThumbnail thumbnail;
    juce::ScrollBar scrollbar { false };
    juce::String openError;
    juce::int64 totalFrames = 0;
    double sampleRate = 0;
    double viewStart = 0;
    double viewLength = 0;
    std::optional<juce::int64> playheadFrame, previewFrame;
    int activeRegion = -1;
    int draggedRegion = -1;
    FrameRange draft;
    Drag drag = Drag::none;
    juce::int64 anchorFrame = 0;
    float anchorX = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};

} // namespace cratesnip
