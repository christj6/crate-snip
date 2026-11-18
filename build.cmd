@echo off
rem Builds Debug and Release, stopping at the first that fails: cmake --build alone only builds Debug.
rem Configures the build folder first if it doesn't exist yet.
if not exist "%~dp0build\CMakeCache.txt" cmake -S "%~dp0." -B "%~dp0build" || exit /b 1
cmake --build "%~dp0build" --config Debug --parallel || exit /b 1
cmake --build "%~dp0build" --config Release --parallel
