#pragma once

#include "RegionAudio.h"

namespace cratesnip
{

bool isAacInput (const juce::File&);

std::unique_ptr<juce::AudioFormatReader> openAacReader (const juce::File&, std::unique_ptr<SourceFileAccess>,
                                                      const std::atomic_bool* cancelled);

} // namespace cratesnip
