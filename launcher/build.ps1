param(
    [Parameter(Mandatory)]
    [string] $SdkDirectory,

    [ValidateSet('v143', 'v145')]
    [string] $PlatformToolset
)

$ErrorActionPreference = 'Stop'

$sdkRoot = (Resolve-Path -LiteralPath $SdkDirectory).ProviderPath
if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot 'foobar2000\SDK\foobar2000.h'))) {
    throw 'SdkDirectory must point to the extracted foobar2000 SDK.'
}

$buildRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'build'
$app = Join-Path $buildRoot 'CrateSnip_artefacts\Release\CrateSnip.exe'
if (-not (Test-Path -LiteralPath $app -PathType Leaf)) {
    throw 'Run build.cmd to build the Release app first.'
}

$vswhere = Join-Path (Get-Item 'Env:ProgramFiles(x86)').Value 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw 'Visual Studio with the desktop C++ workload is required.'
}
if (-not $PlatformToolset) {
    $version = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
    $PlatformToolset = if ([version]$version -ge [version]'18.0') { 'v145' } else { 'v143' }
}

$msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
$project = Join-Path $PSScriptRoot 'foo_cratesnip.vcxproj'
& $msbuild $project /m /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 "/p:FoobarSdkDir=$sdkRoot" "/p:PlatformToolset=$PlatformToolset"
if ($LASTEXITCODE -ne 0) {
    throw "Launcher build failed with exit code $LASTEXITCODE."
}

$launcherOutput = Join-Path $buildRoot 'foobar2000'
$dll = Join-Path $launcherOutput 'foo_cratesnip.dll'
$zip = Join-Path $launcherOutput 'foo_cratesnip.zip'
$package = Join-Path $launcherOutput 'foo_cratesnip.fb2k-component'
Compress-Archive -LiteralPath $dll,$app -DestinationPath $zip -Force
Move-Item -LiteralPath $zip -Destination $package -Force
Write-Output $package
