# Elka VoiceMeeter FX Host VFX Text Commands

These are MacroButtons `SendText` examples for the app's VBAN-TEXT control.

`vban1` is the MacroButtons VBAN-TEXT output slot. The slot's stream name and UDP port must match the app's VBAN settings. The default app settings are port `6981` and stream `Command1`.

## Basics

The command prefix is `VFX`.

`Strip(...)` controls input endpoints. Numbers are zero-based:

```text
VFX.Strip(0)
```

`Bus(...)` controls output buses. Numbers are zero-based, or bus labels can be used:

```text
VFX.Bus(0)
VFX.Bus(A1)
VFX.Bus(B1)
```

`.Ch(...)` is one-based:

```text
Ch(1)
Ch(1-2)
Ch(1,3,5)
Ch(All)
Ch(*)
```

For Potato input strips:

```text
Strip(0) = Hardware In 1
Strip(1) = Hardware In 2
Strip(2) = Hardware In 3
Strip(3) = Hardware In 4
Strip(4) = Hardware In 5
Strip(5) = VAIO
Strip(6) = AUX
Strip(7) = VAIO3
```

## Input Strip Commands

Enable or disable delay/volume processing on a source channel:

```text
SendText("vban1", VFX.Strip(0).Ch(1).Enable=1;);
SendText("vban1", VFX.Strip(0).Ch(1).Enable=0;);
```

Set delay:

```text
SendText("vban1", VFX.Strip(0).Ch(1).Delay=25;);
```

Add or subtract delay:

```text
SendText("vban1", VFX.Strip(0).Ch(1).Delay+=10;);
SendText("vban1", VFX.Strip(0).Ch(1).Delay-=10;);
```

Set volume:

```text
SendText("vban1", VFX.Strip(0).Ch(1).Volume=100;);
```

Add or subtract volume:

```text
SendText("vban1", VFX.Strip(0).Ch(1).Volume+=5;);
SendText("vban1", VFX.Strip(0).Ch(1).Volume-=5;);
```

## Output Bus Commands

Enable or disable delay/volume processing on an output channel:

```text
SendText("vban1", VFX.Bus(B1).Ch(1).Enable=1;);
SendText("vban1", VFX.Bus(B1).Ch(1).Enable=0;);
```

Set delay or volume:

```text
SendText("vban1", VFX.Bus(B1).Ch(1).Delay=25;);
SendText("vban1", VFX.Bus(B1).Ch(1).Volume=100;);
```

Relative delay and volume work on buses too:

```text
SendText("vban1", VFX.Bus(A1).Ch(1-2).Delay+=10;);
SendText("vban1", VFX.Bus(A1).Ch(1-2).Volume-=5;);
```

## Direct Route Commands

Route commands are input-strip commands. They create direct input-to-output routing after the input VST section.

Replace the route list for strip 0 channel 1 and route it to bus B1 channel 3:

```text
SendText("vban1", VFX.Strip(0).Ch(1).Route=Bus(B1).Ch(3););
```

Add another route destination:

```text
SendText("vban1", VFX.Strip(0).Ch(1).Route+=Bus(B2).Ch(4););
```

Remove one route destination:

```text
SendText("vban1", VFX.Strip(0).Ch(1).Route-=Bus(B1).Ch(3););
```

Enable saved routes:

```text
SendText("vban1", VFX.Strip(0).Ch(1).RouteEnable=1;);
```

Disable saved routes without deleting them:

```text
SendText("vban1", VFX.Strip(0).Ch(1).RouteEnable=0;);
```

Mute the normal source path while routing:

```text
SendText("vban1", VFX.Strip(0).Ch(1).MuteNormal=1;);
```

Restore the normal source path:

```text
SendText("vban1", VFX.Strip(0).Ch(1).MuteNormal=0;);
```

## Combined Commands

Multiple commands can be sent in one `SendText`:

```text
SendText("vban1", VFX.Strip(5).Ch(1).Route=Bus(B1).Ch(1); VFX.Strip(5).Ch(1).MuteNormal=1; VFX.Strip(5).Ch(1).Delay=20;);
```

## Accepted Aliases

Enable:

```text
Enable
Enabled
```

Delay:

```text
Delay
DelayMs
Ms
```

Volume:

```text
Volume
Vol
Gain
```

Route enable:

```text
RouteEnable
RouteEnabled
```

Mute normal:

```text
MuteNormal
RouteMute
MuteRoute
RouteMuteNormal
```

Boolean values:

```text
1, 0
true, false
on, off
yes, no
```

## V1 Scope

The V1 text command surface controls delay, volume, direct routing, route enable, and mute-standard routing. It intentionally does not control VST plugin loading, VST editor windows, plugin parameters, plugin bypass, or VST node wiring.
