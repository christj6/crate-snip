#include "AacAudio.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl.h>

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <limits>
#include <mutex>

namespace cratesnip
{
namespace
{
using Microsoft::WRL::ComPtr;

constexpr auto decodedAacPrefix = "CrateSnip-AAC-";
constexpr auto decodedAacSuffix = ".tmp";

// Load Media Foundation only for AAC so the other formats still work when it is absent.
class MediaFoundation
{
public:
    MediaFoundation() = default;
    ~MediaFoundation()
    {
        if (readerLibrary != nullptr)
            FreeLibrary (readerLibrary);
        if (platform != nullptr)
            FreeLibrary (platform);
    }

    MediaFoundation (const MediaFoundation&) = delete;
    MediaFoundation& operator= (const MediaFoundation&) = delete;

private:
    template <typename Function>
    static Function lookup (HMODULE library, const char* name)
    {
        return library != nullptr ? std::bit_cast<Function> (GetProcAddress (library, name)) : nullptr;
    }

    HMODULE platform = LoadLibraryExW (L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE readerLibrary = LoadLibraryExW (L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

public:
    const decltype (&MFStartup) startup = lookup<decltype (&MFStartup)> (platform, "MFStartup");
    const decltype (&MFShutdown) shutdown = lookup<decltype (&MFShutdown)> (platform, "MFShutdown");
    const decltype (&MFCreateMFByteStreamOnStream) createByteStream =
        lookup<decltype (&MFCreateMFByteStreamOnStream)> (platform, "MFCreateMFByteStreamOnStream");
    const decltype (&MFCreateSourceResolver) createResolver =
        lookup<decltype (&MFCreateSourceResolver)> (platform, "MFCreateSourceResolver");
    const decltype (&MFCreateMediaType) createType =
        lookup<decltype (&MFCreateMediaType)> (platform, "MFCreateMediaType");
    const decltype (&MFCreateSourceReaderFromMediaSource) createReader =
        lookup<decltype (&MFCreateSourceReaderFromMediaSource)> (readerLibrary, "MFCreateSourceReaderFromMediaSource");
};

class MediaSession
{
public:
    explicit MediaSession (const MediaFoundation& api) : functions (api)
    {
        if ((SUCCEEDED (comStatus) || comStatus == RPC_E_CHANGED_MODE)
            && api.startup != nullptr && api.shutdown != nullptr && api.createByteStream != nullptr
            && api.createResolver != nullptr && api.createType != nullptr && api.createReader != nullptr)
            started = SUCCEEDED (api.startup (MF_VERSION, MFSTARTUP_LITE));
    }

    ~MediaSession()
    {
        if (started)
            functions.shutdown();
        if (SUCCEEDED (comStatus))
            CoUninitialize();
    }

    bool isReady() const { return started; }

private:
    const MediaFoundation& functions;
    const HRESULT comStatus = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    bool started = false;
};

class SourceStream final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IStream, Microsoft::WRL::FtmBase>
{
public:
    explicit SourceStream (std::unique_ptr<SourceFileAccess> stream) : source (std::move (stream)) {}

    std::optional<SourceVersion> version()
    {
        const std::lock_guard lock (mutex);
        return source->version();
    }

    IFACEMETHODIMP Read (void* destination, ULONG size, ULONG* count) override
    {
        const std::lock_guard lock (mutex);
        ULONG total = 0;
        auto* bytes = static_cast<std::byte*> (destination);
        while (total < size)
        {
            const auto requested = static_cast<int> (juce::jmin (size - total, static_cast<ULONG> (INT_MAX)));
            const auto read = source->read (bytes + total, requested);
            total += static_cast<ULONG> (read);
            if (read < requested)
                break;
        }
        if (count != nullptr)
            *count = total;
        return total == size ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP Seek (LARGE_INTEGER offset, DWORD origin, ULARGE_INTEGER* position) override
    {
        const std::lock_guard lock (mutex);
        juce::int64 base = 0;
        switch (origin)
        {
            case STREAM_SEEK_SET: break;
            case STREAM_SEEK_CUR: base = source->getPosition(); break;
            case STREAM_SEEK_END: base = source->getTotalLength(); break;
            default: return STG_E_INVALIDFUNCTION;
        }
        if (base < 0 || offset.QuadPart < -base || offset.QuadPart > std::numeric_limits<juce::int64>::max() - base
            || ! source->setPosition (base + offset.QuadPart))
            return STG_E_SEEKERROR;

        if (position != nullptr)
            position->QuadPart = static_cast<ULONGLONG> (source->getPosition());
        return S_OK;
    }

    IFACEMETHODIMP Stat (STATSTG* info, DWORD) override
    {
        const std::lock_guard lock (mutex);
        const auto length = source->getTotalLength();
        if (length < 0)
            return STG_E_READFAULT;

        *info = {};
        info->type = STGTY_STREAM;
        info->cbSize.QuadPart = static_cast<ULONGLONG> (length);
        info->grfMode = STGM_READ;
        return S_OK;
    }

    IFACEMETHODIMP Write (const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
    IFACEMETHODIMP SetSize (ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    IFACEMETHODIMP CopyTo (IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    IFACEMETHODIMP Commit (DWORD) override { return E_NOTIMPL; }
    IFACEMETHODIMP Revert() override { return E_NOTIMPL; }
    IFACEMETHODIMP LockRegion (ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    IFACEMETHODIMP UnlockRegion (ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    IFACEMETHODIMP Clone (IStream**) override { return E_NOTIMPL; }

private:
    std::unique_ptr<SourceFileAccess> source;
    std::mutex mutex;
};

struct MediaSource
{
    ComPtr<IMFMediaSource> handle;

    ~MediaSource()
    {
        if (handle != nullptr)
            handle->Shutdown();
    }
};

struct PcmFormat
{
    UINT32 rate = 0;
    UINT32 channels = 0;
    UINT32 channelMask = 0;

    bool operator== (const PcmFormat&) const = default;
};

class DecodedAudio
{
public:
    DecodedAudio()
    {
        const auto file = decodedAacFolder().getChildFile (decodedAacPrefix + juce::Uuid().toString() + decodedAacSuffix);
        handle = CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    }

    ~DecodedAudio()
    {
        if (isOpen())
            CloseHandle (handle);
    }

    DecodedAudio (const DecodedAudio&) = delete;
    DecodedAudio& operator= (const DecodedAudio&) = delete;

    bool isOpen() const { return handle != INVALID_HANDLE_VALUE; }

    bool append (const void* data, DWORD count)
    {
        DWORD written = 0;
        return WriteFile (handle, data, count, &written, nullptr) && written == count;
    }

    bool read (juce::int64 offset, void* data, DWORD count) const
    {
        const std::lock_guard lock (mutex);
        LARGE_INTEGER position {};
        position.QuadPart = offset;
        DWORD received = 0;
        return SetFilePointerEx (handle, position, nullptr, FILE_BEGIN)
            && ReadFile (handle, data, count, &received, nullptr) && received == count;
    }

    PcmFormat format;
    juce::int64 frames = 0;

private:
    HANDLE handle = INVALID_HANDLE_VALUE;
    mutable std::mutex mutex;
};

class AacReader final : public juce::AudioFormatReader
{
public:
    explicit AacReader (std::shared_ptr<const DecodedAudio> pcm)
        : AudioFormatReader (nullptr, "AAC (Windows Media Foundation)"), audio (std::move (pcm))
    {
        sampleRate = audio->format.rate;
        numChannels = audio->format.channels;
        lengthInSamples = audio->frames;
        bitsPerSample = 32;
        usesFloatingPointData = true;
    }

    juce::AudioChannelSet getChannelLayout() override
    {
        const auto layout = juce::AudioChannelSet::fromWaveChannelMask (static_cast<juce::int32> (audio->format.channelMask));
        return layout.size() == static_cast<int> (numChannels) ? layout : AudioFormatReader::getChannelLayout();
    }

    bool readSamples (int* const* destination, int destinationChannels, int offset,
                      juce::int64 start, int count) override
    {
        if (destination == nullptr || start < 0)
            return false;

        const auto channels = static_cast<int> (numChannels);
        const auto frameBytes = static_cast<juce::int64> (numChannels * sizeof (float));
        clearSamplesBeyondAvailableLength (destination, destinationChannels, offset,
                                           juce::jmin (start, lengthInSamples), count, lengthInSamples);
        std::array<float, 8192> interleaved;
        while (count > 0)
        {
            const auto frames = juce::jmin (count, static_cast<int> (interleaved.size()) / channels);
            if (! audio->read (start * frameBytes, interleaved.data(), static_cast<DWORD> (frames * frameBytes)))
                return false;

            for (int channel = 0; channel < destinationChannels; ++channel)
                if (auto* output = reinterpret_cast<float*> (destination[channel]))
                    for (int frame = 0; frame < frames; ++frame)
                        output[offset + frame] = channel < channels ? interleaved[static_cast<size_t> (frame * channels + channel)]
                                                                    : 0.0f;
            offset += frames;
            start += frames;
            count -= frames;
        }

        return true;
    }

private:
    std::shared_ptr<const DecodedAudio> audio;
};

struct DecodeCache
{
    std::mutex mutex;
    std::optional<SourceVersion> version;
    std::weak_ptr<const DecodedAudio> audio;
};

std::optional<PcmFormat> getPcmFormat (IMFSourceReader& reader, DWORD stream)
{
    ComPtr<IMFMediaType> type;
    GUID subtype {};
    PcmFormat format;
    UINT32 depth = 0;
    if (FAILED (reader.GetCurrentMediaType (stream, &type))
        || FAILED (type->GetGUID (MF_MT_SUBTYPE, &subtype)) || subtype != MFAudioFormat_Float
        || FAILED (type->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &format.rate))
        || FAILED (type->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &format.channels))
        || FAILED (type->GetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, &depth))
        || format.rate == 0 || ! isSupportedChannelCount (format.channels) || depth != 32)
        return {};

    type->GetUINT32 (MF_MT_AUDIO_CHANNEL_MASK, &format.channelMask);
    return format;
}

std::shared_ptr<const DecodedAudio> decode (const juce::File& file, std::unique_ptr<SourceFileAccess> stream,
                                           const SourceVersion& version, const std::atomic_bool* cancelled)
{
    static const MediaFoundation api;
    const MediaSession session (api);
    if (! session.isReady())
        return {};

    const auto input = Microsoft::WRL::Make<SourceStream> (std::move (stream));
    ComPtr<IMFByteStream> bytes;
    ComPtr<IMFSourceResolver> resolver;
    ComPtr<IUnknown> sourceObject;
    MF_OBJECT_TYPE objectType {};
    const auto fileHint = file.getFileName();
    if (input == nullptr || FAILED (api.createByteStream (input.Get(), &bytes))
        || FAILED (api.createResolver (&resolver))
        || FAILED (resolver->CreateObjectFromByteStream (bytes.Get(), fileHint.toWideCharPointer(),
                                                        MF_RESOLUTION_MEDIASOURCE, nullptr, &objectType, &sourceObject)))
        return {};

    MediaSource media;
    ComPtr<IMFSourceReader> reader;
    if (FAILED (sourceObject.As (&media.handle)) || FAILED (api.createReader (media.handle.Get(), nullptr, &reader)))
        return {};

    constexpr auto audio = static_cast<DWORD> (MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    ComPtr<IMFMediaType> native;
    GUID subtype {};
    if (FAILED (reader->GetNativeMediaType (audio, 0, &native))
        || FAILED (native->GetGUID (MF_MT_SUBTYPE, &subtype))
        || subtype != MFAudioFormat_AAC)
        return {};

    ComPtr<IMFMediaType> requested;
    if (FAILED (reader->SetStreamSelection (static_cast<DWORD> (MF_SOURCE_READER_ALL_STREAMS), FALSE))
        || FAILED (reader->SetStreamSelection (audio, TRUE))
        || FAILED (api.createType (&requested))
        || FAILED (requested->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio))
        || FAILED (requested->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_Float))
        || FAILED (reader->SetCurrentMediaType (audio, nullptr, requested.Get())))
        return {};

    auto pcm = std::make_shared<DecodedAudio>();
    const auto format = getPcmFormat (*reader.Get(), audio);
    if (! pcm->isOpen() || ! format)
        return {};
    pcm->format = *format;
    for (;;)
    {
        if (cancelled != nullptr && cancelled->load())
            return {};

        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED (reader->ReadSample (audio, 0, nullptr, &flags, nullptr, &sample))
            || (flags & MF_SOURCE_READERF_ERROR) != 0)
            return {};

        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0)
        {
            const auto updated = getPcmFormat (*reader.Get(), audio);
            if (! updated || (pcm->frames > 0 && *updated != pcm->format))
                return {};
            pcm->format = *updated;
        }

        if (sample != nullptr)
        {
            const auto frameBytes = static_cast<DWORD> (pcm->format.channels * sizeof (float));
            ComPtr<IMFMediaBuffer> buffer;
            BYTE* data = nullptr;
            DWORD bytesUsed = 0;
            if (FAILED (sample->ConvertToContiguousBuffer (&buffer))
                || FAILED (buffer->Lock (&data, nullptr, &bytesUsed)))
                return {};

            const auto written = bytesUsed % frameBytes == 0 && (bytesUsed == 0 || pcm->append (data, bytesUsed));
            buffer->Unlock();
            if (! written)
                return {};

            pcm->frames += bytesUsed / frameBytes;
        }

        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
            break;
    }

    return pcm->frames > 0 && input->version() == version ? pcm : nullptr;
}

} // namespace

bool isAacInput (const juce::File& file)
{
    return file.hasFileExtension ("aac;adts;m4a;mp4");
}

juce::File decodedAacFolder()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory);
}

int deleteLeftoverDecodedAac (const juce::File& folder)
{
    int deleted = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (folder, false,
                                                             juce::String (decodedAacPrefix) + "*" + decodedAacSuffix,
                                                             juce::File::findFiles))
    {
        // A window's open file was created without sharing, so this fails for it: only
        // files whose window is gone get deleted.
        if (DeleteFileW (entry.getFile().getFullPathName().toWideCharPointer()))
            ++deleted;
    }
    return deleted;
}

std::unique_ptr<juce::AudioFormatReader> openAacReader (const juce::File& file,
                                                      std::unique_ptr<SourceFileAccess> stream,
                                                      const std::atomic_bool* cancelled)
{
    const auto version = stream->version();
    if (! version || (cancelled != nullptr && cancelled->load()))
        return {};

    // Each editor window has one track. The waveform keeps the decoded audio alive for that window.
    static DecodeCache cache;
    {
        const std::lock_guard lock (cache.mutex);
        if (version == cache.version)
            if (auto pcm = cache.audio.lock())
                return std::make_unique<AacReader> (std::move (pcm));
    }

    auto pcm = decode (file, std::move (stream), *version, cancelled);
    if (pcm == nullptr)
        return {};

    {
        const std::lock_guard lock (cache.mutex);
        cache.version = version;
        cache.audio = pcm;
    }

    return std::make_unique<AacReader> (std::move (pcm));
}

} // namespace cratesnip
