#include "AacAudio.h"

#include <cmath>
#include <iostream>

namespace
{

juce::var probeFile (juce::AudioFormatManager& formats, const juce::File& file)
{
    juce::var record (new juce::DynamicObject);
    auto* row = record.getDynamicObject();
    row->setProperty ("file", file.getFileName());
    row->setProperty ("decoder", juce::String());

    const auto finish = [&] (const char* status, const juce::String& details = {})
    {
        row->setProperty ("status", juce::String (status));
        row->setProperty ("details", details);
        return record;
    };

    if (! file.existsAsFile())
        return finish ("Missing fixture");

    auto reader = cratesnip::openAudioReader (formats, file);
    if (reader == nullptr)
    {
        if (cratesnip::isAacInput (file))
            return finish ("Could not open", "AAC (Windows Media Foundation)");

        const auto* matched = formats.findFormatForFileExtension (file.getFileExtension());
        return matched == nullptr ? finish ("No decoder") : finish ("Could not open", matched->getFormatName());
    }

    row->setProperty ("decoder", reader->getFormatName());
    const auto rate = reader->sampleRate;
    const auto channels = static_cast<int> (reader->numChannels);
    const auto length = reader->lengthInSamples;
    if (! std::isfinite (rate) || rate <= 0 || channels <= 0 || length <= 0)
        return finish ("Could not open", "The decoder did not report a usable audio stream.");

    const auto start = length / 4;
    const auto count = static_cast<int> (juce::jmin (juce::int64 { 4096 }, length - start));
    const cratesnip::FrameRange cut { start, start + count };
    const auto previewChannels = juce::jmin (2, channels);
    juce::AudioBuffer<float> decoded (previewChannels, count);
    if (! reader->read (&decoded, 0, count, start, true, true))
        return finish ("Decode failed");

    const auto version = cratesnip::SourceFileAccess (file).version();
    const auto preview = cratesnip::openVerifiedPreview (formats, file, version, cut, false);
    if (preview.source == nullptr)
        return finish ("Preview failed", preview.error);

    juce::AudioBuffer<float> audition (previewChannels, count);
    preview.source->getNextAudioBlock ({ &audition, 0, count });

    juce::TemporaryFile output (".wav");
    const auto saved = cratesnip::exportRegion (
        cratesnip::openAudioReader (formats, file), cut, output.getFile());
    if (saved.error.isNotEmpty())
        return finish ("Export failed", saved.error);

    auto wav = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (saved.file));
    if (wav == nullptr || wav->lengthInSamples != count || wav->sampleRate != rate
        || wav->numChannels != static_cast<unsigned> (channels) || wav->bitsPerSample != 24)
        return finish ("Export failed", "The WAV did not preserve the region length, sample rate, or channel count.");

    juce::AudioBuffer<float> exported (previewChannels, count);
    if (! wav->read (&exported, 0, count, 0, true, true))
        return finish ("Export failed", "Could not read back the WAV.");

    constexpr float tolerance = 0.000002f;
    for (int channel = 0; channel < previewChannels; ++channel)
    {
        for (int frame = 0; frame < count; ++frame)
        {
            const auto sample = decoded.getSample (channel, frame);
            if (! (std::abs (audition.getSample (channel, frame) - sample) <= tolerance))
                return finish ("Preview failed", "The preview disagreed with the decoded frames.");

            if (! (std::abs (exported.getSample (channel, frame) - juce::jlimit (-1.0f, 1.0f, sample)) <= tolerance))
                return finish ("Export failed", "The exported frames disagreed with the preview.");
        }
    }

    return finish ("Pass");
}

} // namespace

int probeAudioFormats (const juce::StringArray& files)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    juce::Array<juce::var> results;
    for (const auto& path : files)
        results.add (probeFile (formats, juce::File::getCurrentWorkingDirectory().getChildFile (path)));

    std::cout << juce::JSON::toString (juce::var (results)) << '\n';
    return 0;
}

// For each file, reports the first frame louder than half scale. Run on encodings of one signal
// with a sudden onset, the frames should match the WAV's: a later frame means the decoder kept
// the encoder's start padding, which would shift the playhead and every cut.
int probeOnsets (const juce::StringArray& files)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    juce::Array<juce::var> results;
    for (const auto& path : files)
    {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (path);
        juce::var record (new juce::DynamicObject);
        auto* row = record.getDynamicObject();
        row->setProperty ("file", file.getFileName());

        if (auto reader = cratesnip::openAudioReader (formats, file))
        {
            row->setProperty ("decoder", reader->getFormatName());
            row->setProperty ("rate", reader->sampleRate);
            row->setProperty ("frames", reader->lengthInSamples);

            juce::int64 onset = -1;
            juce::AudioBuffer<float> block (static_cast<int> (reader->numChannels), 4096);
            for (juce::int64 start = 0; onset < 0 && start < reader->lengthInSamples; start += block.getNumSamples())
            {
                const auto count = static_cast<int> (juce::jmin (juce::int64 { block.getNumSamples() },
                                                                reader->lengthInSamples - start));
                if (! reader->read (&block, 0, count, start, true, true))
                    break;
                for (int frame = 0; frame < count && onset < 0; ++frame)
                    if (std::abs (block.getSample (0, frame)) > 0.5f)
                        onset = start + frame;
            }
            row->setProperty ("onset", onset);
        }
        else
        {
            row->setProperty ("decoder", "none");
        }
        results.add (record);
    }

    std::cout << juce::JSON::toString (juce::var (results)) << '\n';
    return 0;
}
