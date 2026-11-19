#include "EditorComponent.h"

#include <cmath>

namespace cratesnip
{
namespace
{

juce::PropertiesFile::Options exportSettingsOptions()
{
    static juce::InterProcessLock lock ("CrateSnip.ExportFolder");
    juce::PropertiesFile::Options options;
    options.applicationName = "CrateSnip";
    options.filenameSuffix = "settings";
    options.millisecondsBeforeSaving = -1;
    options.processLock = &lock;
    return options;
}

juce::File loadExportFolder()
{
    const juce::PropertiesFile settings { exportSettingsOptions() };
    const auto path = settings.getValue ("exportFolder");
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

bool saveExportFolder (const juce::File& folder)
{
    juce::PropertiesFile settings { exportSettingsOptions() };
    settings.setValue ("exportFolder", folder.getFullPathName());
    return settings.saveIfNeeded();
}

juce::String rangeText (FrameRange range, double sampleRate)
{
    return formatTime (static_cast<double> (range.getStart()) / sampleRate)
           + " to " + formatTime (static_cast<double> (range.getEnd()) / sampleRate);
}

} // namespace

EditorComponent::EditorComponent (LaunchRequest launchRequest)
    : request (std::move (launchRequest))
{
    juce::Component* const components[] { &waveform, &trackTitle, &trackInfo, &selectionInfo, &folderInfo,
                                          &status, &zoomIn, &zoomOut, &fit, &playhead, &preview,
                                          &loopPreview, &exportWav, &folderButton, &pauseFoobar, &resumeFoobar,
                                          &regionsTitle, &regionList, &filenameLabel, &filename, &remove };
    for (auto* component : components)
        addAndMakeVisible (component);

    trackTitle.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    trackInfo.setColour (juce::Label::textColourId, juce::Colour (0xffa6b2c1));
    status.setColour (juce::Label::textColourId, juce::Colour (0xffbac4cf));
    folderInfo.setColour (juce::Label::textColourId, juce::Colour (0xffa6b2c1));
    regionsTitle.setText ("Regions", juce::dontSendNotification);
    regionsTitle.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    filenameLabel.setText ("WAV filename", juce::dontSendNotification);
    filename.setTitle ("WAV filename");
    filename.setSelectAllWhenFocused (true);
    regionList.setTitle ("Regions");
    regionList.setModel (this);
    regionList.setRowHeight (49);
    regionList.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff161c24));
    filename.onTextChange = [this]
    {
        if (auto* active = getRegion (activeRegion))
        {
            active->name = filename.getText();
            regionList.repaintRow (activeRegion);
        }
    };
    remove.onClick = [this] { removeRegion(); };

    zoomIn.onClick = [this] { waveform.zoom (0.5); };
    zoomOut.onClick = [this] { waveform.zoom (2.0); };
    fit.onClick = [this] { waveform.fitTrack(); };
    playhead.onClick = [this] { waveform.showPlayhead(); };
    preview.onClick = [this] { togglePreview(); };
    loopPreview.onClick = [this]
    {
        if (transport.isPlaying())
            startPreview();
    };
    exportWav.onClick = [this] { exportAll(); };
    folderButton.onClick = [this] { chooseFolder (false); };
    waveform.onRegionCreated = [this] (FrameRange range) { addRegion (range); };
    waveform.onRegionSelected = [this] (int index) { selectRegion (index); };
    waveform.onRegionResized = [this] (int index, FrameRange range) { resizeRegion (index, range); };
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
    refreshFolder();
    selectRegion (-1);
    setSize (1040, 620);
}

EditorComponent::~EditorComponent()
{
    regionList.setModel (nullptr);
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

int EditorComponent::getNumRows()
{
    return static_cast<int> (regions.size());
}

Region* EditorComponent::getRegion (int index)
{
    return index >= 0 && index < getNumRows() ? &regions[static_cast<size_t> (index)] : nullptr;
}

juce::String EditorComponent::getNameForRow (int row)
{
    if (const auto* region = getRegion (row))
        return regionFilename (*region);
    return {};
}

void EditorComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto* region = getRegion (row);
    if (region == nullptr)
        return;

    g.fillAll (juce::Colour (selected ? 0xff274867 : 0xff161c24));
    g.setColour (juce::Colour (0xfff0f4f7));
    g.setFont (14.0f);
    g.drawText (regionFilename (*region), 10, 5, width - 20, 20, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffafbfce));
    g.setFont (12.0f);
    g.drawText (rangeText (region->frames, sourceRate), 10, 25, width - 20, 18, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff2c3744));
    g.fillRect (0, height - 1, width, 1);
}

void EditorComponent::selectedRowsChanged (int row)
{
    selectRegion (row);
    waveform.revealRegion (row);
}

void EditorComponent::deleteKeyPressed (int row)
{
    if (row == activeRegion)
        removeRegion();
}

void EditorComponent::addRegion (FrameRange range)
{
    const auto name = request.suggestedFilename (nextRegionNumber++);
    regions.push_back ({ range, name, name });
    regionList.updateContent();
    selectRegion (getNumRows() - 1);
}

void EditorComponent::selectRegion (int index)
{
    const auto* region = getRegion (index);
    const auto selected = region != nullptr ? index : -1;
    if (activeRegion != selected)
        stopPreview();
    activeRegion = selected;

    juce::SparseSet<int> rows;
    if (region != nullptr)
        rows.addRange ({ selected, selected + 1 });
    regionList.setSelectedRows (rows, juce::dontSendNotification);
    if (region != nullptr)
        regionList.scrollToEnsureRowIsOnscreen (selected);

    waveform.setActiveRegion (selected);
    filename.setText (region != nullptr ? region->name : juce::String(), false);
    updateSelectionInfo();
    updateControls();
}

void EditorComponent::resizeRegion (int index, FrameRange range)
{
    if (auto* region = getRegion (index); region != nullptr && region->frames != range)
    {
        if (index == activeRegion && ! loopPreview.getToggleState())
            stopPreview();
        region->frames = range;
        waveform.repaint();
        regionList.repaintRow (index);
        updateSelectionInfo();
    }
}

void EditorComponent::removeRegion()
{
    if (getRegion (activeRegion) == nullptr)
        return;

    stopPreview();
    const auto removedIndex = activeRegion;
    regions.erase (regions.begin() + removedIndex);
    regionList.updateContent();
    selectRegion (juce::jmin (removedIndex, getNumRows() - 1));
}

void EditorComponent::updateSelectionInfo()
{
    juce::String description;
    if (const auto* active = getRegion (activeRegion))
    {
        const auto range = active->frames;
        description << rangeText (range, sourceRate) << "  |  "
                    << formatTime (static_cast<double> (range.getLength()) / sourceRate)
                    << "  |  " << range.getLength() << " frames";
    }
    else if (waveform.hasTrack())
    {
        description = "Drag across the waveform to add a region.";
    }
    selectionInfo.setText (description, juce::dontSendNotification);
}

void EditorComponent::updateControls()
{
    const auto hasSelection = getRegion (activeRegion) != nullptr;
    zoomIn.setEnabled (waveform.hasTrack());
    zoomOut.setEnabled (waveform.hasTrack());
    fit.setEnabled (waveform.hasTrack());
    playhead.setEnabled (waveform.hasPlayhead());
    preview.setEnabled (hasSelection);
    loopPreview.setEnabled (waveform.hasTrack());
    preview.setButtonText (previewSource == nullptr ? "Preview" : "Stop preview");
    filename.setEnabled (hasSelection);
    remove.setEnabled (hasSelection);
    folderButton.setEnabled (! choosing && ! exporting);
    exportWav.setEnabled (! regions.empty() && ! choosing && ! exporting);
}

void EditorComponent::togglePreview()
{
    if (previewSource != nullptr)
        stopPreview();
    else
        startPreview();
}

void EditorComponent::startPreview()
{
    const auto* active = getRegion (activeRegion);
    if (active == nullptr)
        return;

    const auto bounds = active->frames;
    const auto currentFrame = previewSource != nullptr ? currentPreviewFrame() : bounds.getStart();
    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (request.file));
    auto nextSource = makePreviewSource (std::move (reader), bounds, loopPreview.getToggleState());
    if (nextSource == nullptr)
    {
        stopPreview();
        setStatus ("Could not read the selected region.");
        return;
    }

    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        const auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
        if (error.isNotEmpty() || deviceManager.getCurrentAudioDevice() == nullptr)
        {
            stopPreview();
            setStatus (error.isNotEmpty() ? error : juce::String ("No audio output device is available."));
            return;
        }

        player.setSource (&transport);
        deviceManager.addAudioCallback (&player);
    }

    if (! readAhead.isThreadRunning() && ! readAhead.startThread())
    {
        stopPreview();
        setStatus ("Could not start the preview reader.");
        return;
    }

    const auto readerRate = nextSource->getAudioFormatReader()->sampleRate;
    transport.setSource (nextSource.get(), 32768, &readAhead, readerRate);
    previewSource = std::move (nextSource);
    previewRange = bounds;

    const auto startFrame = bounds.contains (currentFrame) ? currentFrame : bounds.getStart();
    if (startFrame > bounds.getStart())
        transport.setPosition (static_cast<double> (startFrame - bounds.getStart()) / readerRate);

    transport.start();
    previewStartedAtMs = juce::Time::getMillisecondCounterHiRes();
    waveform.setPreviewFrame (startFrame);
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

    // Mouse drags can update the region faster than the buffered source can be rebuilt.
    constexpr auto rangeUpdateIntervalMs = 120.0;
    const auto* active = getRegion (activeRegion);
    if (loopPreview.getToggleState() && active != nullptr && active->frames != previewRange
        && juce::Time::getMillisecondCounterHiRes() - previewStartedAtMs >= rangeUpdateIntervalMs)
    {
        startPreview();
        return;
    }

    waveform.setPreviewFrame (currentPreviewFrame());
}

juce::int64 EditorComponent::currentPreviewFrame() const
{
    const auto rate = previewSource->getAudioFormatReader()->sampleRate;
    const auto elapsed = static_cast<juce::int64> (std::llround (transport.getCurrentPosition() * rate));
    return juce::jlimit (previewRange.getStart(), previewRange.getEnd() - 1, previewRange.getStart() + elapsed);
}

void EditorComponent::refreshFolder()
{
    if (! useSessionFolder)
        exportFolder = loadExportFolder();

    juce::String description = "No export folder selected";
    if (exportFolder != juce::File())
    {
        description = "Export folder  " + exportFolder.getFullPathName();
        if (! exportFolder.isDirectory())
            description += " (unavailable)";
        else if (useSessionFolder)
            description += " (this window)";
    }
    folderInfo.setText (description, juce::dontSendNotification);
    folderInfo.setTooltip (description);
}

void EditorComponent::exportAll()
{
    if (regions.empty() || choosing || exporting)
        return;

    refreshFolder();
    if (exportFolder.isDirectory())
        beginExport();
    else
        chooseFolder (true);
}

void EditorComponent::chooseFolder (bool exportAfterChoosing)
{
    if (choosing || exporting)
        return;

    refreshFolder();
    const auto initialFolder = exportFolder.isDirectory() ? exportFolder : request.file.getParentDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Choose export folder", initialFolder, "*", false, false, this);
    choosing = true;
    updateControls();

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<EditorComponent> (this), exportAfterChoosing]
                          (const auto& dialog)
                          {
                              if (safe == nullptr)
                                  return;

                              safe->choosing = false;
                              const auto folder = dialog.getResult();
                              if (folder.isDirectory())
                              {
                                  safe->exportFolder = folder;
                                  safe->useSessionFolder = ! saveExportFolder (folder);
                                  safe->refreshFolder();
                                  safe->setStatus ({});
                                  if (exportAfterChoosing)
                                      safe->beginExport();
                              }
                              safe->updateControls();
                          });
}

void EditorComponent::beginExport()
{
    if (regions.empty())
        return;

    exporting = true;
    updateControls();
    setStatus ("Exporting " + juce::String (getNumRows()) + (getNumRows() == 1 ? " region..." : " regions..."));
    exportThread.addJob ([safe = juce::Component::SafePointer<EditorComponent> (this), source = request.file,
                          cuts = regions, folder = exportFolder, cancelled = &cancelExport]() mutable
    {
        auto results = exportRegions (source, cuts, folder, cancelled);
        juce::MessageManager::callAsync ([safe, cuts = std::move (cuts), results = std::move (results)]
        {
            if (safe != nullptr)
                safe->exportFinished (cuts, results);
        });
    });
}

void EditorComponent::exportFinished (const std::vector<Region>& cuts, const std::vector<ExportResult>& results)
{
    exporting = false;
    int saved = 0;
    juce::StringArray errors;
    for (size_t i = 0; i < results.size(); ++i)
    {
        if (results[i].error.isEmpty())
            ++saved;
        else
            errors.add (regionFilename (cuts[i]) + " - " + results[i].error);
    }

    const auto summary = "Exported " + juce::String (saved) + " of " + juce::String (cuts.size()) + " WAV files.";
    setStatus (summary);
    updateControls();
    if (! errors.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                               "Some regions could not be exported",
                                               summary + "\n\n" + errors.joinIntoString ("\n"));
}

void EditorComponent::setStatus (const juce::String& message)
{
    status.setText (message, juce::dontSendNotification);
    status.setTooltip (message);
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

    status.setBounds (area.removeFromBottom (24));
    auto footer = area.removeFromBottom (28);
    resumeFoobar.setBounds (footer.removeFromRight (130));
    footer.removeFromRight (8);
    pauseFoobar.setBounds (footer.removeFromRight (130));
    footer.removeFromRight (8);
    folderInfo.setBounds (footer);
    area.removeFromBottom (4);

    auto actions = area.removeFromBottom (36);
    preview.setBounds (actions.removeFromLeft (116));
    actions.removeFromLeft (8);
    loopPreview.setBounds (actions.removeFromLeft (132));
    actions.removeFromLeft (8);
    exportWav.setBounds (actions.removeFromLeft (116));
    actions.removeFromLeft (8);
    folderButton.setBounds (actions.removeFromLeft (116));
    area.removeFromBottom (10);

    auto side = area.removeFromRight (260);
    area.removeFromRight (14);
    regionsTitle.setBounds (side.removeFromTop (26));
    auto nameControls = side.removeFromBottom (91);
    filenameLabel.setBounds (nameControls.removeFromTop (23));
    filename.setBounds (nameControls.removeFromTop (29));
    nameControls.removeFromTop (7);
    remove.setBounds (nameControls.removeFromLeft (116));
    regionList.setBounds (side);

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
