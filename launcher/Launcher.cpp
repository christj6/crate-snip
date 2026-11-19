#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/shared/shared.h>
#include <pfc/string-conv-lite.h>

#include <charconv>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>

DECLARE_COMPONENT_VERSION ("CrateSnip launcher", "0.1.0",
                           "Opens the selected track in CrateSnip with its current playback position.");
VALIDATE_COMPONENT_FILENAME ("foo_cratesnip.dll");

namespace
{
const GUID launchCommandId = { 0x57d36515, 0x51f3, 0x4ae8, { 0xb2, 0x06, 0x14, 0x5b, 0x5f, 0xa6, 0xe4, 0xc1 } };

void appendArgument (std::wstring& command, std::wstring_view argument)
{
    if (! command.empty())
        command.push_back (L' ');

    // CrateSnip uses CommandLineToArgvW. Escape quotes and the backslashes they follow.
    command.push_back (L'"');
    size_t backslashes = 0;
    for (const auto character : argument)
    {
        if (character == L'\\')
        {
            ++backslashes;
            continue;
        }

        command.append (character == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        command.push_back (character);
        backslashes = 0;
    }
    command.append (backslashes * 2, L'\\');
    command.push_back (L'"');
}

std::wstring positionFor (const metadb_handle_ptr& track)
{
    const auto playback = playback_control::get();
    metadb_handle_ptr playingTrack;
    if (! playback->get_now_playing (playingTrack) || playingTrack != track)
        return {};

    const auto position = playback->playback_get_position();
    if (! std::isfinite (position) || position < 0)
        return {};

    char seconds[64];
    const auto result = std::to_chars (seconds, seconds + sizeof (seconds), position,
                                       std::chars_format::fixed, 6);
    if (result.ec != std::errc())
        return {};

    return { seconds, result.ptr };
}

void openTrack (metadb_handle_list_cref selection)
{
    if (selection.get_count() != 1)
    {
        popup_message::g_show ("Select one track to open in CrateSnip.", "CrateSnip");
        return;
    }

    const auto track = selection[0];
    const auto position = positionFor (track);
    pfc::string8 trackPath;
    if (track->get_subsong_index() != 0 || ! filesystem::g_get_native_path (track->get_path(), trackPath))
    {
        popup_message::g_show ("CrateSnip requires a local file containing a single track.", "CrateSnip");
        return;
    }

    pfc::string8 modulePath;
    if (! uGetModuleFileName (core_api::get_my_instance(), modulePath))
    {
        popup_message::g_show ("Could not locate the CrateSnip launcher.", "CrateSnip");
        return;
    }

    auto executable = std::filesystem::path (pfc::wideFromUTF8 (modulePath.c_str()).c_str());
    executable.replace_filename (L"CrateSnip.exe");

    pfc::string8 artist, title;
    track->format_title_legacy (nullptr, artist, "[$meta(artist)]", nullptr);
    track->format_title_legacy (nullptr, title, "[$meta(title)]", nullptr);

    std::wstring command;
    appendArgument (command, executable.native());
    appendArgument (command, pfc::wideFromUTF8 (trackPath.c_str()).c_str());
    appendArgument (command, L"--artist");
    appendArgument (command, pfc::wideFromUTF8 (artist.c_str()).c_str());
    appendArgument (command, L"--title");
    appendArgument (command, pfc::wideFromUTF8 (title.c_str()).c_str());
    if (! position.empty())
    {
        appendArgument (command, L"--position");
        appendArgument (command, position);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof (startup);
    PROCESS_INFORMATION process{};
    if (! CreateProcessW (executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                          nullptr, nullptr, &startup, &process))
    {
        pfc::string_formatter message;
        message << "Could not start CrateSnip.exe next to the launcher (Windows error " << GetLastError() << ").";
        popup_message::g_show (message, "CrateSnip");
        return;
    }

    CloseHandle (process.hThread);
    CloseHandle (process.hProcess);
}

FB2K_DECLARE_CONTEXT_MENU_ITEM (openTrack, "Cut samples…", launchCommandId, contextmenu_groups::root,
                               "Open the selected track in CrateSnip.", 0);
}
