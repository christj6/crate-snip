#include "LaunchRequest.h"

#include <iostream>

namespace
{
int failures = 0;

void check (bool condition, const char* description)
{
    if (! condition)
    {
        ++failures;
        std::cout << "FAIL: " << description << "\n";
    }
}

using cratesnip::LaunchRequest;

void testFullRequest()
{
    const auto r = LaunchRequest::parse ({ "C:\\Music\\a b.flac", "--artist", "Can", "--title", "Vitamin C", "--position", "83.456000" });

    check (r.file == juce::File ("C:\\Music\\a b.flac"), "full: path");
    check (r.artist == "Can" && r.title == "Vitamin C", "full: tags");
    check (r.positionSeconds.has_value() && std::abs (*r.positionSeconds - 83.456) < 1.0e-9, "full: position");
    check (r.problems.isEmpty(), "full: no problems");
    check (r.suggestedBaseName() == "Can - Vitamin C", "full: base name");
}

void testMissingTagsFallBackToFileName()
{
    const auto r = LaunchRequest::parse ({ "C:\\Music\\track07.mp3", "--artist", "?", "--title", "" });

    check (r.artist.isEmpty(), "missing: '?' artist is empty");
    check (r.suggestedBaseName() == "track07", "missing: falls back to file name");
}

void testPositionThatIsNotANumber()
{
    for (const auto* text : { "?", "1:23", "-4", "1.2.3", "." })
    {
        const auto r = LaunchRequest::parse ({ "C:\\x.wav", "--position", text });
        check (! r.positionSeconds.has_value(), "bad position rejected");
        check (r.problems.size() == 1, "bad position reported");
    }

    const auto empty = LaunchRequest::parse ({ "C:\\x.wav", "--position", "" });
    check (! empty.positionSeconds.has_value() && empty.problems.isEmpty(), "empty position is silently absent");
}

void testIllegalFileNameCharacters()
{
    const auto r = LaunchRequest::parse ({ "C:\\x.wav", "--artist", "AC/DC", "--title", "What? \"Why\"" });
    const auto name = r.suggestedBaseName();

    check (! name.containsAnyOf ("\\/:*?\"<>|"), "illegal characters removed");
    check (name.startsWith ("AC"), "illegal characters: rest of the name kept");
}

void testOddArguments()
{
    const auto r = LaunchRequest::parse ({ "relative.wav", "C:\\a.wav", "C:\\b.wav", "--bogus", "--title" });

    check (r.file == juce::File ("C:\\a.wav"), "odd: first full path wins");
    check (r.problems.size() == 4, "odd: relative path, extra path, unknown option and missing value reported");
}
} // namespace

int runRegionAudioTests();

int main()
{
    testFullRequest();
    testMissingTagsFallBackToFileName();
    testPositionThatIsNotANumber();
    testIllegalFileNameCharacters();
    testOddArguments();
    failures += runRegionAudioTests();

    std::cout << (failures == 0 ? "All CrateSnip tests passed\n" : "CrateSnip tests FAILED\n");
    return failures == 0 ? 0 : 1;
}
