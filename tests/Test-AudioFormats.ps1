param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',

    [string] $BuildDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build')
)

$ErrorActionPreference = 'Stop'
$ffmpeg = (Get-Command ffmpeg.exe -CommandType Application).Source
$ffprobe = (Get-Command ffprobe.exe -CommandType Application).Source
$tester = Join-Path $BuildDirectory "CrateSnipTests_artefacts\$Configuration\CrateSnipTests.exe"
if (-not (Test-Path -LiteralPath $tester -PathType Leaf)) {
    throw 'Build CrateSnipTests with CRATE_SNIP_BUILD_TESTS=ON first. See docs/audio-formats.md.'
}

function New-AudioFixture ([string] $File, [string] $Format, [string[]] $EncoderOptions) {
    [pscustomobject]@{ File = $File; Format = $Format; EncoderOptions = $EncoderOptions }
}

$cases = @(
    New-AudioFixture 'pcm16.wav' 'WAV PCM 16-bit' @('-c:a', 'pcm_s16le')
    New-AudioFixture 'pcm24.wav' 'WAV PCM 24-bit' @('-c:a', 'pcm_s24le')
    New-AudioFixture 'float32.wav' 'WAV float 32-bit' @('-c:a', 'pcm_f32le')
    New-AudioFixture 'surround.wav' 'WAV PCM 24-bit 5.1' @('-c:a', 'pcm_s24le', '-ac', '6')
    New-AudioFixture 'rf64.wav' 'RF64 PCM 24-bit' @('-c:a', 'pcm_s24le', '-rf64', 'always')
    New-AudioFixture 'wave64.w64' 'Wave64 PCM 24-bit' @('-c:a', 'pcm_s24le', '-f', 'w64')
    New-AudioFixture 'ima.wav' 'WAV IMA ADPCM' @('-c:a', 'adpcm_ima_wav')
    New-AudioFixture 'pcm16.aiff' 'AIFF PCM 16-bit' @('-c:a', 'pcm_s16be')
    New-AudioFixture 'pcm24.aif' 'AIFF PCM 24-bit' @('-c:a', 'pcm_s24be', '-f', 'aiff')
    New-AudioFixture 'float32.aifc' 'AIFF-C float 32-bit' @('-c:a', 'pcm_f32be', '-f', 'aiff')
    New-AudioFixture 'lossless16.flac' 'FLAC 16-bit' @('-c:a', 'flac', '-sample_fmt', 's16')
    New-AudioFixture 'lossless24.flac' 'FLAC 24-bit at 96 kHz' @('-c:a', 'flac', '-ar', '96000', '-sample_fmt', 's32', '-bits_per_raw_sample', '24')
    New-AudioFixture 'ogg-lossless.oga' 'Ogg FLAC' @('-c:a', 'flac', '-f', 'ogg')
    New-AudioFixture 'constant.mp3' 'MP3 CBR' @('-c:a', 'libmp3lame', '-b:a', '192k')
    New-AudioFixture 'variable.mp3' 'MP3 VBR' @('-c:a', 'libmp3lame', '-q:a', '2')
    New-AudioFixture 'layer2.mp2' 'MPEG Layer II' @('-c:a', 'mp2', '-b:a', '192k')
    New-AudioFixture 'vorbis.ogg' 'Ogg Vorbis' @('-c:a', 'libvorbis')
    New-AudioFixture 'vorbis.oga' 'Ogg Vorbis' @('-c:a', 'libvorbis', '-f', 'ogg')
    New-AudioFixture 'opus.opus' 'Ogg Opus' @('-c:a', 'libopus', '-f', 'ogg')
    New-AudioFixture 'opus.ogg' 'Ogg Opus' @('-c:a', 'libopus', '-f', 'ogg')
    New-AudioFixture 'opus.webm' 'WebM Opus' @('-c:a', 'libopus', '-f', 'webm')
    New-AudioFixture 'opus.mka' 'Matroska Opus' @('-c:a', 'libopus', '-f', 'matroska')
    New-AudioFixture 'windows1.wma' 'WMA v1' @('-c:a', 'wmav1', '-ar', '44100')
    New-AudioFixture 'windows2.wma' 'WMA v2' @('-c:a', 'wmav2', '-ar', '44100')
    New-AudioFixture 'windows2.asf' 'ASF with WMA v2' @('-c:a', 'wmav2', '-ar', '44100')
    New-AudioFixture 'aac-lc.aac' 'AAC-LC in ADTS' @('-c:a', 'aac', '-profile:a', 'aac_low', '-f', 'adts')
    New-AudioFixture 'aac-lc.m4a' 'AAC-LC in M4A' @('-c:a', 'aac', '-profile:a', 'aac_low', '-f', 'ipod')
    New-AudioFixture 'aac-lc.mp4' 'AAC-LC in audio-only MP4' @('-c:a', 'aac', '-profile:a', 'aac_low', '-f', 'mp4')
    New-AudioFixture 'alac.m4a' 'Apple Lossless in M4A' @('-c:a', 'alac', '-f', 'ipod')
    New-AudioFixture 'wavpack.wv' 'WavPack lossless' @('-c:a', 'wavpack')
    New-AudioFixture 'true-audio.tta' 'True Audio' @('-c:a', 'tta')
    New-AudioFixture 'pcm.caf' 'CAF PCM' @('-c:a', 'pcm_s16le', '-f', 'caf')
    New-AudioFixture 'mulaw.au' 'AU mu-law' @('-c:a', 'pcm_mulaw', '-f', 'au')
    New-AudioFixture 'speex.spx' 'Ogg Speex' @('-c:a', 'libspeex', '-ar', '16000', '-ac', '1', '-f', 'ogg')
    New-AudioFixture 'dolby.ac3' 'Dolby AC-3' @('-c:a', 'ac3', '-f', 'ac3')
    New-AudioFixture 'dolby.eac3' 'Dolby E-AC-3' @('-c:a', 'eac3', '-f', 'eac3')
    New-AudioFixture 'dts.dts' 'DTS' @('-c:a', 'dca', '-strict', '-2', '-f', 'dts')
)

$signal = 'aevalsrc=0.15*sin(2*PI*220*t)|0.10*sin(2*PI*330*t):s=48000:d=1'
$root = Join-Path ([IO.Path]::GetTempPath()) ('CrateSnip-formats-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$rows = [ordered]@{}
$encoded = [System.Collections.Generic.List[string]]::new()

try {
    foreach ($case in $cases) {
        $path = Join-Path $root $case.File
        $row = [pscustomobject]@{
            Format = $case.Format
            Extension = [IO.Path]::GetExtension($case.File)
            Codec = ''
            Result = 'Fixture unavailable'
            Decoder = ''
            Details = ''
        }
        $rows.Add($case.File, $row)

        $encoderOptions = $case.EncoderOptions
        $diagnostic = & $ffmpeg -hide_banner -loglevel error -nostdin -f lavfi -i $signal @encoderOptions $path 2>&1
        if ($LASTEXITCODE -ne 0) {
            $row.Details = ($diagnostic | Select-Object -First 1 | Out-String).Trim()
            continue
        }

        $codec = & $ffprobe -v error -select_streams a:0 -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 $path
        if ($LASTEXITCODE -ne 0 -or -not $codec) {
            $row.Details = 'FFprobe could not verify the fixture codec.'
            continue
        }
        $row.Codec = [string]$codec
        $encoded.Add($path)
    }

    if ($encoded.Count -gt 0) {
        $arguments = $encoded.ToArray()
        $json = & $tester --probe-formats @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "The CrateSnip format probe failed with exit code $LASTEXITCODE."
        }

        $results = ($json -join [Environment]::NewLine) | ConvertFrom-Json
        foreach ($result in $results) {
            $row = $rows[$result.file]
            $row.Result = $result.status
            $row.Decoder = $result.decoder
            $row.Details = $result.details
        }
    }
    $rows.Values
}
finally {
    foreach ($case in $cases) {
        $path = Join-Path $root $case.File
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Remove-Item -LiteralPath $path
        }
    }
    if (-not (Get-ChildItem -LiteralPath $root -Force | Select-Object -First 1)) {
        Remove-Item -LiteralPath $root
    }
}
