# Elka VoiceMeeter FX Host Instructions

This repo builds a WPF desktop control app with a native C++ VoiceMeeter callback
and VST3 host backend. The UI follows the same ELKA dark color scheme used by
`ELKA.Delay`: teal primary accent, warm amber active routing state, green route
state, and dark panel surfaces.

## Development Rules

- Keep the realtime audio path in native C++.
- Keep the WPF layer as control, routing, editor launch, scan, and visualization.
- Do not allocate, log, scan plugins, open files, or touch UI from the audio
  callback.
- Keep VST routing state modular so this can later merge into ELKA Audio Control.
- Treat `ELKA.Delay` as the UI and routing reference, not as code to blindly
  copy.

## Main Entry Points

- Solution: `Elka.VoiceMeeterFxHost.sln`
- WPF app: `src/app-wpf/Elka.VoiceMeeterFxHost.App.csproj`
- Native bridge: `src/native_api/FxHostNativeApi.cpp`
- Realtime engine: `src/engine/RealtimeEngine.cpp`
- JUCE plugin host: `src/plugins/PluginHostLayer.cpp`
- Theme: `src/app-wpf/Themes/ElkaTheme.xaml`

## Documentation

- Visual Studio run guide: `docs/VisualStudioRun.md`
- Publish and GitHub release guide: `docs/Publishing.md`
- Existing architecture and API notes are in `docs/`.

## Artwork

- App icon: `src/app-wpf/Assets/VoicemeeterDelay.ico`
- Header icon preview: `src/app-wpf/Assets/VoicemeeterDelayIconPreview.png`
- README/GitHub social preview: `src/app-wpf/Assets/ElkaVoiceMeeterFxHostSocialPreview.png`
- Original Delay social preview reference: `src/app-wpf/Assets/VoicemeeterDelaySocialPreview.reference.png`
