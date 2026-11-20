# CrateSnip

CrateSnip is a Windows companion app for foobar2000 that turns parts of tracks in your music library into WAV samples.

## Workflow

1. Right-click a track in foobar2000 and choose **Cut samples…**.
2. Zoom with the mouse wheel or the zoom buttons. Pan with the scrollbar or **Shift** + wheel. **Playhead** returns to the point captured from foobar2000.
3. Drag across the waveform to add a cut. Grab either edge of an existing cut on the waveform and drag it to move its start or end. Hold **Shift** while starting a drag on an edge to create a new cut there.
4. Click **Preview** to hear the selected cut once. Turn on **Loop preview** to repeat it while you adjust its edges. Use **Pause foobar2000** if the track is still playing in the player.
5. Edit the selected cut’s **WAV filename** or keep its numbered suggestion. The `.wav` suffix is fixed. Chunk numbers follow creation order.
6. Choose an **Export folder** and click **Export All**. CrateSnip remembers the folder for future tracks.

## First release

- Open local PCM WAV and AIFF, FLAC, MP3, Ogg Vorbis, Opus, and WMA files. See the [tested audio formats](docs/audio-formats.md) for supported variants.
- Suggest lower camel case filenames such as `beatles-tomorrowNeverKnows-chunk1.wav`. A leading “The” is dropped from artist names. Symbols inside a word are dropped (`A$AP Rocky` becomes `aapRocky`), while separators such as `/` and `-` start a new word (`AC/DC` becomes `acDc`). If either tag is missing, the suggestion uses the source filename.
- Export 24-bit PCM WAVs at the source sample rate and channel count. Add a suffix when a filename is already taken.

## Implementation

CrateSnip is a standalone JUCE app with a small native foobar2000 launcher. The launcher reads the selected track and passes the playhead when that track is playing. Preview and export share the same sample-frame boundaries. Keeping the audio reader separate from the editor leaves room to use foobar2000’s decoders later. [foobar2000 SDK](https://www.foobar2000.org/SDK) · [JUCE](https://juce.com/)

Preview uses the default stereo output. Each track window keeps its own regions until it closes. CrateSnip asks before discarding markers or cancelling an active export when you close the window.

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
