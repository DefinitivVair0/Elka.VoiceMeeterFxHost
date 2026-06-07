# Elka VoiceMeeter FX Host

WPF control app with a native C++ VoiceMeeter callback/VST bridge.

Open this in Visual Studio:

```text
Elka.VoiceMeeterFxHost.sln
```

The solution contains the WPF app as the startup project. Building or launching
the WPF app also builds the native bridge DLL:

```text
build-vs2019\Debug\ElkaVoiceMeeterFxHost.Native.dll
```

The old Win32 prototype UI has been removed from the build. The native C++ side
is now only the low-latency backend for VoiceMeeter callback processing, routing,
and VST hosting.

## Current App Path

```text
src\app-wpf\bin\Debug\net8.0-windows\win-x64\Elka.VoiceMeeterFxHost.App.exe
```

## Current Features

- WPF UI using the same dark visual language as the VoiceMeeter Delay app.
- Automatic native callback startup through the WPF app.
- Input, output, and main callback mode selection.
- Per-channel enable, delay, and volume.
- Input-to-output routing implemented in the native C++ engine.
- Route mute-normal behavior matching the VoiceMeeter Delay app.
- Native VST3 scan list.
- WPF Add Node loads a native VST node with default stereo routing.
- WPF node controls for bypass, editor open, and remove.

## Architecture

- `src/app-wpf`: WPF desktop UI.
- `src/native_api`: exported C API consumed by WPF.
- `src/engine`: realtime callback processing.
- `src/plugins`: JUCE VST3 hosting.
- `src/voicemeeter`: VoiceMeeter Remote API loading and callback registration.
