#include "RegionAudio.h"
#include "AacAudio.h"
#include "Gapless.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>

#include <cstring>
#include <string>

namespace cratesnip
{

namespace
{

class TemporaryExport
{
public:
    explicit TemporaryExport (const juce::File& folder)
    {
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const auto path = folder.getChildFile (".cratesnip-" + juce::Uuid().toString() + ".tmp").getFullPathName();
            handle = CreateFileW (path.toWideCharPointer(), GENERIC_WRITE | DELETE, 0, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (isOpen())
                break;

            const auto error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
                break;
        }
    }

    ~TemporaryExport()
    {
        if (! isOpen())
            return;

        if (! published)
        {
            FILE_DISPOSITION_INFO disposition { TRUE };
            SetFileInformationByHandle (handle, FileDispositionInfo, &disposition, sizeof (disposition));
        }
        CloseHandle (handle);
    }

    TemporaryExport (const TemporaryExport&) = delete;
    TemporaryExport& operator= (const TemporaryExport&) = delete;

    bool isOpen() const { return handle != INVALID_HANDLE_VALUE; }

    class Stream final : public juce::OutputStream
    {
    public:
        explicit Stream (TemporaryExport& file) : destination (file) {}

        bool write (const void* data, size_t bytes) override
        {
            DWORD written = 0;
            if (bytes > MAXDWORD || ! WriteFile (destination.handle, data, static_cast<DWORD> (bytes), &written, nullptr)
                || written != bytes)
                destination.failed = true;
            position += written;
            return ! destination.failed;
        }

        void flush() override { destination.flush(); }

        juce::int64 getPosition() override { return position; }

        bool setPosition (juce::int64 newPosition) override
        {
            LARGE_INTEGER offset {};
            offset.QuadPart = newPosition;
            if (! SetFilePointerEx (destination.handle, offset, nullptr, FILE_BEGIN))
            {
                destination.failed = true;
                return false;
            }
            position = newPosition;
            return true;
        }

    private:
        TemporaryExport& destination;
        juce::int64 position = 0;
    };

    bool flush()
    {
        if (! FlushFileBuffers (handle))
            failed = true;
        return ! failed;
    }

    DWORD publish (const juce::File& target)
    {
        const std::wstring path (target.getFullPathName().toWideCharPointer());
        const auto nameBytes = path.size() * sizeof (wchar_t);
        const auto infoBytes = sizeof (FILE_RENAME_INFO) + nameBytes;
        juce::HeapBlock<FILE_RENAME_INFO> info;
        info.calloc (1, infoBytes);
        if (info.get() == nullptr)
            return ERROR_NOT_ENOUGH_MEMORY;

        info->FileNameLength = static_cast<DWORD> (nameBytes);
        std::memcpy (info->FileName, path.c_str(), nameBytes + sizeof (wchar_t));

        // ReplaceIfExists stays false. The handle identifies the file we created.
        if (! SetFileInformationByHandle (handle, FileRenameInfo, info.get(), static_cast<DWORD> (infoBytes)))
            return GetLastError();

        published = true;
        return ERROR_SUCCESS;
    }

private:
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool failed = false;
    bool published = false;
};

juce::uint64 combineWords (DWORD high, DWORD low)
{
    return (static_cast<juce::uint64> (high) << 32) | low;
}

} // namespace

SourceFileAccess::SourceFileAccess (const juce::File& file, Sharing sharing)
{
    const auto path = file.getFullPathName();
    const DWORD shareMode = sharing == Sharing::allowWriters ? FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
                                                             : FILE_SHARE_READ;
    auto opened = CreateFileW (path.toWideCharPointer(), GENERIC_READ, shareMode, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (opened != INVALID_HANDLE_VALUE)
        handle.reset (opened);
}

void SourceFileAccess::CloseHandle::operator() (void* opened) const noexcept
{
    ::CloseHandle (opened);
}

std::optional<SourceVersion> SourceFileAccess::version() const
{
    BY_HANDLE_FILE_INFORMATION info {};
    if (handle == nullptr || ! GetFileInformationByHandle (handle.get(), &info))
        return std::nullopt;

    FILE_BASIC_INFO basic {};
    GetFileInformationByHandleEx (handle.get(), FileBasicInfo, &basic, sizeof (basic));
    return SourceVersion { info.dwVolumeSerialNumber, combineWords (info.nFileIndexHigh, info.nFileIndexLow),
                           combineWords (info.nFileSizeHigh, info.nFileSizeLow),
                           combineWords (info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime),
                           basic.ChangeTime.QuadPart };
}

juce::int64 SourceFileAccess::getTotalLength()
{
    LARGE_INTEGER length {};
    return handle != nullptr && GetFileSizeEx (handle.get(), &length) ? length.QuadPart : -1;
}

juce::int64 SourceFileAccess::getPosition()
{
    return position;
}

bool SourceFileAccess::setPosition (juce::int64 offset)
{
    if (handle == nullptr || offset < 0)
        return false;

    if (offset != position)
    {
        LARGE_INTEGER target {};
        target.QuadPart = offset;
        if (! SetFilePointerEx (handle.get(), target, nullptr, FILE_BEGIN))
            return false;
        position = offset;
    }

    return true;
}

bool SourceFileAccess::isExhausted()
{
    return position >= getTotalLength();
}

int SourceFileAccess::read (void* buffer, int count)
{
    if (handle == nullptr || count <= 0)
        return 0;

    DWORD bytesRead = 0;
    if (! ReadFile (handle.get(), buffer, static_cast<DWORD> (count), &bytesRead, nullptr))
        return 0;

    position += bytesRead;
    return static_cast<int> (bytesRead);
}

std::unique_ptr<juce::AudioFormatReader> makeRegionReader (std::unique_ptr<juce::AudioFormatReader> reader,
                                                         FrameRange region)
{
    if (reader == nullptr || ! isSupportedChannelCount (reader->numChannels)
        || region.isEmpty() || region.getStart() < 0
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

std::unique_ptr<juce::AudioFormatReader> openAudioReader (juce::AudioFormatManager& formats, const juce::File& file,
                                                         const std::atomic_bool* cancelled)
{
    std::optional<GaplessInfo> gapless;
    {
        juce::FileInputStream stream (file);
        if (stream.openedOk())
            gapless = readGaplessInfo (stream, file);
    }

    if (isAacInput (file))
        return trimToRealAudio (openAacReader (file, std::make_unique<SourceFileAccess> (file), cancelled), gapless);

    return trimToRealAudio (std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)), gapless);
}

VerifiedPreview openVerifiedPreview (juce::AudioFormatManager& formats, const juce::File& file,
                                     const std::optional<SourceVersion>& expectedVersion,
                                     FrameRange region, bool looping)
{
    if (! expectedVersion)
        return { {}, sourceUnavailable };

    auto stream = std::make_unique<SourceFileAccess> (file, SourceFileAccess::Sharing::allowWriters);
    if (stream->version() != expectedVersion)
        return { {}, sourceUnavailable };

    // Trim the same padding as openAudioReader, so preview frames match the waveform's.
    const auto gapless = readGaplessInfo (*stream, file);
    if (! stream->setPosition (0))
        return { {}, sourceUnavailable };

    if (isAacInput (file))
    {
        if (auto preview = makePreviewSource (trimToRealAudio (openAacReader (file, std::move (stream), nullptr), gapless),
                                              region, looping))
            return { std::move (preview), {} };
        return { {}, "Could not read the selected AAC region." };
    }

    // Match JUCE's file-based decoder order, using the verified stream for every attempt.
    for (auto* format : formats)
    {
        if (! format->canHandleFile (file))
            continue;

        if (! stream->setPosition (0))
            break;

        std::unique_ptr<juce::AudioFormatReader> reader (format->createReaderFor (stream.get(), false));
        if (reader != nullptr)
        {
            stream.release(); // The reader owns the verified handle from here.
            if (auto preview = makePreviewSource (trimToRealAudio (std::move (reader), gapless), region, looping))
                return { std::move (preview), {} };
            break;
        }
    }

    return { {}, "Could not read the selected region." };
}

juce::String withoutWavExtension (const juce::String& name)
{
    const auto trimmed = name.trimEnd();
    return trimmed.endsWithIgnoreCase (wavExtension) ? trimmed.dropLastCharacters (4) : name;
}

juce::String regionFilename (const Region& region)
{
    const auto name = withoutWavExtension (region.name.trim());
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

    return stem + wavExtension;
}

ExportResult exportRegion (std::unique_ptr<juce::AudioFormatReader> reader, FrameRange region,
                           const juce::File& destination, const std::atomic_bool* cancelled)
{
    if (reader != nullptr && ! isSupportedChannelCount (reader->numChannels))
        return { {}, unsupportedChannelCount };

    const auto layout = reader != nullptr ? reader->getChannelLayout() : juce::AudioChannelSet();
    auto section = makeRegionReader (std::move (reader), region);
    if (section == nullptr)
        return { {}, "Could not read the selected region." };

    const auto requested = destination.withFileExtension (wavExtension);
    auto target = requested.getNonexistentSibling();
    TemporaryExport temporary (target.getParentDirectory());
    if (! temporary.isOpen())
        return { {}, "Could not write to the selected folder." };

    std::unique_ptr<juce::OutputStream> output = std::make_unique<TemporaryExport::Stream> (temporary);
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

    writer.reset();
    if (! temporary.flush())
        return { {}, "Could not finish writing the WAV." };
    if (cancelled != nullptr && cancelled->load())
        return { {}, "Export cancelled." };

    // Move within the destination folder without replacing an existing file.
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const auto error = temporary.publish (target);
        if (error == ERROR_SUCCESS)
            return { target, {} };

        if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
            break;

        target = requested.getNonexistentSibling();
    }
    return { {}, "Could not save the finished WAV. Try another filename or folder." };
}

std::vector<ExportResult> exportRegions (const juce::File& source, SourceVersion expectedVersion,
                                         const std::vector<Region>& regions, const juce::File& folder,
                                         const std::atomic_bool* cancelled)
{
    if (regions.empty() || (cancelled != nullptr && cancelled->load()))
        return {};

    const SourceFileAccess access (source);
    if (access.version() != expectedVersion)
        return std::vector<ExportResult> (regions.size(), { {}, sourceUnavailable });

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::vector<ExportResult> results;
    results.reserve (regions.size());
    for (const auto& region : regions)
    {
        if (cancelled != nullptr && cancelled->load())
            break;

        auto reader = openAudioReader (formats, source, cancelled);
        results.push_back (exportRegion (std::move (reader), region.frames,
                                         folder.getChildFile (regionFilename (region)), cancelled));
    }
    return results;
}

} // namespace cratesnip
