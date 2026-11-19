# CrateSnip

CrateSnip is a planned Windows companion app for foobar2000 that turns parts of tracks in your music library into WAV samples.

## Workflow

1. Right-click a track in foobar2000 and choose **Cut samples…**.
2. Zoom into the waveform, mark one or more regions, and drag their edges to refine each cut.
3. Preview each region once or on a loop. Edit the suggested filenames if you like.
4. Click **Export All** to save the cuts to your default sample folder.

## First release

- Open local WAV, FLAC, and MP3 files.
- Suggest filenames such as `Artist - Title - 01.wav`, with the source filename as a fallback when tags are missing.
- Export 24-bit PCM WAVs at the source sample rate and channel count. Add a suffix when a filename is already taken.

## Implementation

CrateSnip is a standalone JUCE app with a small native foobar2000 launcher. The launcher reads the selected track and passes the playhead when that track is playing. Preview and export will share the same sample-frame boundaries. Keeping the audio reader separate from the editor leaves room to use foobar2000’s decoders later. [foobar2000 SDK](https://www.foobar2000.org/SDK) · [JUCE](https://juce.com/)

## Current state: handoff check

Right now CrateSnip is only that integration check. It opens a window that shows:

- every argument it received, exactly as received
- whether the file decodes, with its format, sample rate, channels and length
- the artist, title, position and suggested filename it worked out from those arguments
- the waveform, with an orange line at the received position

It also has **Pause foobar2000** and **Resume foobar2000** buttons, which run `foobar2000.exe /pause` and `foobar2000.exe /play`.

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

When the selected track is playing, the **Position** line and orange waveform marker show where it was when you invoked the command. Selecting a different track or opening a track while playback is stopped leaves the position empty.

If you previously set up CrateSnip through **Run service → Cut samples…**, use the new **Cut samples…** item in the main context menu. The old Run Services action launched CrateSnip with an empty playhead during playback.
