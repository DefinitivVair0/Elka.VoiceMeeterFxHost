# VoiceMeeter Callback Analysis

## API Functions

The VoiceMeeter Remote Audio Callback API consists of:

- `VBVMR_AudioCallbackRegister()`
- `VBVMR_AudioCallbackStart()`
- `VBVMR_AudioCallbackStop()`
- `VBVMR_AudioCallbackUnregister()`

The app must also use the regular remote API login/logout flow:

- `VBVMR_Login()`
- `VBVMR_Logout()`

## Callback Modes

VoiceMeeter exposes three callback modes:

- Input Insert: pre-strip insert for VoiceMeeter inputs.
- Output Insert: pre-master insert for VoiceMeeter busses.
- Main: all inputs and all outputs, with writable VoiceMeeter outputs.

## Callback Commands

Important callback commands:

- `VBVMR_CBCOMMAND_STARTING`
- `VBVMR_CBCOMMAND_ENDING`
- `VBVMR_CBCOMMAND_CHANGE`
- `VBVMR_CBCOMMAND_BUFFER_IN`
- `VBVMR_CBCOMMAND_BUFFER_OUT`
- `VBVMR_CBCOMMAND_BUFFER_MAIN`

`STARTING` and `CHANGE` provide sample rate and block size information.
Buffer commands provide the real-time audio buffer.

## Buffer Format

VoiceMeeter provides channel-separated `float` buffers:

```cpp
float* audiobuffer_r[128]; // read/input channels
float* audiobuffer_w[128]; // write/output channels
```

Each pointer contains `audiobuffer_nbs` samples of 32-bit floating-point audio.
The buffers are not interleaved.

## Potato Channel Counts

VoiceMeeter Potato channel organization:

- Input Insert: 34 channels.
- Output Insert: 64 channels.
- Main read side: 34 input channels + 64 output channels.
- Main write side: 64 output channels.

For Main mode passthrough, the read offset for current bus output channels can
be calculated as:

```cpp
outputReadOffset = audiobuffer_nbi - audiobuffer_nbo;
```

This works for Standard, Banana, and Potato based on the documented layouts.

## Potato Source Ranges

Input Insert source groups:

| Source | Start | Count |
| --- | ---: | ---: |
| Strip 1 | 0 | 2 |
| Strip 2 | 2 | 2 |
| Strip 3 | 4 | 2 |
| Strip 4 | 6 | 2 |
| Strip 5 | 8 | 2 |
| Virtual Input 1 | 10 | 8 |
| Virtual AUX | 18 | 8 |
| Virtual VAIO 3 | 26 | 8 |

The UI exposes those as source groups. Choosing a source opens its channel
lanes: two lanes for stereo strips and eight lanes for virtual inputs. Each
lane has delay on the left and gain on the right.

Output Insert and Main output source groups:

| Source | Start | Count |
| --- | ---: | ---: |
| Bus A1 | 0 | 8 |
| Bus A2 | 8 | 8 |
| Bus A3 | 16 | 8 |
| Bus A4 | 24 | 8 |
| Bus A5 | 32 | 8 |
| Bus B1 | 40 | 8 |
| Bus B2 | 48 | 8 |
| Bus B3 | 56 | 8 |

The UI exposes those as bus groups. Choosing a bus opens eight channel lanes.

The current prototype copies the full callback stream through unchanged, then
applies configured delay and gain to every enabled channel. The selected source
group only controls which stored channel settings the UI displays and edits.

Delay processing uses preallocated circular buffers. Delay memory is prepared
before `VBVMR_AudioCallbackStart()` using VoiceMeeter's configured sample rate
from `Option.sr`, with a fallback of `48000 Hz`.

This is intentionally compatible with the later VST design:

- A mono plugin chain can process a `count = 1` target.
- A stereo plugin chain can process a `count = 2` target.
- Multichannel-aware processing can process a larger target, such as an 8-channel
  bus or virtual input block.

## Real-Time Restrictions

The callback is time-critical and non-reentrant. The callback must not:

- Allocate memory.
- Do file I/O.
- Log.
- Touch UI.
- Wait on locks or OS synchronization primitives.
- Call APIs that might block.

Phase 1 follows this by using only pre-existing state and atomics inside the
audio callback.

## Asynchronous Processing

Asynchronous processing is not suitable for the real-time audio path because
VoiceMeeter expects the processed buffer before the callback returns. A worker
thread can be used for UI, scanning, loading, saving, and preparation, but the
audio block itself must be processed synchronously inside the callback.
