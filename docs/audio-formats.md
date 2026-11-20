# Audio format compatibility

The Windows Release build passed 18 of 37 codec and container combinations in this sweep. Each row represents one generated audio file.

CrateSnip’s foobar2000 launcher passes the file path, track metadata, and the playhead when available. The standalone editor uses Windows Media Foundation for AAC and JUCE’s readers for the other supported formats. AAC is decoded to a temporary audio file when opened, then the waveform, preview, and export read from it. Windows removes the temporary file when the editor closes. Windows N editions need the Media Feature Pack for AAC. [Windows AAC support](https://learn.microsoft.com/en-us/windows/win32/medfound/supported-media-formats-in-media-foundation) · [Media Feature Pack](https://support.microsoft.com/en-us/windows/experience/platform-variants/media-feature-pack-for-windows-n)

## Tested inputs

**Pass** means CrateSnip opened the file, rendered a selected region through the preview code, and exported a readable 24-bit WAV. The probe checks that the WAV has the expected length, sample rate, and channel count. It compares audio samples for up to the first two channels. The test runs without opening the editor or using an audio device.

**No decoder** means CrateSnip has no reader for that extension. **Could not open** means a reader handles the extension but could not return a usable audio stream for the test file.

| Audio format | Extension | Result | Reader |
| --- | --- | --- | --- |
| WAV PCM 16-bit | `.wav` | Pass | WAV file |
| WAV PCM 24-bit | `.wav` | Pass | WAV file |
| WAV float 32-bit | `.wav` | Pass | WAV file |
| WAV PCM 24-bit 5.1 | `.wav` | Pass | WAV file |
| RF64 PCM 24-bit | `.wav` | Could not open | WAV file |
| Wave64 PCM 24-bit | `.w64` | No decoder | — |
| WAV IMA ADPCM | `.wav` | Could not open | WAV file |
| AIFF PCM 16-bit | `.aiff` | Pass | AIFF file |
| AIFF PCM 24-bit | `.aif` | Pass | AIFF file |
| AIFF-C float 32-bit | `.aifc` | No decoder | — |
| FLAC 16-bit | `.flac` | Pass | FLAC file |
| FLAC 24-bit at 96 kHz | `.flac` | Pass | FLAC file |
| Ogg FLAC | `.oga` | No decoder | — |
| MP3 CBR | `.mp3` | Pass | MP3 file |
| MP3 VBR | `.mp3` | Pass | MP3 file |
| MPEG Layer II | `.mp2` | No decoder | — |
| Ogg Vorbis | `.ogg` | Pass | Ogg-Vorbis file |
| Ogg Vorbis | `.oga` | No decoder | — |
| Ogg Opus | `.opus` | Pass | Opus file |
| Ogg Opus | `.ogg` | Could not open | Ogg-Vorbis file |
| WebM Opus | `.webm` | No decoder | — |
| Matroska Opus | `.mka` | No decoder | — |
| WMA v1 | `.wma` | Pass | Windows Media |
| WMA v2 | `.wma` | Pass | Windows Media |
| ASF with WMA v2 | `.asf` | Pass | Windows Media |
| AAC-LC in ADTS | `.aac` | Pass | AAC (Windows Media Foundation) |
| AAC-LC in M4A | `.m4a` | Pass | AAC (Windows Media Foundation) |
| AAC-LC in audio-only MP4 | `.mp4` | Pass | AAC (Windows Media Foundation) |
| Apple Lossless in M4A | `.m4a` | Could not open | AAC (Windows Media Foundation) |
| WavPack lossless | `.wv` | No decoder | — |
| True Audio | `.tta` | No decoder | — |
| CAF PCM | `.caf` | No decoder | — |
| AU mu-law | `.au` | No decoder | — |
| Ogg Speex | `.spx` | No decoder | — |
| Dolby AC-3 | `.ac3` | No decoder | — |
| Dolby E-AC-3 | `.eac3` | No decoder | — |
| DTS | `.dts` | No decoder | — |

The RF64 fixture was recognized as WAV, but the reader did not report a usable audio stream. WMA results use the Windows Media reader available on the test machine.

## Rerun the sweep

From the repository root, with `ffmpeg.exe` and `ffprobe.exe` on `PATH`, run the following in PowerShell. The script generates short test tones and removes them when finished. CrateSnip itself does not use FFmpeg for decoding.

```powershell
cmake -S . -B build -DCRATE_SNIP_BUILD_TESTS=ON
cmake --build build --config Release --target CrateSnipTests
.\tests\Test-AudioFormats.ps1 | Format-Table -AutoSize
```

To check a specific file, pass its path directly to the test executable.

```powershell
.\build\CrateSnipTests_artefacts\Release\CrateSnipTests.exe --probe-formats 'C:\Music\sample.m4a'
```
