#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <optional>

namespace cratesnip
{

/** Where a lossy file's real audio sits within its decoded output. Encoders add silence at the
    start (and decoders add a little more) and pad the end. foobar2000 removes it using the file's
    own records of it, so its playhead counts from the first real sample; CrateSnip must do the same
    for the playhead and its cuts to line up.
*/
struct GaplessInfo
{
    juce::int64 skipFrames = 0;

    /** The real audio's length. A negative value is relative to the decoded length instead. */
    std::optional<juce::int64> lengthFrames;

    /** Units per second of the values above, or 0 when they're already in decoded frames. */
    juce::uint32 timescale = 0;
};

/** Reads the padding recorded in an MP3's LAME tag, or an MP4/M4A's iTunSMPB tag or edit list.
    Returns nothing for other formats, or files with no record (where foobar2000 doesn't trim
    either). The stream's position is left undefined.
*/
std::optional<GaplessInfo> readGaplessInfo (juce::InputStream&, const juce::File& file);

/** Wraps a reader so it starts at the first real sample and stops at the last. Returns the reader
    unchanged when the info doesn't fit it.
*/
std::unique_ptr<juce::AudioFormatReader> trimToRealAudio (std::unique_ptr<juce::AudioFormatReader>,
                                                         const std::optional<GaplessInfo>&);

} // namespace cratesnip
