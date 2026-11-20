#include "WaveformView.h"

#include <algorithm>
#include <iostream>

namespace
{

class SilentTrack final : public juce::AudioFormatReader
{
public:
    SilentTrack() : AudioFormatReader (nullptr, "Test track")
    {
        sampleRate = 10000.0;
        lengthInSamples = 100000;
        numChannels = 1;
    }

    bool readSamples (int* const* channels, int numDestChannels, int offset, juce::int64, int frames) override
    {
        for (int channel = 0; channel < numDestChannels; ++channel)
            if (auto* samples = channels[channel])
                std::fill_n (samples + offset, frames, 0);

        return true;
    }
};

} // namespace

int runWaveformViewTests()
{
    const juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioFormatManager formats;
    std::vector<cratesnip::Region> regions { { { 10000, 30000 }, {}, {} },
                                           { { 50000, 70000 }, {}, {} },
                                           { { 80000, 100000 }, {}, {} } };
    cratesnip::WaveformView waveform (formats, regions);
    waveform.setSize (400, 200);


    int selected = 0;
    cratesnip::FrameRange created;
    waveform.onRegionSelected = [&] (int index)
    {
        selected = index;
        waveform.setActiveRegion (index);
    };
    waveform.onRegionResized = [&] (int index, cratesnip::FrameRange range)
    {
        regions[static_cast<size_t> (index)].frames = range;
    };
    waveform.onRegionCreated = [&] (cratesnip::FrameRange range) { created = range; };

    auto& input = static_cast<juce::Component&> (waveform);
    const auto gesture = [&] (float down, float moved, float up, bool shift = false)
    {
        const auto makeEvent = [&] (float x, bool held)
        {
            const auto mods = (held ? juce::ModifierKeys::leftButtonModifier : 0)
                            | (shift ? juce::ModifierKeys::shiftModifier : 0);
            const auto time = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { x, 20.0f },
                                     juce::ModifierKeys (mods), 0, 0, 0, 0, 0, &input, &input, time,
                                     { down, 20.0f }, time, 1, x != down);
        };
        input.mouseDown (makeEvent (down, true));
        input.mouseDrag (makeEvent (moved, true));
        input.mouseUp (makeEvent (up, false));
    };

    int failures = 0;
    const auto check = [&] (bool passed, const char* label)
    {
        if (! passed)
        {
            ++failures;
            std::cerr << "FAIL: " << label << '\n';
        }
    };

    const juce::ProgressBar* indicator = nullptr;
    for (auto* child : waveform.getChildren())
        if (auto* progress = dynamic_cast<const juce::ProgressBar*> (child))
            indicator = progress;

    waveform.setLoading (true);
    check (indicator != nullptr && indicator->isVisible() && ! waveform.hasTrack(),
           "opening indicator is visible while the waveform is unavailable");
    gesture (160.0f, 180.0f, 180.0f);
    check (created.isEmpty(), "region creation waits for the track");

    waveform.setOpenError ("File not found.");
    check (indicator != nullptr && ! indicator->isVisible() && ! waveform.hasTrack(),
           "an open error ends loading without enabling the waveform");

    waveform.setLoading (true);
    waveform.setTrack (std::make_unique<SilentTrack>(), std::nullopt);
    waveform.setActiveRegion (0);
    check (indicator != nullptr && ! indicator->isVisible() && waveform.hasTrack(),
           "an opened reader replaces the loading indicator");

    gesture (203.0f, 223.0f, 223.0f);
    check (selected == 1 && regions[1].frames == cratesnip::FrameRange (55000, 70000) && created.isEmpty(),
           "dragging a previously unselected start selects and resizes its region without jumping");
    check (regions[0].frames == cratesnip::FrameRange (10000, 30000), "resizing preserves the other region");

    gesture (277.0f, 277.0f, 257.0f);
    check (regions[1].frames == cratesnip::FrameRange (55000, 65000), "the end uses the final mouse position");

    gesture (223.0f, 223.0f, 223.0f);
    check (regions[1].frames == cratesnip::FrameRange (55000, 65000), "clicking near an edge preserves its frame");

    gesture (220.0f, 300.0f, 300.0f);
    check (regions[1].frames == cratesnip::FrameRange (64999, 65000), "the start stops one frame before the end");

    gesture (399.0f, 379.0f, 379.0f);
    check (selected == 2 && regions[2].frames == cratesnip::FrameRange (80000, 95000),
           "the end at the right side of the waveform is draggable");

    gesture (380.0f, 370.0f, 340.0f, true);
    check (created == cratesnip::FrameRange (85000, 95000)
               && regions[2].frames == cratesnip::FrameRange (80000, 95000),
           "Shift-drag from an edge creates a new region");

    return failures;
}
