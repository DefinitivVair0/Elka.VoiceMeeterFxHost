# Dependencies

## Phase 1

- Windows SDK.
- VoiceMeeter installed on the machine.
- VoiceMeeter Remote DLL loaded dynamically at runtime:
  - `VoicemeeterRemote64.dll` for x64 builds.
  - `VoicemeeterRemote.dll` for x86 builds.

No VoiceMeeter import library is required for Phase 1.

## Phase 2+

- JUCE 8 or newer recommended.
- VST3 hosting/discovery via JUCE.
- VST2 hosting only if valid VST2 SDK headers are available locally. The build
  expects a folder containing `pluginterfaces\vst2.x\aeffect.h`; see
  `docs/VST2Workflow.md`.

The project is organized so JUCE can be added under:

```text
external/JUCE
```

The current local project uses `external/JUCE` when present. JUCE is a source
dependency used at build time.
