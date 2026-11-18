#pragma once

#include <juce_core/juce_core.h>

namespace cratesnip
{

/** Sends playback commands to a running foobar2000 by starting foobar2000.exe with a switch such
    as /pause. The new process hands the command to the running instance and exits.
*/
class FoobarRemote
{
public:
    FoobarRemote();

    /** Where foobar2000.exe was found, or an empty File if it wasn't. */
    const juce::File& getExecutable() const noexcept { return executable; }

    /** Returns an empty string on success, otherwise what went wrong. */
    juce::String pause();
    juce::String resume();

private:
    juce::String send (const juce::String& command);

    juce::File executable;
    juce::ChildProcess process;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FoobarRemote)
};

} // namespace cratesnip
