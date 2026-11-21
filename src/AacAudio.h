#pragma once

#include "RegionAudio.h"

namespace cratesnip
{

bool isAacInput (const juce::File&);

/** Where decoded AAC audio is kept while a window is open. */
juce::File decodedAacFolder();

/** Deletes decoded AAC files left in the folder by a power loss or system crash, which skip the
    usual delete-on-close. Files another CrateSnip window has open can't be deleted, so they're
    left alone. Returns how many were deleted.
*/
int deleteLeftoverDecodedAac (const juce::File& folder = decodedAacFolder());

std::unique_ptr<juce::AudioFormatReader> openAacReader (const juce::File&, std::unique_ptr<SourceFileAccess>,
                                                      const std::atomic_bool* cancelled);

} // namespace cratesnip
