# Build Instructions

For launching the current WPF app from Visual Studio, use
`docs/VisualStudioRun.md`. For publish/release packaging, use
`docs/Publishing.md`.

## Prerequisites

- Windows 10 or Windows 11.
- Visual Studio 2022 with C++ desktop workload.
- VoiceMeeter installed.
- VoiceMeeter running for runtime testing.
- JUCE under `external/JUCE` for VST3 plugin discovery.
- Optional VST2 SDK path; see `docs/VST2Workflow.md`.

This machine has Visual Studio Community 2022 installed, but that installation
currently reports as incomplete. The verified build path is Visual Studio 2019
Build Tools using Visual Studio's bundled CMake at:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
```

## Configure

From `C:\Users\torme\source\repos\Elka.VoiceMeeterFxHost`:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --preset vs2019-x64
```

If `external/JUCE` exists, CMake enables the JUCE VST3 discovery layer. If it is
missing, the callback, delay, and gain prototype still builds without plugin
hosting.

To configure VST2 hosting with a local SDK:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --preset vs2019-x64 -DELKA_VST2_SDK_PATH="D:\AudioSDKs\VST2_SDK"
```

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset debug --target ElkaVoiceMeeterFxHost.Native
```

## Run

Start VoiceMeeter first, then run:

```powershell
.\src\app-wpf\bin\Debug\net8.0-windows\win-x64\Elka.VoiceMeeterFxHost.App.exe
```

Recommended first test:

1. Select `Input`.
2. Select a VoiceMeeter section such as `VAIO`.
3. Open `Channels` and confirm delay, volume, and direct routing.
4. Open `VST`.
5. Click `Scan` and confirm the plugin host reports either the found plugin
   count or a clear error.
6. Add a plugin node and drag cables from the left endpoint through the plugin
   to the right endpoint.
