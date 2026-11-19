# CrateSnip

CrateSnip is a Windows companion app for foobar2000 that turns parts of tracks in your music library into WAV samples.

## Workflow

1. Right-click a track in foobar2000 and choose **Cut samples…**.
2. Zoom with the mouse wheel or the zoom buttons. Pan with the scrollbar or **Shift** + wheel. **Playhead** returns to the point captured from foobar2000.
3. Drag across the waveform to select a region. Drag either edge to refine it.
4. Click **Preview** to hear the selection once. Use **Pause foobar2000** if the track is still playing in the player.
5. Click **Export WAV** and choose where to save the cut. The save dialog suggests an artist/title filename.

## First release

- Open local WAV, FLAC, and MP3 files.
- Suggest filenames such as `Artist - Title - 01.wav`, with the source filename as a fallback when tags are missing.
- Export 24-bit PCM WAVs at the source sample rate and channel count. Add a suffix when a filename is already taken.

## Implementation

CrateSnip is a standalone JUCE app with a small native foobar2000 launcher. The launcher reads the selected track and passes the playhead when that track is playing. Preview and export share the same sample-frame boundaries. Keeping the audio reader separate from the editor leaves room to use foobar2000’s decoders later. [foobar2000 SDK](https://www.foobar2000.org/SDK) · [JUCE](https://juce.com/)

Preview uses the default stereo output. The editor currently handles one region per track window.

## Building

Needs Visual Studio 2022 or later (C++ desktop workload) and CMake 3.22+. JUCE is a submodule:

```
git submodule update --init
build.cmd
```

`build.cmd` configures `build\` on first use and builds Debug and Release. The app is `build\CrateSnip_artefacts\Release\CrateSnip.exe`. To build the tests as well, configure with `cmake -S . -B build -DCRATE_SNIP_BUILD_TESTS=ON` and run `build\CrateSnipTests_artefacts\Release\CrateSnipTests.exe`.

## Building the foobar2000 launcher

Build the Release app, then download and extract the [foobar2000 SDK](https://www.foobar2000.org/SDK). Run the following command from the repo root in PowerShell.

```powershell
.\launcher\build.ps1 -SdkDirectory 'C:\path\to\extracted-sdk'
```

The script creates `build\foobar2000\foo_cratesnip.fb2k-component` with the x64 launcher and the Release app.

## Adding it to foobar2000

1. In 64-bit foobar2000 v2, open **Preferences → Components → Install…** and select `foo_cratesnip.fb2k-component`.
2. Apply the change and restart foobar2000.
3. Right-click one local track and choose **Cut samples…** from foobar2000’s context menu.

When the selected track is playing, the editor opens around an orange marker at the position captured when you invoked the command. Selecting a different track or opening a track while playback is stopped opens the full waveform.

If you previously set up CrateSnip through **Run service → Cut samples…**, use the new **Cut samples…** item in the main context menu. The old Run Services action launched CrateSnip with an empty playhead during playback.
