# Crash Protection Recommendation

## Scanning

Plugin scanning should run out-of-process as soon as practical.

Reason:

- Plugin scanners load plugin binaries.
- A bad plugin can crash while scanning.
- If scanning happens in the main app process, the whole app goes down.

JUCE's `PluginDirectoryScanner` has a dead-man's-pedal blacklist mechanism. JUCE
AudioPluginHost also demonstrates out-of-process scanning using a child process
and ping/coordination.

## Runtime Processing

Runtime plugin sandboxing is possible, but it is a later phase.

Running plugins in a separate process protects the host from plugin crashes, but
it introduces:

- Interprocess audio transfer.
- Extra context switches.
- More latency pressure.
- More complicated plugin editor handling.
- State synchronization complexity.

Recommendation:

1. Phase 2/3: in-process plugin processing for minimum latency.
2. Add out-of-process scanning before broad plugin scanning.
3. Add crash recovery around plugin load/unload.
4. Treat full runtime sandboxing as Phase 6, after measuring callback timing.

