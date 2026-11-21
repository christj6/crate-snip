#include "Gapless.h"

#include <iostream>

namespace
{

juce::MemoryBlock bigEndian (juce::uint64 value, int bytes)
{
    juce::MemoryBlock block;
    for (int i = bytes - 1; i >= 0; --i)
    {
        const auto byte = static_cast<char> ((value >> (8 * i)) & 0xff);
        block.append (&byte, 1);
    }
    return block;
}

juce::MemoryBlock join (std::initializer_list<juce::MemoryBlock> parts)
{
    juce::MemoryBlock block;
    for (const auto& part : parts)
        block.append (part.getData(), part.getSize());
    return block;
}

juce::MemoryBlock text (const juce::String& value)
{
    return { value.toRawUTF8(), value.getNumBytesAsUTF8() };
}

juce::MemoryBlock box (const char* type, const juce::MemoryBlock& content)
{
    return join ({ bigEndian (content.getSize() + 8, 4), text (type), content });
}

juce::MemoryBlock fullBoxHeader() { return bigEndian (0, 4); }

// A sound track whose edit list starts the media at mediaTime and plays for duration (in the
// movie's 1000-per-second timescale).
juce::MemoryBlock soundTrack (juce::uint32 mediaTimescale, juce::int64 mediaTime, juce::uint32 duration)
{
    const auto hdlr = box ("hdlr", join ({ fullBoxHeader(), bigEndian (0, 4), text ("soun"), bigEndian (0, 12), text ("\0") }));
    const auto mdhd = box ("mdhd", join ({ fullBoxHeader(), bigEndian (0, 8), bigEndian (mediaTimescale, 4), bigEndian (0, 8) }));
    const auto elst = box ("elst", join ({ fullBoxHeader(), bigEndian (1, 4), bigEndian (duration, 4),
                                           bigEndian (static_cast<juce::uint64> (mediaTime), 4), bigEndian (0x10000, 4) }));
    return box ("trak", join ({ box ("edts", elst), box ("mdia", join ({ hdlr, mdhd })) }));
}

juce::MemoryBlock movie (const juce::MemoryBlock& contents)
{
    const auto mvhd = box ("mvhd", join ({ fullBoxHeader(), bigEndian (0, 8), bigEndian (1000, 4), bigEndian (0, 84) }));
    return join ({ box ("ftyp", text ("M4A ")), box ("mdat", bigEndian (0, 16)), box ("moov", join ({ mvhd, contents })) });
}

juce::MemoryBlock iTunSmpb (const juce::String& value)
{
    const auto item = box ("----", join ({ box ("mean", join ({ fullBoxHeader(), text ("com.apple.iTunes") })),
                                           box ("name", join ({ fullBoxHeader(), text ("iTunSMPB") })),
                                           box ("data", join ({ bigEndian (1, 4), bigEndian (0, 4), text (value) })) }));
    return box ("udta", box ("meta", join ({ fullBoxHeader(), box ("hdlr", bigEndian (0, 25)), box ("ilst", item) })));
}

// A Layer III stereo frame header (MPEG-1, or MPEG-2 for 22.05 and 24 kHz), its side info, then a
// Xing header, optionally with the frame count, and a LAME tag recording the encoder delay and padding.
juce::MemoryBlock mp3WithLameTag (int delay, int padding, bool mpeg2 = false, std::optional<juce::uint32> frames = {})
{
    juce::MemoryBlock frame (512, true);
    auto* bytes = static_cast<juce::uint8*> (frame.getData());
    bytes[0] = 0xff;
    bytes[1] = mpeg2 ? 0xf3 : 0xfb;
    bytes[2] = 0x90;
    bytes[3] = 0x00;
    const auto xing = mpeg2 ? 21 : 36;
    std::memcpy (bytes + xing, "Info", 4);
    auto lame = bytes + xing + 8;
    if (frames)
    {
        bytes[xing + 7] = 1;
        for (int i = 0; i < 4; ++i)
            lame[i] = static_cast<juce::uint8> (*frames >> (24 - 8 * i));
        lame += 4;
    }
    std::memcpy (lame, "LAME3.100", 9);
    lame[21] = static_cast<juce::uint8> (delay >> 4);
    lame[22] = static_cast<juce::uint8> (((delay & 0x0f) << 4) | (padding >> 8));
    lame[23] = static_cast<juce::uint8> (padding & 0xff);

    const auto id3 = join ({ text ("ID3"), bigEndian (0x0400, 2), bigEndian (0, 1), bigEndian (16, 4), bigEndian (0, 16) });
    return join ({ id3, frame });
}

class SilentReader final : public juce::AudioFormatReader
{
public:
    SilentReader (double rate, juce::int64 frames) : AudioFormatReader (nullptr, "Silent")
    {
        sampleRate = rate;
        lengthInSamples = frames;
        numChannels = 2;
    }

    bool readSamples (int* const*, int, int, juce::int64, int) override { return true; }
};

} // namespace

int runGaplessTests()
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

    const auto read = [] (const juce::MemoryBlock& data, const char* name)
    {
        juce::MemoryInputStream stream (data, false);
        return cratesnip::readGaplessInfo (stream, juce::File::getCurrentWorkingDirectory().getChildFile (name));
    };
    const auto trimmed = [] (double rate, juce::int64 frames, const std::optional<cratesnip::GaplessInfo>& info)
    {
        auto reader = cratesnip::trimToRealAudio (std::make_unique<SilentReader> (rate, frames), info);
        return reader->lengthInSamples;
    };

    // ffmpeg's AAC encoder, measured: 1024 frames of priming, 3 s of real audio at 44.1 kHz.
    const auto editList = read (movie (soundTrack (44100, 1024, 3000)), "track.m4a");
    check (editList && editList->skipFrames == 1024 && editList->lengthFrames == 132300 && editList->timescale == 44100,
           "an M4A edit list records the priming and real length");
    check (trimmed (44100, 134144, editList) == 132300, "the edit list trims a decoded AAC to its real length");
    check (trimmed (48000, 145980, editList) == 144000, "edit list values convert to the decoder's rate");

    const auto apple = read (movie (join ({ soundTrack (44100, 2112, 3000),
                                            iTunSmpb (" 00000000 00000840 000001CA 0000000000020F58 00000000") })),
                             "track.m4a");
    check (apple && apple->skipFrames == 2112 && apple->lengthFrames == 0x20F58 && apple->timescale == 0,
           "iTunes' iTunSMPB tag is preferred to the edit list");

    // LAME, measured: 576 frames of encoder delay plus the decoder's 529, then 756 of padding.
    const auto lame = read (mp3WithLameTag (576, 756), "track.mp3");
    check (lame && lame->skipFrames == 1105, "the LAME tag's encoder delay plus the decoder's delay is skipped");
    check (trimmed (44100, 133632, lame) == 132300, "the LAME tag's padding is trimmed from the end");

    // At 22.05 kHz, frames hold 576 samples, but JUCE reports twice the real length.
    const auto halfRate = read (mp3WithLameTag (576, 1242, true, 118), "track.mp3");
    check (halfRate && halfRate->skipFrames == 1105 && halfRate->lengthFrames == 66150,
           "an MPEG-2 MP3's length comes from its recorded frame count");
    check (trimmed (22050, 133542, halfRate) == 66150, "the frame count corrects JUCE's doubled MPEG-2 length");

    check (! read (mp3WithLameTag (576, 756), "track.aac"), "formats without padding records are left alone");
    check (! read (movie ({}), "track.m4a"), "an M4A with no sound track is left alone");
    check (trimmed (44100, 1000, cratesnip::GaplessInfo { 5000 }) == 1000, "a skip longer than the audio is ignored");

    return failures;
}
