@echo off
rem Builds Debug and Release, stopping at the first that fails: cmake --build alone only builds Debug.
rem Configures the build folder first if it doesn't exist yet.
if not exist "%~dp0build\CMakeCache.txt" cmake -S "%~dp0." -B "%~dp0build" || exit /b 1
cmake --build "%~dp0build" --config Debug --parallel || exit /b 1
cmake --build "%~dp0build" --config Release --parallel || exit /b 1

rem If the foobar2000 component is installed, refresh its copy of the app. foobar2000 only loads
rem the launcher DLL, so the exe can be replaced while it runs, but not while a CrateSnip window is open.
set "installed=%APPDATA%\foobar2000-v2\user-components-x64\foo_cratesnip"
if not exist "%installed%\CrateSnip.exe" exit /b 0
copy /y "%~dp0build\CrateSnip_artefacts\Release\CrateSnip.exe" "%installed%\CrateSnip.exe" >nul
if errorlevel 1 (
    echo build.cmd: Couldn't update foobar2000's copy of CrateSnip.exe. Close any CrateSnip windows and run build.cmd again.
    exit /b 1
)
echo build.cmd: Updated foobar2000's copy of CrateSnip.exe.
