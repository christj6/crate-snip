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
