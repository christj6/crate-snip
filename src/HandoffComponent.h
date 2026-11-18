#pragma once

#include "FoobarRemote.h"
#include "LaunchRequest.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace cratesnip
{

/** Step 1's window: shows exactly what the launch handed over, whether the file decodes, and its
    waveform with the requested position marked, plus buttons to pause and resume foobar2000.
*/
class HandoffComponent final : public juce::Component,
                               private juce::ChangeListener
{
public:
    HandoffComponent (const juce::StringArray& rawArgs, LaunchRequest requestToShow);
    ~HandoffComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    juce::String describe (const juce::StringArray& rawArgs) const;
    void showRemoteResult (const juce::String& error, const juce::String& action);

    LaunchRequest request;

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 1 };
    juce::AudioThumbnail thumbnail { 512, formatManager, thumbnailCache };
    double lengthSeconds = 0.0;
    juce::String openError;
    juce::String formatInfo;

    FoobarRemote foobar;

    juce::TextEditor details;
    juce::TextButton pauseButton { "Pause foobar2000" };
    juce::TextButton resumeButton { "Resume foobar2000" };
    juce::Label remoteStatus;
    juce::Rectangle<int> waveformArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HandoffComponent)
};

} // namespace cratesnip
