#include "EditorComponent.h"

#include <cmath>

namespace cratesnip
{

EditorComponent::EditorComponent (LaunchRequest launchRequest)
    : request (std::move (launchRequest))
{
    juce::Component* const components[] { &waveform, &trackTitle, &trackInfo, &selectionInfo, &status,
                                          &zoomIn, &zoomOut, &fit, &playhead, &preview, &exportWav,
                                          &pauseFoobar, &resumeFoobar };
    for (auto* component : components)
        addAndMakeVisible (component);

    trackTitle.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    trackInfo.setColour (juce::Label::textColourId, juce::Colour (0xffa6b2c1));
    status.setColour (juce::Label::textColourId, juce::Colour (0xffbac4cf));

    zoomIn.onClick = [this] { waveform.zoom (0.5); };
    zoomOut.onClick = [this] { waveform.zoom (2.0); };
    fit.onClick = [this] { waveform.fitTrack(); };
    playhead.onClick = [this] { waveform.showPlayhead(); };
    preview.onClick = [this] { togglePreview(); };
    exportWav.onClick = [this] { chooseExport(); };
    waveform.onSelectionChanged = [this] { selectionChanged(); };
    pauseFoobar.onClick = [this]
    {
        const auto error = foobar.pause();
        setStatus (error.isEmpty() ? juce::String ("Pause sent to foobar2000.") : error);
    };
    resumeFoobar.onClick = [this]
    {
        stopPreview();
        const auto error = foobar.resume();
        setStatus (error.isEmpty() ? juce::String ("Play sent to foobar2000.") : error);
    };

    const auto hasFoobar = foobar.getExecutable() != juce::File();
    pauseFoobar.setEnabled (hasFoobar);
    resumeFoobar.setEnabled (hasFoobar);
    formats.registerBasicFormats();
    openTrack();
    selectionChanged();
    setSize (960, 560);
}

EditorComponent::~EditorComponent()
{
    chooser.reset();
    cancelExport.store (true);
    exportThread.removeAllJobs (true, -1);

    stopTimer();
    deviceManager.removeAudioCallback (&player);
    player.setSource (nullptr);
    transport.setSource (nullptr);
    previewSource.reset();
    readAhead.stopThread (-1);
}

void EditorComponent::openTrack()
{
    trackTitle.setText (request.title.isNotEmpty() ? request.title : request.file.getFileNameWithoutExtension(),
                        juce::dontSendNotification);

    if (! request.file.existsAsFile())
    {
        waveform.setOpenError (request.file == juce::File() ? "Open a track from foobar2000." : "File not found.");
        return;
    }

    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (request.file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0
        || ! std::isfinite (reader->sampleRate) || reader->numChannels == 0)
    {
        waveform.setOpenError ("Could not decode this audio file.");
        return;
    }

    sourceRate = reader->sampleRate;
    juce::String info;
    if (request.artist.isNotEmpty())
        info << request.artist << "  |  ";
    info << reader->getFormatName() << "  |  " << sourceRate << " Hz  |  "
         << static_cast<int> (reader->numChannels) << " ch  |  "
         << formatTime (static_cast<double> (reader->lengthInSamples) / sourceRate);
    trackInfo.setText (info, juce::dontSendNotification);
    waveform.setTrack (request.file, reader->lengthInSamples, sourceRate, request.positionSeconds);
    if (! request.problems.isEmpty())
        setStatus (request.problems[0]);
}

void EditorComponent::selectionChanged()
{
    if (previewSource != nullptr)
        stopPreview();

    const auto region = waveform.getSelection();
    juce::String description;
    if (waveform.hasTrack())
    {
        if (region.isEmpty())
            description = "Drag across the waveform to select a region.";
        else
            description << formatTime (static_cast<double> (region.getStart()) / sourceRate) << " to "
                        << formatTime (static_cast<double> (region.getEnd()) / sourceRate) << "  |  "
                        << formatTime (static_cast<double> (region.getLength()) / sourceRate)
                        << "  |  " << region.getLength() << " frames";
    }
    selectionInfo.setText (description, juce::dontSendNotification);
    updateControls();
}

void EditorComponent::updateControls()
{
    const auto hasSelection = ! waveform.getSelection().isEmpty();
    zoomIn.setEnabled (waveform.hasTrack());
    zoomOut.setEnabled (waveform.hasTrack());
    fit.setEnabled (waveform.hasTrack());
    playhead.setEnabled (waveform.hasPlayhead());
    preview.setEnabled (hasSelection);
    preview.setButtonText (previewSource == nullptr ? "Preview" : "Stop preview");
    exportWav.setEnabled (hasSelection && ! choosing && ! exporting);
}

void EditorComponent::togglePreview()
{
    if (previewSource != nullptr)
    {
        stopPreview();
        return;
    }

    previewRange = waveform.getSelection();
    if (previewRange.isEmpty())
        return;

    auto reader = makeRegionReader (std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (request.file)),
                                    previewRange);
    if (reader == nullptr)
    {
        setStatus ("Could not read the selected region.");
        return;
    }

    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        const auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
        if (error.isNotEmpty() || deviceManager.getCurrentAudioDevice() == nullptr)
        {
            setStatus (error.isNotEmpty() ? error : juce::String ("No audio output device is available."));
            return;
        }

        player.setSource (&transport);
        deviceManager.addAudioCallback (&player);
    }

    if (! readAhead.isThreadRunning() && ! readAhead.startThread())
    {
        setStatus ("Could not start the preview reader.");
        return;
    }

    const auto readerRate = reader->sampleRate;
    previewSource = std::make_unique<juce::AudioFormatReaderSource> (reader.get(), true);
    reader.release();
    transport.setSource (previewSource.get(), 32768, &readAhead, readerRate);
    transport.start();
    waveform.setPreviewFrame (previewRange.getStart());
    startTimerHz (30);
    updateControls();
}

void EditorComponent::stopPreview()
{
    if (previewSource == nullptr)
        return;

    stopTimer();
    transport.stop();
    transport.setSource (nullptr);
    previewSource.reset();
    waveform.setPreviewFrame (std::nullopt);
    updateControls();
}

void EditorComponent::timerCallback()
{
    if (! transport.isPlaying() || deviceManager.getCurrentAudioDevice() == nullptr)
    {
        stopPreview();
        return;
    }

    const auto elapsed = static_cast<juce::int64> (std::llround (transport.getCurrentPosition() * sourceRate));
    waveform.setPreviewFrame (juce::jmin (previewRange.getEnd(), previewRange.getStart() + elapsed));
}

void EditorComponent::chooseExport()
{
    const auto region = waveform.getSelection();
    if (region.isEmpty() || choosing || exporting)
        return;

    auto stem = request.suggestedBaseName();
    if (stem.isEmpty())
        stem = "Sample";
    const auto suggested = request.file.getSiblingFile (stem + " - 01.wav").getNonexistentSibling();
    chooser = std::make_unique<juce::FileChooser> ("Export selected region", suggested, "*.wav", false, false, this);
    choosing = true;
    updateControls();

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<EditorComponent> (this), region] (const auto& dialog)
                          {
                              if (safe == nullptr)
                                  return;
                              safe->choosing = false;
                              const auto destination = dialog.getResult();
                              if (destination != juce::File())
                                  safe->beginExport (region, destination);
                              safe->updateControls();
                          });
}

void EditorComponent::beginExport (FrameRange region, const juce::File& destination)
{
    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (request.file));
    if (reader == nullptr)
    {
        setStatus ("Could not reopen the source audio file.");
        return;
    }

    exporting = true;
    setStatus ("Exporting WAV...");
    exportThread.addJob ([safe = juce::Component::SafePointer<EditorComponent> (this),
                          reader = std::move (reader), region, destination, cancelled = &cancelExport]() mutable
    {
        const auto result = exportRegion (std::move (reader), region, destination, cancelled);
        juce::MessageManager::callAsync ([safe, result]
        {
            if (safe != nullptr)
                safe->exportFinished (result);
        });
    });
}

void EditorComponent::exportFinished (const ExportResult& result)
{
    exporting = false;
    setStatus (result.error.isEmpty() ? "Saved " + result.file.getFullPathName() : result.error);
    updateControls();
}

void EditorComponent::setStatus (const juce::String& message)
{
    status.setText (message, juce::dontSendNotification);
}

void EditorComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff202831));
}

void EditorComponent::resized()
{
    auto area = getLocalBounds().reduced (16);
    trackTitle.setBounds (area.removeFromTop (34));
    trackInfo.setBounds (area.removeFromTop (24));
    area.removeFromTop (12);

    auto tools = area.removeFromTop (32);
    for (auto* button : { &zoomIn, &zoomOut, &fit, &playhead })
    {
        button->setBounds (tools.removeFromLeft (90));
        tools.removeFromLeft (8);
    }
    area.removeFromTop (10);

    status.setBounds (area.removeFromBottom (26));
    auto actions = area.removeFromBottom (36);
    for (auto* button : { &preview, &exportWav })
    {
        button->setBounds (actions.removeFromLeft (124));
        actions.removeFromLeft (8);
    }
    resumeFoobar.setBounds (actions.removeFromRight (130));
    actions.removeFromRight (8);
    pauseFoobar.setBounds (actions.removeFromRight (130));

    area.removeFromBottom (6);
    selectionInfo.setBounds (area.removeFromBottom (28));
    waveform.setBounds (area);
}

bool EditorComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::spaceKey && preview.isEnabled())
    {
        togglePreview();
        return true;
    }
    return false;
}

} // namespace cratesnip
