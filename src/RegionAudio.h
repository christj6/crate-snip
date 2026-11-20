#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
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

juce::String regionFilename (const Region&);

struct ExportResult
{
    juce::File file;
    juce::String error;
};

ExportResult exportRegion (std::unique_ptr<juce::AudioFormatReader>, FrameRange,
                           const juce::File& destination, const std::atomic_bool* cancelled = nullptr);

std::vector<ExportResult> exportRegions (const juce::File& source, const std::vector<Region>&,
                                         const juce::File& folder, const std::atomic_bool* cancelled = nullptr);

} // namespace cratesnip
