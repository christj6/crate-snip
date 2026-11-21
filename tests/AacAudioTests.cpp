#include "AacAudio.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>

#include <iostream>

int runAacAudioTests()
{
    int failures = 0;
    const auto check = [&] (bool ok, const char* description)
    {
        if (! ok)
        {
            ++failures;
            std::cout << "FAIL: " << description << "\n";
        }
    };

    const auto folder = juce::File::createTempFile ("CrateSnipSweep");
    if (! folder.createDirectory())
    {
        check (false, "create sweep test folder");
        return failures;
    }

    const auto leftover = folder.getChildFile ("CrateSnip-AAC-leftover.tmp");
    const auto inUse = folder.getChildFile ("CrateSnip-AAC-in-use.tmp");
    const auto unrelated = folder.getChildFile ("Other-AAC-file.tmp");
    for (const auto& file : { leftover, inUse, unrelated })
        file.replaceWithText ("decoded audio");

    // Opened the way a window's decoded audio is: no sharing.
    const auto handle = CreateFileW (inUse.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE, 0,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check (handle != INVALID_HANDLE_VALUE, "open the in-use fixture");

    const auto deleted = cratesnip::deleteLeftoverDecodedAac (folder);
    check (deleted == 1 && ! leftover.exists(), "a leftover from a crashed session is deleted");
    check (inUse.exists(), "a file another window has open is left alone");
    check (unrelated.exists(), "files that aren't CrateSnip's are left alone");

    if (handle != INVALID_HANDLE_VALUE)
        CloseHandle (handle);

    folder.deleteRecursively();
    return failures;
}
