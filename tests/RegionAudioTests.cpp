#include "RegionAudio.h"

#include <algorithm>
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
    check (! cratesnip::isSupportedChannelCount (0), "zero channels are rejected");
    check (cratesnip::isSupportedChannelCount (cratesnip::maxSupportedChannels), "the channel limit is inclusive");
    auto oversizedReader = openWav (source.getFile());
    auto oversizedPreviewReader = openWav (source.getFile());
    if (oversizedReader != nullptr && oversizedPreviewReader != nullptr)
    {
        oversizedReader->numChannels = cratesnip::maxSupportedChannels + 1;
        oversizedPreviewReader->numChannels = oversizedReader->numChannels;
        juce::TemporaryFile rejected (".wav");
        const auto result = cratesnip::exportRegion (std::move (oversizedReader), region, rejected.getFile());
        check (result.error == cratesnip::unsupportedChannelCount && ! rejected.getFile().exists(),
               "oversized channel count is rejected before the writer opens");
        check (cratesnip::makePreviewSource (std::move (oversizedPreviewReader), region, false) == nullptr,
               "oversized channel count is rejected before preview");
    }
    else
    {
        check (false, "open channel validation fixtures");
    }

    auto preview = cratesnip::makePreviewSource (openWav (source.getFile()), region, false);
    check (preview != nullptr && preview->getTotalLength() == 5, "preview range length");
    if (preview == nullptr)
        return failures;

    juce::AudioBuffer<float> audition (2, 8);
    preview->getNextAudioBlock ({ &audition, 0, audition.getNumSamples() });

    auto loop = cratesnip::makePreviewSource (openWav (source.getFile()), region, true);
    check (loop != nullptr && loop->isLooping(), "looping preview enabled");
    if (loop != nullptr)
    {
        juce::AudioBuffer<float> repeated (2, 13);
        loop->getNextAudioBlock ({ &repeated, 0, 6 });
        loop->getNextAudioBlock ({ &repeated, 6, 7 });
        for (int channel = 0; channel < repeated.getNumChannels(); ++channel)
            for (int frame = 0; frame < repeated.getNumSamples(); ++frame)
                check (near (fixture.getSample (channel, 2 + frame % 5), repeated.getSample (channel, frame)),
                       "loop wraps at selected frames across audio blocks");
        check (loop->getNextReadPosition() == 3, "loop position remains within the region");
    }

    auto resizedLoop = cratesnip::makePreviewSource (openWav (source.getFile()), { 4, 7 }, true);
    check (resizedLoop != nullptr, "preview accepts adjusted loop bounds");
    if (resizedLoop != nullptr)
    {
        juce::AudioBuffer<float> repeated (2, 7);
        resizedLoop->setNextReadPosition (1);
        resizedLoop->getNextAudioBlock ({ &repeated, 0, repeated.getNumSamples() });
        for (int frame = 0; frame < repeated.getNumSamples(); ++frame)
            check (near (fixture.getSample (0, 4 + (frame + 1) % 3), repeated.getSample (0, frame)),
                   "updated loop repeats only within the new bounds");
    }

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
    juce::TemporaryFile batchDestination (".batch");
    const auto folder = batchDestination.getFile();
    if (folder.createDirectory().failed())
    {
        check (false, "create batch export folder");
        return failures;
    }

    const std::vector<cratesnip::Region> cuts {
        { { 0, 3 }, "Artist - Title - 01.wav", "Shared" },
        { { 0, 20 }, "Artist - Title - 02.wav", "Invalid range" },
        { { 7, 12 }, "Artist - Title - 03.wav", "Shared.wav" }
    };
    const auto version = cratesnip::SourceFileAccess (source.getFile()).version();
    check (version.has_value(), "capture the open track version");
    if (! version)
        return failures;

    {
        using Sharing = cratesnip::SourceFileAccess::Sharing;
        const auto canWriteWhileOpen = [&] (Sharing sharing)
        {
            const cratesnip::SourceFileAccess access (source.getFile(), sharing);
            juce::FileOutputStream writer (source.getFile());
            return access.version() == version && writer.openedOk();
        };
        check (canWriteWhileOpen (Sharing::allowWriters), "preview access lets other programs write the source");
        check (! canWriteWhileOpen (Sharing::blockWriters), "export access keeps the source unchanged");
    }

    const auto batch = cratesnip::exportRegions (source.getFile(), *version, cuts, folder);
    check (batch.size() == 3, "one result for each cut");
    if (batch.size() == 3)
    {
        check (batch[0].error.isEmpty() && batch[1].error.isNotEmpty() && batch[2].error.isEmpty(),
               "a failed range does not prevent exporting the other cuts");
        check (batch[0].file.getParentDirectory() == folder && batch[2].file.getParentDirectory() == folder,
               "batch uses the chosen folder for all files");
        check (batch[0].file.getFileName() == "Shared.wav" && batch[2].file != batch[0].file,
               "duplicate edited filenames stay distinct");
        const auto first = openWav (batch[0].file);
        const auto last = openWav (batch[2].file);
        check (first != nullptr && first->lengthInSamples == 3 && last != nullptr && last->lengthInSamples == 5,
               "non-adjacent cuts keep their independent lengths");
    }
    for (const auto& item : batch)
        if (item.file.getParentDirectory() == folder)
            item.file.deleteFile();

    juce::TemporaryFile renamedSource (".wav");
    const auto replaced = source.getFile().moveFileTo (renamedSource.getFile())
                       && renamedSource.getFile().copyFileTo (source.getFile());
    check (replaced, "replace source fixture with an equal-length file");
    if (replaced)
    {
        const auto stale = cratesnip::exportRegions (source.getFile(), *version, cuts, folder);
        check (stale.size() == cuts.size()
               && std::all_of (stale.begin(), stale.end(), [] (const auto& item)
                              { return item.error == cratesnip::sourceUnavailable; }),
               "a replacement source at the same path requires reopening");
        check (folder.getNumberOfChildFiles (juce::File::findFiles) == 0,
               "a replacement source produces no exports");
    }

    folder.deleteFile();

    const cratesnip::Region fallback { { 1, 2 }, "Artist - Title - 02.wav", " .wav " };
    check (cratesnip::regionFilename (fallback) == fallback.suggestedFilename, "empty edited name uses its suggestion");
    const cratesnip::Region reserved { { 1, 2 }, "Artist - Title - 02.wav", "CON.wav" };
    check (cratesnip::regionFilename (reserved) == "_CON.wav", "reserved device name becomes a filename");

    return failures;
}
