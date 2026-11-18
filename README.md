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

The first version will be a standalone JUCE app launched through foobar2000’s Run Services component. Preview and export will share the same sample-frame boundaries. Keeping the audio reader separate from the editor leaves room to use foobar2000’s decoders later. [Run Services](https://www.foobar2000.org/components/view/foo_run) · [JUCE](https://juce.com/)

An early integration check will determine whether the editor can open at the current playhead and pause foobar2000 for preview. A small native launcher can handle that handoff if needed.

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

## Adding it to foobar2000

1. Install [Run Services](https://www.foobar2000.org/components/view/foo_run) (version 0.4.5 has a 64-bit build for foobar2000 v2) and restart foobar2000.
2. Open **Preferences → Tools → Run services** and add a service named `Cut samples…` with this path, replacing `<crate-snip>` with the folder you cloned into:

   ```
   "<crate-snip>\build\CrateSnip_artefacts\Release\CrateSnip.exe" "%path%" --artist "[%artist%]" --title "[%title%]" --position "[%playback_time_seconds%]"
   ```

   The square brackets make a missing tag empty instead of `?`.
3. Right-click a track and choose the service from the **Run service** submenu.

## What the handoff check needs to answer

- [ ] Right-clicking the **playing** track: does the position arrive, and does the orange line sit where you heard it? Try a WAV or FLAC and an MP3: MP3 decoders can disagree by a few dozen milliseconds at the start.
- [ ] Right-clicking a track that **isn't playing**: is the position empty, or the playing track's position?
- [ ] Tags with quotes or unusual characters (`"`, `?`, `/`, accented letters): do the raw arguments match the tags?
- [ ] Do **Pause** and **Resume** control the running foobar2000?
- [ ] Which of your library's formats fail to decode?
