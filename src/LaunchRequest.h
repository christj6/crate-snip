#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace cratesnip
{

/** What foobar2000 (or anyone else) asked CrateSnip to open, read from the command line.

    The expected form is:
        CrateSnip.exe "<path>" --artist "<artist>" --title "<title>" --position "<seconds>"

    Everything but the path is optional. foobar2000 fills a missing tag with "?" unless the field
    is wrapped in [square brackets], so a lone "?" is treated as empty too.
*/
struct LaunchRequest
{
    juce::File file;
    juce::String artist;
    juce::String title;
    std::optional<double> positionSeconds;

    /** Anything that couldn't be understood, to show to the user rather than silently drop. */
    juce::StringArray problems;

    /** Builds a WAV filename such as artist-songTitle-chunk1.wav. Uses the source filename if a tag is missing. */
    juce::String suggestedFilename (unsigned chunkNumber) const;

    static LaunchRequest parse (const juce::StringArray& args);
};

} // namespace cratesnip
