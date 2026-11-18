#include "LaunchRequest.h"

namespace cratesnip
{

namespace
{
    juce::String cleanTag (const juce::String& value)
    {
        const auto trimmed = value.trim();
        return trimmed == "?" ? juce::String() : trimmed;
    }

    std::optional<double> parseSeconds (const juce::String& value)
    {
        const auto trimmed = value.trim();

        // Plain non-negative decimals only, e.g. "83.456000". getDoubleValue() would turn
        // anything else (including "?" for a track that isn't playing) into 0, which looks valid.
        if (trimmed.isEmpty()
            || ! trimmed.containsOnly ("0123456789.")
            || trimmed.indexOfChar ('.') != trimmed.lastIndexOfChar ('.')
            || trimmed == ".")
            return std::nullopt;

        return trimmed.getDoubleValue();
    }
} // namespace

juce::String LaunchRequest::suggestedBaseName() const
{
    const auto name = artist.isNotEmpty() && title.isNotEmpty()
                          ? artist + " - " + title
                          : file.getFileNameWithoutExtension();

    return juce::File::createLegalFileName (name).trim();
}

LaunchRequest LaunchRequest::parse (const juce::StringArray& args)
{
    LaunchRequest request;

    for (int i = 0; i < args.size(); ++i)
    {
        const auto& arg = args[i];

        if (arg == "--artist" || arg == "--title" || arg == "--position")
        {
            if (i + 1 >= args.size())
            {
                request.problems.add (arg + " was given no value");
                continue;
            }

            const auto& value = args[++i];

            if (arg == "--artist")
            {
                request.artist = cleanTag (value);
            }
            else if (arg == "--title")
            {
                request.title = cleanTag (value);
            }
            else
            {
                request.positionSeconds = parseSeconds (value);

                if (! request.positionSeconds.has_value() && value.trim().isNotEmpty())
                    request.problems.add ("Couldn't read a position from \"" + value + "\"");
            }
        }
        else if (arg.startsWith ("--"))
        {
            request.problems.add ("Unknown option " + arg);
        }
        else if (request.file == juce::File())
        {
            if (juce::File::isAbsolutePath (arg))
                request.file = juce::File (arg);
            else
                request.problems.add ("Not a full path: \"" + arg + "\"");
        }
        else
        {
            request.problems.add ("Extra argument ignored: \"" + arg + "\"");
        }
    }

    return request;
}

} // namespace cratesnip
