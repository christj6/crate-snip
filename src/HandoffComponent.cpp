#include "HandoffComponent.h"

namespace cratesnip
{

namespace
{
    juce::String formatTime (double seconds)
    {
        const auto minutes = static_cast<int> (seconds / 60.0);
        return juce::String (minutes) + ":" + juce::String (seconds - minutes * 60.0, 3).paddedLeft ('0', 6);
    }
} // namespace

HandoffComponent::HandoffComponent (const juce::StringArray& rawArgs, LaunchRequest requestToShow)
    : request (std::move (requestToShow))
{
    formatManager.registerBasicFormats();

    if (request.file == juce::File())
    {
        openError = "No file was given";
    }
    else if (! request.file.existsAsFile())
    {
        openError = "File not found";
    }
    else if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formatManager.createReaderFor (request.file)))
    {
        lengthSeconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
        formatInfo << reader->getFormatName() << ", " << reader->sampleRate << " Hz, "
                   << static_cast<int> (reader->numChannels) << " ch, " << static_cast<int> (reader->bitsPerSample) << "-bit, "
                   << formatTime (lengthSeconds) << " (" << reader->lengthInSamples << " frames)";
        thumbnail.setSource (new juce::FileInputSource (request.file));
    }
    else
    {
        openError = "JUCE can't decode this format";
    }

    thumbnail.addChangeListener (this);

    details.setMultiLine (true);
    details.setReadOnly (true);
    details.setScrollbarsShown (true);
    details.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    details.setText (describe (rawArgs), false);
    addAndMakeVisible (details);

    pauseButton.onClick = [this] { showRemoteResult (foobar.pause(), "Sent /pause"); };
    resumeButton.onClick = [this] { showRemoteResult (foobar.resume(), "Sent /play"); };
    addAndMakeVisible (pauseButton);
    addAndMakeVisible (resumeButton);

    remoteStatus.setText (foobar.getExecutable() == juce::File()
                              ? "foobar2000.exe wasn't found"
                              : "Using " + foobar.getExecutable().getFullPathName(),
                          juce::dontSendNotification);
    addAndMakeVisible (remoteStatus);

    setSize (900, 600);
}

HandoffComponent::~HandoffComponent()
{
    thumbnail.removeChangeListener (this);
}

juce::String HandoffComponent::describe (const juce::StringArray& rawArgs) const
{
    juce::String text;

    text << "Raw arguments (" << rawArgs.size() << "):\n";

    for (int i = 0; i < rawArgs.size(); ++i)
        text << "  [" << i << "] " << rawArgs[i].quoted() << "\n";

    text << "\nFile:      " << (request.file == juce::File() ? juce::String ("(none)") : request.file.getFullPathName()) << "\n";

    text << "Decoding:  " << (openError.isNotEmpty() ? "FAILED - " + openError : formatInfo) << "\n";

    text << "Artist:    " << (request.artist.isEmpty() ? juce::String ("(missing)") : request.artist) << "\n";
    text << "Title:     " << (request.title.isEmpty() ? juce::String ("(missing)") : request.title) << "\n";
    text << "Position:  " << (request.positionSeconds.has_value()
                                   ? formatTime (*request.positionSeconds) + " (" + juce::String (*request.positionSeconds, 6) + " s)"
                                   : juce::String ("(none)"))
         << "\n";
    text << "Filename:  " << request.suggestedBaseName() << " - 01.wav\n";

    if (! request.problems.isEmpty())
    {
        text << "\nProblems:\n";

        for (const auto& problem : request.problems)
            text << "  - " << problem << "\n";
    }

    return text;
}

void HandoffComponent::showRemoteResult (const juce::String& error, const juce::String& action)
{
    remoteStatus.setText (error.isEmpty() ? action + " to " + foobar.getExecutable().getFullPathName() : error,
                          juce::dontSendNotification);
}

void HandoffComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint (waveformArea);
}

void HandoffComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::black);
    g.fillRect (waveformArea);

    if (openError.isNotEmpty() || thumbnail.getTotalLength() <= 0.0)
    {
        g.setColour (juce::Colours::grey);
        g.drawText (openError.isNotEmpty() ? openError : juce::String ("Reading..."), waveformArea, juce::Justification::centred);
        return;
    }

    g.setColour (juce::Colours::lightgreen);
    thumbnail.drawChannels (g, waveformArea, 0.0, lengthSeconds, 1.0f);

    if (request.positionSeconds.has_value() && lengthSeconds > 0.0)
    {
        const auto x = static_cast<float> (waveformArea.getX())
                     + static_cast<float> (*request.positionSeconds / lengthSeconds) * static_cast<float> (waveformArea.getWidth());

        g.setColour (juce::Colours::orange);
        g.drawVerticalLine (juce::roundToInt (x), static_cast<float> (waveformArea.getY()), static_cast<float> (waveformArea.getBottom()));
    }
}

void HandoffComponent::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto buttons = area.removeFromBottom (30);
    pauseButton.setBounds (buttons.removeFromLeft (160));
    buttons.removeFromLeft (8);
    resumeButton.setBounds (buttons.removeFromLeft (160));
    buttons.removeFromLeft (8);
    remoteStatus.setBounds (buttons);

    area.removeFromBottom (10);
    waveformArea = area.removeFromBottom (area.getHeight() / 2);
    area.removeFromBottom (10);
    details.setBounds (area);
}

} // namespace cratesnip
