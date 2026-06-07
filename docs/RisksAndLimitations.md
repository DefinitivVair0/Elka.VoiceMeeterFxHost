# Risks and Limitations

## VoiceMeeter Callback Ownership

Only one app can own a callback stream for a given mode. Registration can fail
if another application already registered that stream.

## Plugin Safety

Third-party plugins run arbitrary native code. If a plugin is hosted in-process
and crashes, the host process can crash.

## Real-Time Behavior

Some plugins allocate, lock, start threads, touch disk, or perform license checks
from their processing path. That can cause dropouts in a VoiceMeeter callback.

## Plugin Latency

Many effects report latency. VoiceMeeter's callback API does not provide a
host-style latency compensation contract for this app. We need a deliberate
latency policy before using lookahead limiters, linear-phase EQs, and similar
plugins.

## VST2

VST2 support is a legal/dependency issue. Steinberg discontinued the public VST2
SDK and no longer issues new VST2 agreements. JUCE can host VST2 only when the
project is built with valid VST2 SDK headers available to the developer.

Recommendation: implement VST3 first, then add VST2 only if the local SDK/legal
position is settled.

## Main Mode Complexity

Main mode can replace output busses and inspect both inputs and outputs. It is
powerful, but it is easier to make a routing mistake than in Input Insert or
Output Insert mode.

Recommendation: validate Phase 1 in Input Insert first, then Output Insert, then
Main.

