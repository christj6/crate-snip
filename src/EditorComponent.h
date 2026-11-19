#pragma once

#include "FoobarRemote.h"
#include "LaunchRequest.h"
#include "WaveformView.h"

namespace cratesnip
{

class EditorComponent final : public juce::Component,
                              private juce::Timer
{
public:
    explicit EditorComponent (LaunchRequest);
    ~EditorComponent() override;

private:
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void timerCallback() override;

    void openTrack();
    void selectionChanged();
    void updateControls();
    void togglePreview();
    void stopPreview();
    void chooseExport();
    void beginExport (FrameRange, const juce::File&);
    void exportFinished (const ExportResult&);
    void setStatus (const juce::String&);

    LaunchRequest request;
    juce::AudioFormatManager formats;
    WaveformView waveform { formats };
    double sourceRate = 0;

    juce::AudioDeviceManager deviceManager;
    juce::TimeSliceThread readAhead { "CrateSnip preview" };
    std::unique_ptr<juce::AudioFormatReaderSource> previewSource;
    juce::AudioTransportSource transport;
    juce::AudioSourcePlayer player;
    FrameRange previewRange;

    std::atomic_bool cancelExport { false };
    juce::ThreadPool exportThread { 1 };
    std::unique_ptr<juce::FileChooser> chooser;
    bool choosing = false;
    bool exporting = false;

    FoobarRemote foobar;
    juce::Label trackTitle, trackInfo, selectionInfo, status;
    juce::TextButton zoomIn { "Zoom in" }, zoomOut { "Zoom out" }, fit { "Fit track" }, playhead { "Playhead" };
    juce::TextButton preview { "Preview" }, exportWav { "Export WAV" };
    juce::TextButton pauseFoobar { "Pause foobar2000" }, resumeFoobar { "Resume foobar2000" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorComponent)
};

} // namespace cratesnip
