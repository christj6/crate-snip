#pragma once

#include "FoobarRemote.h"
#include "LaunchRequest.h"
#include "WaveformView.h"

namespace cratesnip
{

class EditorComponent final : public juce::Component,
                              private juce::Timer,
                              private juce::ListBoxModel
{
public:
    explicit EditorComponent (LaunchRequest);
    ~EditorComponent() override;

private:
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void timerCallback() override;
    int getNumRows() override;
    juce::String getNameForRow (int) override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged (int) override;
    void deleteKeyPressed (int) override;

    void openTrack();
    Region* getRegion (int);
    void addRegion (FrameRange);
    void selectRegion (int);
    void resizeRegion (int, FrameRange);
    void removeRegion();
    void updateSelectionInfo();
    void updateControls();
    void togglePreview();
    void startPreview();
    void stopPreview();
    juce::int64 currentPreviewFrame() const;
    void exportAll();
    void chooseFolder (bool exportAfterChoosing);
    void refreshFolder();
    void beginExport();
    void exportFinished (const std::vector<Region>&, const std::vector<ExportResult>&);
    void setStatus (const juce::String&);

    LaunchRequest request;
    juce::AudioFormatManager formats;
    std::vector<Region> regions;
    WaveformView waveform { formats, regions };
    int activeRegion = -1;
    unsigned nextRegionNumber = 1;
    double sourceRate = 0;

    juce::AudioDeviceManager deviceManager;
    juce::TimeSliceThread readAhead { "CrateSnip preview" };
    std::unique_ptr<juce::AudioFormatReaderSource> previewSource;
    juce::AudioTransportSource transport;
    juce::AudioSourcePlayer player;
    FrameRange previewRange;
    double previewStartedAtMs = 0;

    std::atomic_bool cancelExport { false };
    juce::ThreadPool exportThread { 1 };
    std::unique_ptr<juce::FileChooser> chooser;
    bool choosing = false;
    bool exporting = false;
    bool useSessionFolder = false;
    juce::File exportFolder;

    FoobarRemote foobar;
    juce::Label trackTitle, trackInfo, selectionInfo, folderInfo, status;
    juce::Label regionsTitle, filenameLabel;
    juce::ListBox regionList { "Regions" };
    juce::TextEditor filename;
    juce::TextButton remove { "Remove region" };
    juce::TextButton zoomIn { "Zoom in" }, zoomOut { "Zoom out" }, fit { "Fit track" }, playhead { "Playhead" };
    juce::TextButton preview { "Preview" }, exportWav { "Export All" }, folderButton { "Export folder..." };
    juce::ToggleButton loopPreview { "Loop preview" };
    juce::TextButton pauseFoobar { "Pause foobar2000" }, resumeFoobar { "Resume foobar2000" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorComponent)
};

} // namespace cratesnip
