# Run in Visual Studio

## Prerequisites

- Windows 10 or Windows 11.
- Visual Studio 2022 with:
  - `.NET desktop development`
  - `Desktop development with C++`
  - CMake tools for Windows
- .NET 8 Desktop Runtime installed on the target PC.
- VoiceMeeter installed and running for audio callback testing.
- `external/JUCE` present when VST3 hosting is needed.

## Open the Solution

Open this file in Visual Studio:

```text
C:\Users\torme\source\repos\Elka.VoiceMeeterFxHost\Elka.VoiceMeeterFxHost.sln
```

Set `Elka.VoiceMeeterFxHost.App` as the startup project if Visual Studio does
not pick it automatically.

## Build and Launch

Use `Debug` and `x64`.

When the WPF project builds, MSBuild also runs CMake for the native bridge and
copies the native DLL beside the WPF EXE:

```text
src\app-wpf\bin\Debug\net8.0-windows\win-x64\Elka.VoiceMeeterFxHost.App.exe
src\app-wpf\bin\Debug\net8.0-windows\win-x64\ElkaVoiceMeeterFxHost.Native.dll
```

Start VoiceMeeter first, then press `F5` in Visual Studio.

## First Test

1. Open the app.
2. Select `Input`.
3. Select a VoiceMeeter input section such as `VAIO`.
4. Use `Channels` to verify delay, volume, and direct routing.
5. Use `VST` to add a plugin node and drag cables from the left endpoint pins
   through the plugin to the right endpoint pins.
6. Right-click an endpoint card to switch between `Stereo`, `Advanced / Full`,
   and route hue colors.
7. Right-click a VST node to open the editor, bypass it, change pin layout, add
   sidechain input, or remove it.

The `Input`, `Output`, and `Main` buttons switch the visible canvas. They should
not disable audio already running on another side.
