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
    explicit WaveformView (juce::AudioFormatManager&);
    ~WaveformView() override;

    void setTrack (const juce::File&, juce::int64 frames, double rate, std::optional<double> playheadSeconds);
    void setOpenError (juce::String);
    void zoom (double factor);
    void fitTrack();
    void showPlayhead();
    void setPreviewFrame (std::optional<juce::int64>);

    FrameRange getSelection() const noexcept { return selection; }
    bool hasTrack() const noexcept { return totalFrames > 0; }
    bool hasPlayhead() const noexcept { return playheadFrame.has_value(); }

    std::function<void()> onSelectionChanged;

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
    void setSelection (FrameRange);
    float xForFrame (juce::int64 frame) const;
    juce::int64 frameAtX (float x) const;
    juce::Rectangle<int> plot() const;

    juce::AudioThumbnailCache cache { 1 };
    juce::AudioThumbnail thumbnail;
    juce::ScrollBar scrollbar { false };
    juce::String openError;
    juce::int64 totalFrames = 0;
    double sampleRate = 0;
    double viewStart = 0;
    double viewLength = 0;
    std::optional<juce::int64> playheadFrame, previewFrame;
    FrameRange selection;
    Drag drag = Drag::none;
    juce::int64 anchorFrame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};

} // namespace cratesnip
