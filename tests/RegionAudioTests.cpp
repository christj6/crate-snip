#include "RegionAudio.h"

#include <cmath>
#include <iostream>

namespace
{
std::unique_ptr<juce::AudioFormatReader> openWav (const juce::File& file)
{
    juce::WavAudioFormat wav;
    auto stream = file.createInputStream();
    return stream != nullptr ? std::unique_ptr<juce::AudioFormatReader> (wav.createReaderFor (stream.release(), true))
                             : nullptr;
}

bool near (float actual, float expected)
{
    return std::abs (actual - expected) < 0.000001f;
}
} // namespace

int runRegionAudioTests()
{
    int failures = 0;
    const auto check = [&] (bool ok, const char* description)
    {
        if (! ok)
        {
            ++failures;
            std::cout << "FAIL: " << description << "\n";
        }
    };

    juce::TemporaryFile source (".wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> sink = source.getFile().createOutputStream();
    if (sink == nullptr)
    {
        check (false, "create region test fixture");
        return failures;
    }
    auto writer = wav.createWriterFor (sink, juce::AudioFormatWriterOptions()
                                                .withSampleRate (48000)
                                                .withNumChannels (2)
                                                .withBitsPerSample (24));
    juce::AudioBuffer<float> fixture (2, 12);
    for (int frame = 0; frame < fixture.getNumSamples(); ++frame)
    {
        const auto sample = static_cast<float> (frame + 1) / 16.0f;
        fixture.setSample (0, frame, sample);
        fixture.setSample (1, frame, -sample);
    }
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (fixture, 0, fixture.getNumSamples()))
    {
        check (false, "write region test fixture");
        return failures;
    }
    writer.reset();

    const cratesnip::FrameRange region { 2, 7 };
    auto section = cratesnip::makeRegionReader (openWav (source.getFile()), region);
    check (section != nullptr && section->lengthInSamples == 5, "preview range length");
    if (section == nullptr)
        return failures;

    juce::AudioFormatReaderSource preview (section.release(), true);
    juce::AudioBuffer<float> audition (2, 8);
    preview.getNextAudioBlock ({ &audition, 0, audition.getNumSamples() });

    juce::TemporaryFile occupied (".wav");
    check (occupied.getFile().replaceWithText ("existing file"), "write collision fixture");
    const auto exported = cratesnip::exportRegion (openWav (source.getFile()), region, occupied.getFile());
    check (exported.error.isEmpty() && exported.file != occupied.getFile(), "export selects a new filename on collision");
    check (occupied.getFile().loadFileAsString() == "existing file", "export preserves existing file");
    if (exported.error.isNotEmpty())
    {
        std::cout << exported.error << "\n";
        return failures;
    }

    auto readBack = openWav (exported.file);
    check (readBack != nullptr && readBack->lengthInSamples == 5 && readBack->numChannels == 2
           && readBack->sampleRate == 48000 && readBack->bitsPerSample == 24,
           "export keeps source rate and channels with exactly five 24-bit frames");
    juce::AudioBuffer<float> rendered (2, 5);
    if (readBack != nullptr && readBack->read (&rendered, 0, 5, 0, true, true))
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            for (int frame = 0; frame < 5; ++frame)
            {
                const auto expected = fixture.getSample (channel, frame + 2);
                check (near (expected, audition.getSample (channel, frame)), "preview starts and ends at selected frames");
                check (near (expected, rendered.getSample (channel, frame)), "export matches preview frames");
            }
            for (int frame = 5; frame < audition.getNumSamples(); ++frame)
                check (near (0, audition.getSample (channel, frame)), "preview is silent after the selected end");
        }
    }
    else
    {
        check (false, "read back exported frames");
    }
    readBack.reset();
    exported.file.deleteFile();

    juce::TemporaryFile cancelledTarget (".wav");
    const std::atomic_bool cancel { true };
    const auto cancelled = cratesnip::exportRegion (openWav (source.getFile()), region, cancelledTarget.getFile(), &cancel);
    check (cancelled.error.isNotEmpty() && ! cancelledTarget.getFile().exists(),
           "cancelled export leaves no destination file");
    return failures;
}
