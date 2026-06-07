# Build Instructions

## Prerequisites

- Windows 10 or Windows 11.
- Visual Studio 2022 with C++ desktop workload.
- VoiceMeeter installed.
- VoiceMeeter running for runtime testing.
- JUCE under `external/JUCE` for VST3 plugin discovery.

This machine has Visual Studio Community 2022 installed, but that installation
currently reports as incomplete. The verified build path is Visual Studio 2019
Build Tools using Visual Studio's bundled CMake at:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
```

## Configure

From `C:\Users\torme\source\repos\Elka.VoiceMeeterFxHost`:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build-vs2019 -G "Visual Studio 16 2019" -A x64
```

If `external/JUCE` exists, CMake enables the JUCE VST3 discovery layer. If it is
missing, the callback, delay, and gain prototype still builds without plugin
hosting.

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build-vs2019 --config Debug
```

## Run

Start VoiceMeeter first, then run:

```powershell
.\build-vs2019\Debug\ElkaVoiceMeeterFxHost.exe
```

Recommended first test:

1. Select `Input Insert`.
2. Select `Strip 1 L/R`.
3. Click `Connect`.
4. Keep delay at `0 ms` and gain at `100%`.
5. Click `Start`.
6. Confirm sample rate, block size, and channels update.
7. Move one gain fader down/up and confirm only that channel changes.
8. Move one delay strip from `0 ms` upward and confirm only that channel delays.
9. Enable `Link faders`, move one delay strip or gain fader, and confirm the
   whole selected source group follows.
10. Change to another source group and confirm its saved delay/gain state
    appears.
11. Click `Scan VST3` and confirm the plugin host reports either the found
    plugin count or a clear error.
