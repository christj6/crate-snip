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

std::unique_ptr<juce::AudioFormatReaderSource> makePreviewSource (std::unique_ptr<juce::AudioFormatReader> reader,
                                                                  FrameRange region, bool looping)
{
    auto section = makeRegionReader (std::move (reader), region);
    if (section == nullptr)
        return {};

    auto preview = std::make_unique<juce::AudioFormatReaderSource> (section.get(), true);
    section.release();
    preview->setLooping (looping);
    return preview;
}

juce::String regionFilename (const Region& region)
{
    auto name = region.name.trim();
    if (name.endsWithIgnoreCase (".wav"))
        name = name.dropLastCharacters (4);

    auto stem = juce::File::createLegalFileName (name).trim().trimCharactersAtEnd (" .");
    if (stem.isEmpty())
        return region.suggestedFilename;

    const auto deviceName = stem.upToFirstOccurrenceOf (".", false, false).trimEnd().toUpperCase();
    const auto last = deviceName.getLastCharacter();
    const auto numberedDevice = deviceName.length() == 4
                              && (deviceName.startsWith ("COM") || deviceName.startsWith ("LPT"))
                              && ((last >= '1' && last <= '9') || last == 0xb9 || last == 0xb2 || last == 0xb3);
    if (numberedDevice || deviceName == "CON" || deviceName == "PRN" || deviceName == "AUX"
        || deviceName == "NUL" || deviceName == "CONIN$" || deviceName == "CONOUT$")
        stem = "_" + stem;

    return stem + ".wav";
}

ExportResult exportRegion (std::unique_ptr<juce::AudioFormatReader> reader, FrameRange region,
                           const juce::File& destination, const std::atomic_bool* cancelled)
{
    const auto layout = reader != nullptr ? reader->getChannelLayout() : juce::AudioChannelSet();
    auto section = makeRegionReader (std::move (reader), region);
    if (section == nullptr)
        return { {}, "Could not read the selected region." };

    const auto requested = destination.withFileExtension (".wav");
    auto target = requested.getNonexistentSibling();
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
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const auto targetPath = target.getFullPathName();
        if (MoveFileW (temporaryPath.toWideCharPointer(), targetPath.toWideCharPointer()))
            return { target, {} };

        const auto error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
            break;

        target = requested.getNonexistentSibling();
    }
    return { {}, "Could not save the finished WAV. Try another filename or folder." };
}

std::vector<ExportResult> exportRegions (const juce::File& source, const std::vector<Region>& regions,
                                         const juce::File& folder, const std::atomic_bool* cancelled)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::vector<ExportResult> results;
    results.reserve (regions.size());
    for (const auto& region : regions)
    {
        if (cancelled != nullptr && cancelled->load())
            break;

        auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (source));
        results.push_back (exportRegion (std::move (reader), region.frames,
                                         folder.getChildFile (regionFilename (region)), cancelled));
    }
    return results;
}

} // namespace cratesnip
