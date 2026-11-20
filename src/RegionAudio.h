#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <optional>
#include <vector>

namespace cratesnip
{

// The start frame is included and the end frame is excluded.
using FrameRange = juce::Range<juce::int64>;

// JUCE's export helper leaves one entry in its 128-slot table for a null terminator.
inline constexpr unsigned maxSupportedChannels = 127;

constexpr bool isSupportedChannelCount (unsigned channels)
{
    return channels > 0 && channels <= maxSupportedChannels;
}

inline constexpr auto unsupportedChannelCount = "CrateSnip supports audio with 1 to 127 channels.";

std::unique_ptr<juce::AudioFormatReader> makeRegionReader (std::unique_ptr<juce::AudioFormatReader>,
                                                         FrameRange);

std::unique_ptr<juce::AudioFormatReaderSource> makePreviewSource (std::unique_ptr<juce::AudioFormatReader>,
                                                                  FrameRange, bool looping);

struct Region
{
    FrameRange frames;
    juce::String suggestedFilename;
    juce::String name;
};

inline constexpr auto wavExtension = ".wav";

juce::String withoutWavExtension (const juce::String&);
juce::String regionFilename (const Region&);

struct SourceVersion
{
    juce::uint64 volume;
    juce::uint64 fileIndex;
    juce::uint64 bytes;
    juce::uint64 lastWrite;
    juce::int64 changeTime;

    bool operator== (const SourceVersion&) const = default;
};

// Opens the source to read its version or to decode from the same handle. Preview allows
// tag edits, renames, and deletes. Export holds the source steady until it completes.
class SourceFileAccess final : public juce::InputStream
{
public:
    enum class Sharing { blockWriters, allowWriters };

    explicit SourceFileAccess (const juce::File&, Sharing = Sharing::blockWriters);
    std::optional<SourceVersion> version() const;

    juce::int64 getTotalLength() override;
    juce::int64 getPosition() override;
    bool setPosition (juce::int64) override;
    bool isExhausted() override;
    int read (void*, int) override;

private:
    struct CloseHandle
    {
        void operator() (void*) const noexcept;
    };
    std::unique_ptr<void, CloseHandle> handle;
    juce::int64 position = 0;
};

inline constexpr auto sourceUnavailable = "The source changed or is unavailable. Reopen the track in CrateSnip.";

std::unique_ptr<juce::AudioFormatReader> openAudioReader (juce::AudioFormatManager&, const juce::File&,
                                                         const std::atomic_bool* cancelled = nullptr);

struct VerifiedPreview
{
    std::unique_ptr<juce::AudioFormatReaderSource> source;
    juce::String error;
};

VerifiedPreview openVerifiedPreview (juce::AudioFormatManager&, const juce::File&,
                                     const std::optional<SourceVersion>& expectedVersion, FrameRange, bool looping);

struct ExportResult
{
    juce::File file;
    juce::String error;
};

ExportResult exportRegion (std::unique_ptr<juce::AudioFormatReader>, FrameRange,
                           const juce::File& destination, const std::atomic_bool* cancelled = nullptr);

std::vector<ExportResult> exportRegions (const juce::File& source, SourceVersion, const std::vector<Region>&,
                                         const juce::File& folder, const std::atomic_bool* cancelled = nullptr);

} // namespace cratesnip
