#include "WaveformView.h"

#include <cmath>
#include <utility>

namespace cratesnip
{

juce::String formatTime (double seconds)
{
    const auto milliseconds = static_cast<juce::int64> (std::llround (seconds * 1000.0));
    return juce::String (milliseconds / 60000) + ":"
           + juce::String (milliseconds / 1000 % 60).paddedLeft ('0', 2) + "."
           + juce::String (milliseconds % 1000).paddedLeft ('0', 3);
}

WaveformView::WaveformView (juce::AudioFormatManager& formats, const std::vector<Region>& trackRegions)
    : regions (trackRegions), thumbnail (512, formats, cache)
{
    thumbnail.addChangeListener (this);
    scrollbar.setAutoHide (false);
    scrollbar.addListener (this);
    scrollbar.setTitle ("Waveform position");
    addAndMakeVisible (scrollbar);
    setTitle ("Track waveform");
    setWantsKeyboardFocus (true);
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

double WaveformView::xForFrame (juce::int64 frame) const
{
    return (static_cast<double> (frame) - viewStart) / viewLength * plot().getWidth();
}

juce::int64 WaveformView::frameAtX (double x) const
{
    const auto fraction = juce::jlimit (0.0, 1.0, x / juce::jmax (1, plot().getWidth()));
    return juce::jlimit (juce::int64 { 0 }, totalFrames,
                        static_cast<juce::int64> (std::llround (viewStart + fraction * viewLength)));
}

std::optional<WaveformView::RegionEdge> WaveformView::edgeAtX (float x) const
{
    constexpr auto hitDistance = 9.0;
    const auto width = plot().getWidth();
    std::optional<RegionEdge> nearest;
    auto nearestDistance = hitDistance;

    for (int i = static_cast<int> (regions.size()) - 1; i >= 0; --i)
    {
        const auto range = regions[static_cast<size_t> (i)].frames;
        for (const auto side : { Drag::start, Drag::end })
        {
            const auto edgeX = xForFrame (side == Drag::start ? range.getStart() : range.getEnd());
            const auto distance = std::abs (x - edgeX);
            const auto winsTie = ! nearest || (i == activeRegion && nearest->region != i);
            if (edgeX >= 0 && edgeX <= width
                && (distance < nearestDistance || (distance == nearestDistance && winsTie)))
            {
                nearest = RegionEdge { i, side };
                nearestDistance = distance;
            }
        }
    }
    return nearest;
}

juce::MouseCursor WaveformView::getMouseCursor()
{
    const auto pointer = getMouseXYRelative();
    if (drag == Drag::start || drag == Drag::end
        || (drag == Drag::none && hasTrack() && plot().contains (pointer)
            && ! juce::ModifierKeys::getCurrentModifiers().isShiftDown()
            && edgeAtX (static_cast<float> (pointer.x))))
        return juce::MouseCursor::LeftRightResizeCursor;

    return juce::MouseCursor::CrosshairCursor;
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
        const auto drawRegion = [&] (FrameRange range, bool active)
        {
            const auto leftX = static_cast<float> (xForFrame (range.getStart()));
            const auto rightX = static_cast<float> (xForFrame (range.getEnd()));
            g.setColour (juce::Colour (active ? 0x6638a6ef : 0x3338a6ef));
            g.fillRect (juce::Rectangle<float> (leftX, 0, rightX - leftX, static_cast<float> (area.getHeight()))
                            .getIntersection (area.toFloat()));

            g.setColour (juce::Colour (active ? 0xff8ed4ff : 0xff547384));
            const auto width = active ? 2.0f : 1.0f;
            for (const auto x : { leftX, rightX })
                if (x >= 0 && x <= static_cast<float> (area.getRight()))
                {
                    g.fillRect (juce::jmin (x, static_cast<float> (area.getRight()) - width), 0.0f, width,
                                static_cast<float> (area.getHeight()));
                    g.fillRect (x - 4.0f, 0.0f, 8.0f, 10.0f);
                }
        };

        for (int i = 0; i < static_cast<int> (regions.size()); ++i)
            if (i != activeRegion)
                drawRegion (regions[static_cast<size_t> (i)].frames, false);
        if (const auto* active = getRegion (activeRegion))
            drawRegion (active->frames, true);
        if (! draft.isEmpty())
            drawRegion (draft, true);

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
    anchorX = e.position.x;
    if (! e.mods.isShiftDown())
    {
        if (const auto edge = edgeAtX (anchorX))
        {
            const auto range = regions[static_cast<size_t> (edge->region)].frames;
            drag = edge->side;
            draggedRegion = edge->region;
            anchorFrame = drag == Drag::start ? range.getEnd() : range.getStart();
            const auto movingFrame = drag == Drag::start ? range.getStart() : range.getEnd();
            edgeOffsetX = xForFrame (movingFrame) - anchorX;

            if (draggedRegion != activeRegion && onRegionSelected)
                onRegionSelected (draggedRegion);
            return;
        }
    }

    drag = Drag::newRegion;
    anchorFrame = frameAtX (anchorX);
    draft = {};
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::newRegion)
    {
        const auto range = regionFromDrag (e.position.x);
        if (range != draft)
        {
            draft = range;
            repaint (plot());
        }
    }
    else if (drag != Drag::none)
    {
        resizeDraggedRegion (e.position.x);
    }
}

void WaveformView::resizeDraggedRegion (float x)
{
    const auto frame = frameAtX (x + edgeOffsetX);
    const auto range = drag == Drag::start ? FrameRange { juce::jmin (frame, anchorFrame - 1), anchorFrame }
                                           : FrameRange { anchorFrame, juce::jmax (frame, anchorFrame + 1) };
    if (onRegionResized)
        onRegionResized (draggedRegion, range);
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::start || drag == Drag::end)
        resizeDraggedRegion (e.position.x);

    const auto previousDrag = std::exchange (drag, Drag::none);
    if (previousDrag != Drag::newRegion)
        return;

    if (! std::exchange (draft, FrameRange {}).isEmpty())
        repaint (plot());

    const auto completed = regionFromDrag (e.position.x);
    if (! completed.isEmpty())
    {
        if (onRegionCreated)
            onRegionCreated (completed);
        repaint (plot());
        return;
    }

    const auto frame = frameAtX (e.position.x);
    if (! onRegionSelected || ! plot().contains (e.getPosition()))
        return;
    if (const auto* active = getRegion (activeRegion); active != nullptr && active->frames.contains (frame))
        return;
    for (int i = static_cast<int> (regions.size()) - 1; i >= 0; --i)
    {
        if (regions[static_cast<size_t> (i)].frames.contains (frame))
        {
            onRegionSelected (i);
            return;
        }
    }
}

FrameRange WaveformView::regionFromDrag (float x) const
{
    constexpr float minimumDragPixels = 4.0f;
    if (std::abs (x - anchorX) < minimumDragPixels)
        return {};

    const auto frame = frameAtX (x);
    return { juce::jmin (frame, anchorFrame), juce::jmax (frame, anchorFrame) };
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

const Region* WaveformView::getRegion (int index) const
{
    return index >= 0 && static_cast<size_t> (index) < regions.size() ? &regions[static_cast<size_t> (index)] : nullptr;
}

void WaveformView::setActiveRegion (int index)
{
    activeRegion = index;
    repaint (plot());
}

void WaveformView::revealRegion (int index)
{
    if (const auto* region = getRegion (index))
    {
        const auto first = static_cast<double> (region->frames.getStart());
        const auto last = static_cast<double> (region->frames.getEnd());
        if (first < viewStart || last > viewStart + viewLength)
            setView (first - juce::jmax (0.0, viewLength - (last - first)) * 0.5, viewLength);
    }
}

void WaveformView::changeListenerCallback (juce::ChangeBroadcaster*) { repaint (plot()); }
void WaveformView::scrollBarMoved (juce::ScrollBar*, double start)   { setView (start, viewLength); }

} // namespace cratesnip
