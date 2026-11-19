#include "WaveformView.h"

#include <cmath>

namespace cratesnip
{

juce::String formatTime (double seconds)
{
    const auto milliseconds = static_cast<juce::int64> (std::llround (seconds * 1000.0));
    return juce::String (milliseconds / 60000) + ":"
           + juce::String (milliseconds / 1000 % 60).paddedLeft ('0', 2) + "."
           + juce::String (milliseconds % 1000).paddedLeft ('0', 3);
}

WaveformView::WaveformView (juce::AudioFormatManager& formats)
    : thumbnail (512, formats, cache)
{
    thumbnail.addChangeListener (this);
    scrollbar.setAutoHide (false);
    scrollbar.addListener (this);
    scrollbar.setTitle ("Waveform position");
    addAndMakeVisible (scrollbar);
    setTitle ("Track waveform");
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

WaveformView::~WaveformView()
{
    scrollbar.removeListener (this);
    thumbnail.removeChangeListener (this);
}

void WaveformView::setTrack (const juce::File& file, juce::int64 frames, double rate,
                            std::optional<double> playheadSeconds)
{
    totalFrames = frames;
    sampleRate = rate;
    if (playheadSeconds && std::isfinite (*playheadSeconds) && *playheadSeconds >= 0
        && *playheadSeconds <= static_cast<double> (frames) / rate)
        playheadFrame = static_cast<juce::int64> (std::llround (*playheadSeconds * rate));

    scrollbar.setRangeLimits (0, static_cast<double> (frames), juce::dontSendNotification);
    thumbnail.setSource (new juce::FileInputSource (file));
    if (hasPlayhead())
        showPlayhead();
    else
        fitTrack();
}

void WaveformView::setOpenError (juce::String error)
{
    openError = std::move (error);
    repaint();
}

void WaveformView::zoom (double factor) { zoomAt (factor, 0.5); }
void WaveformView::fitTrack()          { setView (0, static_cast<double> (totalFrames)); }

void WaveformView::showPlayhead()
{
    if (playheadFrame)
        setView (static_cast<double> (*playheadFrame) - sampleRate * 8.0, sampleRate * 16.0);
}

void WaveformView::setPreviewFrame (std::optional<juce::int64> frame)
{
    if (frame != previewFrame)
    {
        previewFrame = frame;
        repaint (plot());
    }
}

void WaveformView::setView (double start, double length)
{
    if (! hasTrack())
        return;

    const auto extent = static_cast<double> (totalFrames);
    viewLength = juce::jlimit (juce::jmin (extent, juce::jmax (1.0, sampleRate * 0.01)), extent, length);
    viewStart = juce::jlimit (0.0, extent - viewLength, start);
    scrollbar.setCurrentRange (viewStart, viewLength, juce::dontSendNotification);
    scrollbar.setSingleStepSize (juce::jmax (1.0, viewLength * 0.1));
    scrollbar.setEnabled (viewLength < extent);
    repaint();
}

void WaveformView::zoomAt (double factor, double fraction)
{
    if (! hasTrack())
        return;

    const auto extent = static_cast<double> (totalFrames);
    const auto minimum = juce::jmin (extent, juce::jmax (1.0, sampleRate * 0.01));
    const auto nextLength = juce::jlimit (minimum, extent, viewLength * factor);
    setView (viewStart + (viewLength - nextLength) * fraction, nextLength);
}

juce::Rectangle<int> WaveformView::plot() const
{
    return getLocalBounds().withTrimmedBottom (40);
}

float WaveformView::xForFrame (juce::int64 frame) const
{
    return static_cast<float> ((static_cast<double> (frame) - viewStart) / viewLength * plot().getWidth());
}

juce::int64 WaveformView::frameAtX (float x) const
{
    const auto fraction = juce::jlimit (0.0, 1.0, static_cast<double> (x) / juce::jmax (1, plot().getWidth()));
    return juce::jlimit (juce::int64 { 0 }, totalFrames,
                        static_cast<juce::int64> (std::llround (viewStart + fraction * viewLength)));
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto area = plot();
    g.setColour (juce::Colour (0xff11161d));
    g.fillRect (area);

    if (! hasTrack() || thumbnail.getTotalLength() <= 0)
    {
        g.setColour (juce::Colour (0xffa6b2c1));
        g.drawText (openError.isNotEmpty() ? openError : juce::String ("Reading waveform..."),
                    area, juce::Justification::centred);
        if (! hasTrack())
            return;
    }

    const auto right = viewStart + viewLength;
    if (thumbnail.getTotalLength() > 0)
    {
        g.setColour (juce::Colour (0xff73b4ab));
        thumbnail.drawChannels (g, area.reduced (0, 4), viewStart / sampleRate, right / sampleRate, 1.0f);
    }

    {
        const juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area);
        if (! selection.isEmpty())
        {
            const auto leftX = xForFrame (selection.getStart());
            const auto rightX = xForFrame (selection.getEnd());
            g.setColour (juce::Colour (0x5538a6ef));
            g.fillRect (juce::Rectangle<float> (leftX, 0, rightX - leftX, static_cast<float> (area.getHeight()))
                            .getIntersection (area.toFloat()));

            g.setColour (juce::Colour (0xff8ed4ff));
            for (const auto x : { leftX, rightX })
                if (x >= 0 && x <= static_cast<float> (area.getRight()))
                    g.fillRect (juce::jmin (x, static_cast<float> (area.getRight()) - 2.0f), 0.0f, 2.0f,
                                static_cast<float> (area.getHeight()));
        }

        const auto drawMarker = [&] (std::optional<juce::int64> frame, juce::Colour colour)
        {
            if (! frame)
                return;
            const auto x = xForFrame (*frame);
            if (x >= 0 && x <= static_cast<float> (area.getRight()))
            {
                g.setColour (colour);
                g.fillRect (juce::jmin (juce::roundToInt (x), area.getRight() - 2),
                            0, 2, area.getHeight());
            }
        };

        drawMarker (playheadFrame, juce::Colour (0xffffa34d));
        drawMarker (previewFrame, juce::Colours::white);
    }

    g.setColour (juce::Colour (0xffa6b2c1));
    g.setFont (12.0f);
    constexpr int divisions = 4;
    for (int i = 0; i <= divisions; ++i)
    {
        const auto x = area.getWidth() * i / divisions;
        const auto labelX = juce::jlimit (0, juce::jmax (0, area.getWidth() - 72), x - 36);
        g.drawText (formatTime ((viewStart + viewLength * i / divisions) / sampleRate),
                    labelX, area.getBottom() + 1, 72, 19,
                    i == 0 ? juce::Justification::centredLeft
                           : i == divisions ? juce::Justification::centredRight : juce::Justification::centred);
    }
}

void WaveformView::resized()
{
    scrollbar.setBounds (getLocalBounds().removeFromBottom (16));
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (! hasTrack() || ! e.mods.isLeftButtonDown() || ! plot().contains (e.getPosition()))
        return;

    grabKeyboardFocus();
    const auto frame = frameAtX (e.position.x);
    if (! selection.isEmpty())
    {
        const auto distanceToStart = std::abs (e.position.x - xForFrame (selection.getStart()));
        const auto distanceToEnd = std::abs (e.position.x - xForFrame (selection.getEnd()));
        if (juce::jmin (distanceToStart, distanceToEnd) <= 9.0f)
        {
            drag = distanceToStart <= distanceToEnd ? Drag::start : Drag::end;
            anchorFrame = drag == Drag::start ? selection.getEnd() : selection.getStart();
            return;
        }
    }

    drag = Drag::newRegion;
    anchorFrame = frame;
    setSelection ({ frame, frame });
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none)
        return;

    const auto frame = frameAtX (e.position.x);
    if (drag == Drag::start)
        setSelection ({ juce::jmin (frame, anchorFrame - 1), anchorFrame });
    else if (drag == Drag::end)
        setSelection ({ anchorFrame, juce::jmax (frame, anchorFrame + 1) });
    else
        setSelection ({ juce::jmin (frame, anchorFrame), juce::jmax (frame, anchorFrame) });
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! hasTrack())
        return;

    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const auto delta = wheel.deltaX != 0 ? wheel.deltaX : wheel.deltaY;
        setView (viewStart - viewLength * delta, viewLength);
        return;
    }

    const auto fraction = juce::jlimit (0.0, 1.0, static_cast<double> (e.position.x) / juce::jmax (1, plot().getWidth()));
    zoomAt (std::pow (2.0, -2.0 * wheel.deltaY), fraction);
}

void WaveformView::setSelection (FrameRange range)
{
    if (selection != range)
    {
        selection = range;
        repaint (plot());
        if (onSelectionChanged)
            onSelectionChanged();
    }
}

void WaveformView::changeListenerCallback (juce::ChangeBroadcaster*) { repaint (plot()); }
void WaveformView::scrollBarMoved (juce::ScrollBar*, double start)   { setView (start, viewLength); }

} // namespace cratesnip
