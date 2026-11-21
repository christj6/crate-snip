#include "Gapless.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>

namespace cratesnip
{
namespace
{

// JUCE's MP3 decoder, like other mpg123-style decoders, outputs this many samples before the
// encoder's first one.
constexpr juce::int64 mp3DecoderDelay = 529;

bool readExactly (juce::InputStream& stream, void* destination, int bytes)
{
    return stream.read (destination, bytes) == bytes;
}

std::optional<GaplessInfo> readLameTag (juce::InputStream& stream)
{
    std::array<juce::uint8, 10> id3 {};
    if (! stream.setPosition (0) || ! readExactly (stream, id3.data(), static_cast<int> (id3.size())))
        return {};

    juce::int64 frameStart = 0;
    if (id3[0] == 'I' && id3[1] == 'D' && id3[2] == '3')
    {
        frameStart = 10 + ((id3[6] & 0x7f) << 21 | (id3[7] & 0x7f) << 14 | (id3[8] & 0x7f) << 7 | (id3[9] & 0x7f));
        if ((id3[5] & 0x10) != 0)
            frameStart += 10;
    }

    // The tag lives in the first frame, a silent one that decoders skip.
    std::array<juce::uint8, 512> frame {};
    if (! stream.setPosition (frameStart) || ! readExactly (stream, frame.data(), static_cast<int> (frame.size())))
        return {};

    if (frame[0] != 0xff || (frame[1] & 0xe0) != 0xe0 || ((frame[1] >> 1) & 3) != 1)
        return {};

    const auto mpeg1 = ((frame[1] >> 3) & 3) == 3;
    const auto mono = ((frame[3] >> 6) & 3) == 3;
    const auto hasCrc = (frame[1] & 1) == 0;
    size_t position = 4 + (hasCrc ? 2u : 0u) + (mpeg1 ? (mono ? 17u : 32u) : (mono ? 9u : 17u));

    const auto at = [&] (size_t offset, const char* text) { return std::memcmp (frame.data() + offset, text, 4) == 0; };
    if (! at (position, "Xing") && ! at (position, "Info"))
        return {};

    const auto flags = frame[position + 7];
    position += 8;

    std::optional<juce::int64> audioFrames;
    if ((flags & 1) != 0)
        audioFrames = static_cast<juce::int64> (frame[position]) << 24 | frame[position + 1] << 16
                    | frame[position + 2] << 8 | frame[position + 3];
    position += (flags & 1) != 0 ? 4u : 0u;
    position += (flags & 2) != 0 ? 4u : 0u;
    position += (flags & 4) != 0 ? 100u : 0u;
    position += (flags & 8) != 0 ? 4u : 0u;

    // The LAME tag starts with the encoder's name, e.g. "LAME3.100" or "Lavc61.3.".
    if (position + 24 > frame.size())
        return {};
    for (size_t i = 0; i < 4; ++i)
        if (! std::isalnum (frame[position + i]))
            return {};

    const auto* delays = frame.data() + position + 21;
    const juce::int64 encoderDelay = (delays[0] << 4) | (delays[1] >> 4);
    const juce::int64 padding = ((delays[1] & 0x0f) << 8) | delays[2];

    // Prefer the recorded frame count: JUCE reports double the length of MPEG-2 files (22.05 and
    // 24 kHz). Without it, subtract from the decoded length, which counts whole frames.
    const juce::int64 samplesPerFrame = mpeg1 ? 1152 : 576;
    if (audioFrames && *audioFrames * samplesPerFrame > encoderDelay + padding)
        return GaplessInfo { encoderDelay + mp3DecoderDelay, *audioFrames * samplesPerFrame - encoderDelay - padding };
    return GaplessInfo { encoderDelay + mp3DecoderDelay, -(encoderDelay + padding) };
}

struct Mp4Box
{
    juce::int64 start = 0, contentStart = 0, end = 0;
    std::array<char, 4> type {};

    bool is (const char* name) const { return std::memcmp (type.data(), name, 4) == 0; }
};

// Calls visit for each box between start and end. visit returns false to stop.
void forEachBox (juce::InputStream& stream, juce::int64 start, juce::int64 end,
                 const std::function<bool (const Mp4Box&)>& visit)
{
    for (auto position = start; position + 8 <= end;)
    {
        Mp4Box box;
        box.start = position;
        if (! stream.setPosition (position))
            return;

        auto size = static_cast<juce::int64> (static_cast<juce::uint32> (stream.readIntBigEndian()));
        if (! readExactly (stream, box.type.data(), 4))
            return;

        box.contentStart = position + 8;
        if (size == 1)
        {
            size = stream.readInt64BigEndian();
            box.contentStart += 8;
        }
        else if (size == 0)
        {
            size = end - position;
        }

        if (size < box.contentStart - position || position + size > end)
            return;

        box.end = position + size;
        if (! visit (box))
            return;
        position = box.end;
    }
}

std::optional<Mp4Box> findChild (juce::InputStream& stream, const Mp4Box& parent, const char* name,
                                 juce::int64 skip = 0)
{
    std::optional<Mp4Box> found;
    forEachBox (stream, parent.contentStart + skip, parent.end, [&] (const Mp4Box& box)
    {
        if (box.is (name))
            found = box;
        return ! found;
    });
    return found;
}

// iTunes records " 00000000 PPPPPPPP QQQQQQQQ LLLLLLLLLLLLLLLL ..." in hex: priming, padding, length.
std::optional<GaplessInfo> readITunSmpb (juce::InputStream& stream, const Mp4Box& moov)
{
    const auto udta = findChild (stream, moov, "udta");
    const auto meta = udta ? findChild (stream, *udta, "meta") : std::nullopt;
    if (! meta)
        return {};

    // meta is a full box (4 bytes of version and flags) in MP4, a plain one in QuickTime files.
    stream.setPosition (meta->contentStart);
    const auto metaSkip = stream.readIntBigEndian() == 0 ? 4 : 0;
    const auto ilst = findChild (stream, *meta, "ilst", metaSkip);
    if (! ilst)
        return {};

    std::optional<GaplessInfo> info;
    forEachBox (stream, ilst->contentStart, ilst->end, [&] (const Mp4Box& item)
    {
        if (! item.is ("----"))
            return true;

        const auto readText = [&] (const char* name, int headerBytes) -> juce::String
        {
            const auto box = findChild (stream, item, name);
            if (! box || box->end - box->contentStart <= headerBytes || box->end - box->contentStart > 4096)
                return {};
            juce::MemoryBlock text;
            stream.setPosition (box->contentStart + headerBytes);
            stream.readIntoMemoryBlock (text, box->end - box->contentStart - headerBytes);
            return text.toString();
        };

        if (readText ("name", 4) != "iTunSMPB")
            return true;

        auto values = juce::StringArray::fromTokens (readText ("data", 8), " ", {});
        values.removeEmptyStrings();
        if (values.size() >= 4)
            info = GaplessInfo { values[1].getHexValue64(), values[3].getHexValue64() };
        return false;
    });
    return info;
}

std::optional<GaplessInfo> readEditList (juce::InputStream& stream, const Mp4Box& moov)
{
    const auto mvhd = findChild (stream, moov, "mvhd");
    if (! mvhd)
        return {};
    stream.setPosition (mvhd->contentStart);
    const auto movieVersion = stream.readByte();
    stream.setPosition (mvhd->contentStart + (movieVersion == 1 ? 20 : 12));
    const auto movieTimescale = static_cast<juce::uint32> (stream.readIntBigEndian());

    std::optional<GaplessInfo> info;
    forEachBox (stream, moov.contentStart, moov.end, [&] (const Mp4Box& trak)
    {
        if (! trak.is ("trak"))
            return true;

        const auto mdia = findChild (stream, trak, "mdia");
        const auto hdlr = mdia ? findChild (stream, *mdia, "hdlr") : std::nullopt;
        const auto mdhd = mdia ? findChild (stream, *mdia, "mdhd") : std::nullopt;
        if (! hdlr || ! mdhd)
            return true;

        std::array<char, 4> handler {};
        stream.setPosition (hdlr->contentStart + 8);
        if (! readExactly (stream, handler.data(), 4) || std::memcmp (handler.data(), "soun", 4) != 0)
            return true;

        stream.setPosition (mdhd->contentStart);
        const auto mediaVersion = stream.readByte();
        stream.setPosition (mdhd->contentStart + (mediaVersion == 1 ? 20 : 12));
        const auto mediaTimescale = static_cast<juce::uint32> (stream.readIntBigEndian());

        const auto edts = findChild (stream, trak, "edts");
        const auto elst = edts ? findChild (stream, *edts, "elst") : std::nullopt;
        if (! elst || movieTimescale == 0 || mediaTimescale == 0)
            return false;

        stream.setPosition (elst->contentStart);
        const auto listVersion = stream.readByte();
        stream.setPosition (elst->contentStart + 4);
        const auto entries = static_cast<juce::uint32> (stream.readIntBigEndian());

        // One edit that plays the track from a later start is how encoders record priming.
        if (entries != 1)
            return false;

        const auto duration = listVersion == 1 ? stream.readInt64BigEndian()
                                               : static_cast<juce::int64> (static_cast<juce::uint32> (stream.readIntBigEndian()));
        const auto mediaTime = listVersion == 1 ? stream.readInt64BigEndian()
                                                : static_cast<juce::int64> (stream.readIntBigEndian());
        if (mediaTime < 0)
            return false;

        // The duration is in the movie's timescale; express both in the track's.
        const auto length = static_cast<juce::int64> (std::llround (static_cast<double> (duration) * mediaTimescale
                                                                    / movieTimescale));
        info = GaplessInfo { mediaTime, duration > 0 ? std::optional (length) : std::nullopt, mediaTimescale };
        return false;
    });
    return info;
}

std::optional<GaplessInfo> readMp4Gapless (juce::InputStream& stream)
{
    const auto length = stream.getTotalLength();
    if (length <= 0)
        return {};

    std::optional<Mp4Box> moov;
    forEachBox (stream, 0, length, [&] (const Mp4Box& box)
    {
        if (box.is ("moov"))
            moov = box;
        return ! moov;
    });
    if (! moov)
        return {};

    if (auto info = readITunSmpb (stream, *moov))
        return info;
    return readEditList (stream, *moov);
}

class TrimmedReader final : public juce::AudioSubsectionReader
{
public:
    TrimmedReader (std::unique_ptr<juce::AudioFormatReader> reader, juce::int64 start, juce::int64 length)
        : AudioSubsectionReader (reader.get(), start, length, true), source (reader.release())
    {
    }

    juce::AudioChannelSet getChannelLayout() override { return source->getChannelLayout(); }

private:
    juce::AudioFormatReader* source; // Owned and deleted by the base class.
};

} // namespace

std::optional<GaplessInfo> readGaplessInfo (juce::InputStream& stream, const juce::File& file)
{
    if (file.hasFileExtension ("mp3"))
        return readLameTag (stream);
    if (file.hasFileExtension ("m4a;mp4"))
        return readMp4Gapless (stream);
    return {};
}

std::unique_ptr<juce::AudioFormatReader> trimToRealAudio (std::unique_ptr<juce::AudioFormatReader> reader,
                                                         const std::optional<GaplessInfo>& info)
{
    if (reader == nullptr || ! info || reader->sampleRate <= 0)
        return reader;

    const auto toDecoded = [&] (juce::int64 value)
    {
        return info->timescale == 0 ? value
                                    : static_cast<juce::int64> (std::llround (static_cast<double> (value)
                                                                              * reader->sampleRate / info->timescale));
    };

    const auto skip = toDecoded (info->skipFrames);
    if (skip < 0 || skip >= reader->lengthInSamples)
        return reader;

    const auto available = reader->lengthInSamples - skip;
    auto length = available;
    if (info->lengthFrames)
    {
        // A negative length is relative to the decoded length (MP3's padding is recorded that way).
        const auto recorded = *info->lengthFrames < 0 ? reader->lengthInSamples + *info->lengthFrames
                                                      : toDecoded (*info->lengthFrames);
        if (recorded > 0)
            length = juce::jmin (available, recorded);
    }

    if (skip == 0 && length == reader->lengthInSamples)
        return reader;

    return std::make_unique<TrimmedReader> (std::move (reader), skip, length);
}

} // namespace cratesnip
