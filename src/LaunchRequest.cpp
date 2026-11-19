#include "LaunchRequest.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace cratesnip
{

namespace
{
    juce::String mapCase (std::wstring_view text, DWORD flags)
    {
        const auto length = static_cast<int> (text.size());
        const auto capacity = LCMapStringEx (LOCALE_NAME_INVARIANT, flags, text.data(), length,
                                              nullptr, 0, nullptr, nullptr, 0);
        if (capacity > 0)
        {
            std::wstring mapped (static_cast<size_t> (capacity), L'\0');
            if (LCMapStringEx (LOCALE_NAME_INVARIANT, flags, text.data(), length, mapped.data(), capacity,
                               nullptr, nullptr, 0) != 0)
                return juce::String (mapped.data(), mapped.size());
        }
        return juce::String (text.data(), text.size());
    }

    juce::String toFilenameSegment (const juce::String& value, bool skipArtistArticle)
    {
        const std::wstring_view text (value.toWideCharPointer());
        if (text.empty())
            return {};

        std::vector<WORD> types (text.size());
        if (! GetStringTypeW (CT_CTYPE1, text.data(), static_cast<int> (text.size()), types.data()))
            return {};

        juce::StringArray words;
        std::wstring word;
        const auto addWord = [&]
        {
            if (! word.empty())
            {
                words.add (juce::String (word.data(), word.size()));
                word.clear();
            }
        };
        const auto isAlphanumeric = [&] (size_t i)
        {
            return i < text.size() && (types[i] & (C1_ALPHA | C1_DIGIT)) != 0;
        };

        for (size_t i = 0; i < text.size(); ++i)
        {
            const auto character = text[i];
            if (character == L'\'' || character == L'\u2019')
                continue;

            // A symbol inside a word is part of it (A$AP, P!nk), not a break between words.
            // Separators such as AC/DC and Jay-Z still split.
            if ((types[i] & C1_PUNCT) != 0 && i > 0 && isAlphanumeric (i - 1) && isAlphanumeric (i + 1)
                && std::wstring_view (L"/\\-_.,:;&+").find (character) == std::wstring_view::npos)
                continue;
            if ((types[i] & (C1_SPACE | C1_BLANK | C1_PUNCT | C1_CNTRL)) != 0)
                addWord();
            else
                word.push_back (character);
        }
        addWord();

        const auto start = skipArtistArticle && words.size() > 1 && words[0].equalsIgnoreCase ("the") ? 1 : 0;
        juce::String result;
        for (int i = start; i < words.size(); ++i)
        {
            const auto lower = mapCase (words[i].toWideCharPointer(), LCMAP_LOWERCASE);
            if (result.isEmpty())
                result = lower;
            else
                result << mapCase (lower.substring (0, 1).toWideCharPointer(), LCMAP_UPPERCASE) << lower.substring (1);
        }
        return result;
    }

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

juce::String LaunchRequest::suggestedFilename (unsigned chunkNumber) const
{
    const auto suffix = "-chunk" + juce::String (chunkNumber);
    const auto limit = 128 - suffix.length();
    const auto artistPart = juce::File::createLegalFileName (toFilenameSegment (artist, true));
    const auto titlePart = juce::File::createLegalFileName (toFilenameSegment (title, false));

    if (artistPart.isNotEmpty() && titlePart.isNotEmpty())
    {
        const auto artistLength = juce::jmin (artistPart.length(), juce::jmax (limit / 2, limit - 1 - titlePart.length()));
        return artistPart.substring (0, artistLength) + "-"
               + titlePart.substring (0, limit - artistLength - 1) + suffix + ".wav";
    }

    const auto fallback = juce::File::createLegalFileName (toFilenameSegment (file.getFileNameWithoutExtension(), false));
    return (fallback.isEmpty() ? juce::String ("sample") : fallback.substring (0, limit)) + suffix + ".wav";
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
