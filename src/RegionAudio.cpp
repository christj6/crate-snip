#include "RegionAudio.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>

namespace cratesnip
{

std::unique_ptr<juce::AudioFormatReader> makeRegionReader (std::unique_ptr<juce::AudioFormatReader> reader,
                                                         FrameRange region)
{
    if (reader == nullptr || region.isEmpty() || region.getStart() < 0
        || region.getEnd() > reader->lengthInSamples)
        return {};

    auto section = std::make_unique<juce::AudioSubsectionReader> (reader.get(), region.getStart(),
                                                                region.getLength(), true);
    reader.release();
    return section;
}

ExportResult exportRegion (std::unique_ptr<juce::AudioFormatReader> reader, FrameRange region,
                           const juce::File& destination, const std::atomic_bool* cancelled)
{
    const auto layout = reader != nullptr ? reader->getChannelLayout() : juce::AudioChannelSet();
    auto section = makeRegionReader (std::move (reader), region);
    if (section == nullptr)
        return { {}, "Could not read the selected region." };

    const auto target = destination.withFileExtension (".wav").getNonexistentSibling();
    juce::TemporaryFile temporary (target);
    auto fileStream = temporary.getFile().createOutputStream();
    if (fileStream == nullptr)
        return { {}, "Could not write to the selected folder." };

    auto* const stream = fileStream.get();
    std::unique_ptr<juce::OutputStream> output = std::move (fileStream);
    auto options = juce::AudioFormatWriterOptions()
                       .withSampleRate (section->sampleRate)
                       .withNumChannels (static_cast<int> (section->numChannels))
                       .withBitsPerSample (24);
    if (layout.size() == static_cast<int> (section->numChannels))
        options = options.withChannelLayout (layout);

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (output, options);
    if (writer == nullptr)
        return { {}, "Could not create a WAV with the source audio format." };

    constexpr juce::int64 blockSize = 65536;
    for (juce::int64 offset = 0; offset < section->lengthInSamples;)
    {
        if (cancelled != nullptr && cancelled->load())
            return { {}, "Export cancelled." };

        const auto frames = juce::jmin (blockSize, section->lengthInSamples - offset);
        if (! writer->writeFromAudioReader (*section, offset, frames))
            return { {}, "Could not finish writing the WAV." };

        offset += frames;
    }

    if (! writer->flush())
        return { {}, "Could not finish writing the WAV." };

    stream->flush();
    if (stream->getStatus().failed())
        return { {}, "Could not finish writing the WAV." };

    writer.reset();
    if (cancelled != nullptr && cancelled->load())
        return { {}, "Export cancelled." };

    // Move within the destination folder without replacing an existing file.
    const auto temporaryPath = temporary.getFile().getFullPathName();
    const auto targetPath = target.getFullPathName();
    if (! MoveFileW (temporaryPath.toWideCharPointer(), targetPath.toWideCharPointer()))
        return { {}, "Could not save the finished WAV. Try another filename or folder." };

    return { target, {} };
}

} // namespace cratesnip
