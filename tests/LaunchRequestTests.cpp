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
    check (r.suggestedFilename (1) == "can-vitaminC-chunk1.wav", "full: suggested filename");
}

void testMissingTagsFallBackToFileName()
{
    const auto r = LaunchRequest::parse ({ "C:\\Music\\Track 07.mp3", "--artist", "?", "--title", "" });

    check (r.artist.isEmpty(), "missing: '?' artist is empty");
    check (r.suggestedFilename (3) == "track07-chunk3.wav", "missing: falls back to the source filename");
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
    check (r.suggestedFilename (1) == "acDc-whatWhy-chunk1.wav", "punctuation separates words");
}

void testCamelCaseFilenames()
{
    LaunchRequest request;
    request.artist = "The Beatles";
    request.title = "Tomorrow Never Knows";
    check (request.suggestedFilename (1) == "beatles-tomorrowNeverKnows-chunk1.wav", "Beatles example");
    check (request.suggestedFilename (2) == "beatles-tomorrowNeverKnows-chunk2.wav", "second chunk");

    request.artist = "tHE   Heavy";
    request.title = "The Big Bad Wolf";
    check (request.suggestedFilename (10) == "heavy-theBigBadWolf-chunk10.wav", "leading artist article is case-insensitive");

    request.artist = "Cage the Elephant";
    request.title = "Ain't No Rest";
    check (request.suggestedFilename (1) == "cageTheElephant-aintNoRest-chunk1.wav", "interior artist article and apostrophe");

    request.artist = juce::String::fromUTF8 ("Beyonc\xc3\xa9");
    request.title = juce::String::fromUTF8 ("D\xc3\x89" "J\xc3\x80 VU");
    check (request.suggestedFilename (1) == juce::String::fromUTF8 ("beyonc\xc3\xa9-d\xc3\xa9" "j\xc3\xa0Vu-chunk1.wav"),
           "Unicode tags and casing");

    request.artist = juce::String::repeatedString ("Artist ", 40);
    request.title = "Song Name";
    const auto longName = request.suggestedFilename (100);
    check (longName.length() <= 132 && longName.endsWith ("-songName-chunk100.wav"), "long names preserve both tags and chunk suffix");
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
    testCamelCaseFilenames();
    testOddArguments();
    failures += runRegionAudioTests();

    std::cout << (failures == 0 ? "All CrateSnip tests passed\n" : "CrateSnip tests FAILED\n");
    return failures == 0 ? 0 : 1;
}
