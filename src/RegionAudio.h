#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>

namespace cratesnip
{

// The start frame is included and the end frame is excluded.
using FrameRange = juce::Range<juce::int64>;

std::unique_ptr<juce::AudioFormatReader> makeRegionReader (std::unique_ptr<juce::AudioFormatReader>,
                                                         FrameRange);

struct ExportResult
{
    juce::File file;
    juce::String error;
};

ExportResult exportRegion (std::unique_ptr<juce::AudioFormatReader>, FrameRange,
                           const juce::File& destination, const std::atomic_bool* cancelled = nullptr);

} // namespace cratesnip
