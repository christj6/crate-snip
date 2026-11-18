#include "FoobarRemote.h"

namespace cratesnip
{

namespace
{
    juce::File findFoobar()
    {
        // The installer registers foobar2000.exe under App Paths; a portable copy won't be.
        const auto registered = juce::WindowsRegistry::getValue (
            "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\foobar2000.exe\\");

        if (registered.isNotEmpty())
        {
            const juce::File file (registered.unquoted());

            if (file.existsAsFile())
                return file;
        }

        const auto fallback = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                                  .getChildFile ("foobar2000")
                                  .getChildFile ("foobar2000.exe");

        return fallback.existsAsFile() ? fallback : juce::File();
    }
} // namespace

FoobarRemote::FoobarRemote()
    : executable (findFoobar())
{
}

juce::String FoobarRemote::pause()  { return send ("/pause"); }
juce::String FoobarRemote::resume() { return send ("/play"); }

juce::String FoobarRemote::send (const juce::String& command)
{
    if (executable == juce::File())
        return "foobar2000.exe wasn't found";

    // No pipes: nothing reads foobar2000's output, so it mustn't wait on a full one.
    if (! process.start (juce::StringArray { executable.getFullPathName(), command }, 0))
        return "Couldn't start " + executable.getFullPathName();

    return {};
}

} // namespace cratesnip
