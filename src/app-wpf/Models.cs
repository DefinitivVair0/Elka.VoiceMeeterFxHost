using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace Elka.VoiceMeeterFxHost.App;

[Flags]
internal enum CallbackMode
{
    None = 0,
    Input = 1,
    Output = 2,
    Main = 4
}

public enum VoicemeeterKind
{
    Unknown = 0,
    Standard = 1,
    Banana = 2,
    Potato = 3
}

internal static class VoicemeeterKindInfo
{
    public static string DisplayName(VoicemeeterKind kind)
    {
        return kind switch
        {
            VoicemeeterKind.Standard => "Voicemeeter Standard",
            VoicemeeterKind.Banana => "Voicemeeter Banana",
            VoicemeeterKind.Potato => "Voicemeeter Potato",
            _ => "Voicemeeter Potato profile"
        };
    }
}

internal sealed record ChannelRange(int Start, int End)
{
    public int Count => End - Start + 1;
}

internal sealed record IoEndpoint(string Name, ChannelRange Range)
{
    public int ChannelCount => Range.Count;

    public string DisplayName => Range.Start == Range.End
        ? $"{Name} ({Range.Start + 1})"
        : $"{Name} ({Range.Start + 1}-{Range.End + 1})";

    public string Key(CallbackMode mode) => $"{mode}:{Name}:{Range.Start}:{Range.End}";
}

internal enum EndpointPinMode
{
    Stereo = 0,
    Full = 1
}

internal static class VoicemeeterIoLayout
{
    public static IReadOnlyList<IoEndpoint> GetEndpoints(CallbackMode mode, VoicemeeterKind kind)
    {
        return mode == CallbackMode.Output
            ? BuildOutputEndpoints(kind)
            : BuildInputEndpoints(kind);
    }

    public static IReadOnlyList<IoEndpoint> BuildCanvasInputs(VoicemeeterKind kind) => BuildInputEndpoints(kind);

    public static IReadOnlyList<IoEndpoint> BuildCanvasOutputs(VoicemeeterKind kind) => BuildOutputEndpoints(kind);

    private static IReadOnlyList<IoEndpoint> BuildInputEndpoints(VoicemeeterKind kind)
    {
        var spec = GetSpec(kind);
        var endpoints = new List<IoEndpoint>();
        var channel = 0;

        for (var hardware = 1; hardware <= spec.HardwareInputs; hardware++)
        {
            endpoints.Add(new IoEndpoint($"Hardware In {hardware}", new ChannelRange(channel, channel + 1)));
            channel += 2;
        }

        foreach (var virtualInput in spec.VirtualInputs)
        {
            endpoints.Add(new IoEndpoint(virtualInput, new ChannelRange(channel, channel + 7)));
            channel += 8;
        }

        return endpoints;
    }

    private static IReadOnlyList<IoEndpoint> BuildOutputEndpoints(VoicemeeterKind kind)
    {
        var spec = GetSpec(kind);
        var endpoints = new List<IoEndpoint>();
        var channel = 0;

        for (var hardware = 1; hardware <= spec.HardwareOutputs; hardware++)
        {
            endpoints.Add(new IoEndpoint($"A{hardware} 8ch", new ChannelRange(channel, channel + 7)));
            channel += 8;
        }

        for (var virtualBus = 1; virtualBus <= spec.VirtualOutputs; virtualBus++)
        {
            var name = virtualBus switch
            {
                2 => "B2 / AUX Out",
                3 => "B3 / VAIO3 Out",
                _ => $"B{virtualBus} 8ch"
            };
            endpoints.Add(new IoEndpoint(name, new ChannelRange(channel, channel + 7)));
            channel += 8;
        }

        return endpoints;
    }

    private static VoicemeeterLayoutSpec GetSpec(VoicemeeterKind kind)
    {
        return kind switch
        {
            VoicemeeterKind.Standard => new VoicemeeterLayoutSpec(2, ["VAIO"], 2, 1),
            VoicemeeterKind.Banana => new VoicemeeterLayoutSpec(3, ["VAIO", "AUX"], 3, 2),
            _ => new VoicemeeterLayoutSpec(5, ["VAIO", "AUX", "VAIO3"], 5, 3)
        };
    }

    private sealed record VoicemeeterLayoutSpec(
        int HardwareInputs,
        IReadOnlyList<string> VirtualInputs,
        int HardwareOutputs,
        int VirtualOutputs);
}

internal sealed class ChannelSettingsSnapshot
{
    public CallbackMode Mode { get; set; }
    public string EndpointName { get; set; } = string.Empty;
    public int ChannelCount { get; set; }
    public bool[] Enabled { get; set; } = [];
    public double[] DelayMilliseconds { get; set; } = [];
    public double[] VolumePercent { get; set; } = [];
    public bool[] RouteEnabled { get; set; } = [];
    public bool[] RouteMuteNormal { get; set; } = [];
    public List<List<RouteDestinationSnapshot>> RouteDestinations { get; set; } = [];
    public EndpointPinMode PinMode { get; set; } = EndpointPinMode.Stereo;
}

internal sealed class RouteDestinationSnapshot
{
    public int BusIndex { get; set; }
    public int ChannelOffset { get; set; }
}

internal sealed class RouteBusChoice
{
    public int Index { get; init; }
    public string Name { get; init; } = string.Empty;
    public int ChannelCount { get; init; }

    public override string ToString() => Name;
}

internal sealed class EndpointChannelSettings
{
    public EndpointChannelSettings(CallbackMode mode, IoEndpoint endpoint)
    {
        Mode = mode;
        Endpoint = endpoint;
        Enabled = new bool[endpoint.ChannelCount];
        DelayMilliseconds = new double[endpoint.ChannelCount];
        VolumePercent = Enumerable.Repeat(100.0, endpoint.ChannelCount).ToArray();
        RouteEnabled = new bool[endpoint.ChannelCount];
        RouteMuteNormal = new bool[endpoint.ChannelCount];
        RouteDestinations = Enumerable.Range(0, endpoint.ChannelCount)
            .Select(offset => new List<RouteDestinationSnapshot>
            {
                new()
                {
                    BusIndex = 0,
                    ChannelOffset = Math.Min(offset, 7)
                }
            })
            .ToArray();
    }

    public CallbackMode Mode { get; }
    public IoEndpoint Endpoint { get; private set; }
    public bool[] Enabled { get; }
    public double[] DelayMilliseconds { get; }
    public double[] VolumePercent { get; }
    public bool[] RouteEnabled { get; }
    public bool[] RouteMuteNormal { get; }
    public List<RouteDestinationSnapshot>[] RouteDestinations { get; }
    public EndpointPinMode PinMode { get; set; } = EndpointPinMode.Stereo;
    public bool HasActiveChannels => Enabled.Any(static enabled => enabled) || RouteEnabled.Any(static enabled => enabled);
    public int CanvasPinCount => PinMode == EndpointPinMode.Full ? Endpoint.ChannelCount : Math.Min(2, Endpoint.ChannelCount);

    public string Key => Endpoint.Key(Mode);

    public void RebindEndpoint(IoEndpoint endpoint)
    {
        if (endpoint.ChannelCount != Endpoint.ChannelCount)
        {
            return;
        }

        Endpoint = endpoint;
    }

    public ChannelSettingsSnapshot ToSnapshot()
    {
        return new ChannelSettingsSnapshot
        {
            Mode = Mode,
            EndpointName = Endpoint.Name,
            ChannelCount = Endpoint.ChannelCount,
            Enabled = [.. Enabled],
            DelayMilliseconds = [.. DelayMilliseconds],
            VolumePercent = [.. VolumePercent],
            RouteEnabled = [.. RouteEnabled],
            RouteMuteNormal = [.. RouteMuteNormal],
            PinMode = PinMode,
            RouteDestinations = RouteDestinations
                .Select(static destinations => destinations.Select(CloneDestination).ToList())
                .ToList()
        };
    }

    public void ApplySnapshot(ChannelSettingsSnapshot snapshot)
    {
        for (var offset = 0; offset < Endpoint.ChannelCount; offset++)
        {
            Enabled[offset] = offset < snapshot.Enabled.Length && snapshot.Enabled[offset];
            DelayMilliseconds[offset] = offset < snapshot.DelayMilliseconds.Length
                ? Math.Clamp(snapshot.DelayMilliseconds[offset], 0.0, 10_000.0)
                : 0.0;
            VolumePercent[offset] = offset < snapshot.VolumePercent.Length
                ? Math.Clamp(snapshot.VolumePercent[offset], 0.0, 200.0)
                : 100.0;
            RouteEnabled[offset] = offset < snapshot.RouteEnabled.Length && snapshot.RouteEnabled[offset];
            RouteMuteNormal[offset] = offset < snapshot.RouteMuteNormal.Length && snapshot.RouteMuteNormal[offset];
            PinMode = snapshot.PinMode;
            RouteDestinations[offset].Clear();

            if (offset < snapshot.RouteDestinations.Count && snapshot.RouteDestinations[offset].Count > 0)
            {
                RouteDestinations[offset].AddRange(snapshot.RouteDestinations[offset].Select(CloneDestination));
            }
            else
            {
                RouteDestinations[offset].Add(new RouteDestinationSnapshot());
            }
        }
    }

    public IEnumerable<DirectRouteSummary> ToDirectRoutes(VoicemeeterKind kind)
    {
        if (Mode != CallbackMode.Input)
        {
            yield break;
        }

        var buses = VoicemeeterIoLayout.GetEndpoints(CallbackMode.Output, kind);
        for (var offset = 0; offset < RouteEnabled.Length; offset++)
        {
            if (!RouteEnabled[offset])
            {
                continue;
            }

            foreach (var destination in RouteDestinations[offset])
            {
                if (buses.Count == 0)
                {
                    continue;
                }

                var busIndex = Math.Clamp(destination.BusIndex, 0, buses.Count - 1);
                var bus = buses[busIndex];
                var destinationOffset = Math.Clamp(destination.ChannelOffset, 0, bus.ChannelCount - 1);
                yield return new DirectRouteSummary(
                    Endpoint.Range.Start + offset,
                    bus.Range.Start + destinationOffset,
                    (int)Math.Round(DelayMilliseconds[offset]),
                    (int)Math.Round(VolumePercent[offset]),
                    RouteMuteNormal[offset],
                    $"{Endpoint.Name} Ch {offset + 1} -> {bus.Name} Ch {destinationOffset + 1}");
            }
        }
    }

    private static RouteDestinationSnapshot CloneDestination(RouteDestinationSnapshot destination)
    {
        return new RouteDestinationSnapshot
        {
            BusIndex = destination.BusIndex,
            ChannelOffset = destination.ChannelOffset
        };
    }
}

internal sealed record DirectRouteSummary(
    int SourceChannel,
    int DestinationChannel,
    int DelayMilliseconds,
    int GainPercent,
    bool MuteNormal,
    string Name);

internal sealed record PluginChoice(int Index, string Name)
{
    public override string ToString() => Name;
}

internal sealed class PluginNodeSnapshot
{
    public string Name { get; set; } = "VST";
    public int PluginIndex { get; set; } = -1;
    public int Slot { get; set; }
    public int X { get; set; } = 430;
    public int Y { get; set; } = 120;
    public int MainInputPins { get; set; } = 2;
    public int SidechainInputPins { get; set; }
    public int InputPins { get; set; } = 2;
    public int OutputPins { get; set; } = 2;
    public bool Bypassed { get; set; }
    public CallbackMode Mode { get; set; } = CallbackMode.Input;
}

internal sealed class CanvasConnectionSnapshot
{
    public string From { get; set; } = string.Empty;
    public string To { get; set; } = string.Empty;
    public string Kind { get; set; } = "route";
    public string FromKind { get; set; } = string.Empty;
    public CallbackMode FromMode { get; set; } = CallbackMode.None;
    public int FromChannel { get; set; } = -1;
    public int FromSlot { get; set; } = -1;
    public int FromPin { get; set; } = -1;
    public string ToKind { get; set; } = string.Empty;
    public CallbackMode ToMode { get; set; } = CallbackMode.None;
    public int ToChannel { get; set; } = -1;
    public int ToSlot { get; set; } = -1;
    public int ToPin { get; set; } = -1;
}

internal sealed class FxHostSettings
{
    public VoicemeeterKind Kind { get; set; } = VoicemeeterKind.Potato;
    public CallbackMode SelectedMode { get; set; } = CallbackMode.Input;
    public string? SelectedEndpointName { get; set; }
    public string PluginSearchText { get; set; } = string.Empty;
    public List<string> PluginScanFolders { get; set; } = [];
    public bool VbanControlEnabled { get; set; }
    public int VbanControlPort { get; set; } = 6981;
    public string VbanControlStreamName { get; set; } = "Command1";
    public bool VbanControlLocalOnly { get; set; } = true;
    public List<ChannelSettingsSnapshot> Endpoints { get; set; } = [];
    public List<PluginNodeSnapshot> PluginNodes { get; set; } = [];
    public List<CanvasConnectionSnapshot> CanvasConnections { get; set; } = [];
    public Dictionary<string, double> EndpointCanvasYOffsets { get; set; } = [];
    public Dictionary<string, string> EndpointRouteHues { get; set; } = [];
}

internal static class FxHostSettingsStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true
    };

    public static FxHostSettings Load()
    {
        try
        {
            if (!File.Exists(SettingsPath))
            {
                return new FxHostSettings();
            }

            return JsonSerializer.Deserialize<FxHostSettings>(File.ReadAllText(SettingsPath), JsonOptions)
                ?? new FxHostSettings();
        }
        catch
        {
            return new FxHostSettings();
        }
    }

    public static void Save(FxHostSettings settings)
    {
        try
        {
            Directory.CreateDirectory(SettingsDirectory);
            File.WriteAllText(SettingsPath, JsonSerializer.Serialize(settings, JsonOptions));
        }
        catch
        {
            // Persistence should never interrupt the audio engine or UI.
        }
    }

    private static string SettingsDirectory =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ElkaVoiceMeeterFxHost");

    private static string SettingsPath => Path.Combine(SettingsDirectory, "wpf-main-window.json");
}

internal sealed class NativeEngineClient : IDisposable
{
    private const string DllName = "ElkaVoiceMeeterFxHost.Native.dll";
    private bool _attached;
    private bool _disposed;
    private string _lastStatus = "Native engine bridge not loaded";
    private CallbackMode _requestedMode = CallbackMode.Input;
    private CallbackMode _appliedMode = CallbackMode.None;

    public NativeEngineClient()
    {
        var status = new StringBuilder(65536);
        try
        {
            _attached = ElkaFx_Initialize(status, status.Capacity) == 0;
            _lastStatus = status.Length > 0
                ? status.ToString()
                : _attached ? "Native engine attached" : "Native engine failed to start";
        }
        catch (DllNotFoundException)
        {
            _attached = false;
            _lastStatus = "Native bridge DLL missing. Build ElkaVoiceMeeterFxHost.Native first.";
        }
        catch (BadImageFormatException)
        {
            _attached = false;
            _lastStatus = "Native bridge architecture mismatch. Build x64 native and WPF.";
        }
        catch (Exception ex)
        {
            _attached = false;
            _lastStatus = ex.Message;
        }
    }

    public bool IsAttached => _attached;

    public string StatusText
    {
        get
        {
            if (!_attached)
            {
                return _lastStatus;
            }

            var stats = GetStats();
            var state = stats.ConnectionState switch
            {
                3 => "Running",
                2 => "Callback registered",
                1 => "Connected",
                _ => "Disconnected"
            };

            var rate = stats.SampleRate > 0 ? $"{stats.SampleRate} Hz" : "no audio yet";
            var block = stats.BlockSize > 0 ? $"{stats.BlockSize} spl" : "block --";
            return $"{state} | {rate} | {block} | CPU {stats.CallbackCpuPercent:0.0}% | peak {stats.PeakProcessUsec:0} us";
        }
    }

    public IReadOnlyList<PluginChoice> PluginChoices()
    {
        if (!_attached)
        {
            return [];
        }

        var count = Math.Max(0, ElkaFx_GetPluginCount());
        var plugins = new List<PluginChoice>(count);
        for (var index = 0; index < count; index++)
        {
            var buffer = new StringBuilder(512);
            if (ElkaFx_GetPluginName(index, buffer, buffer.Capacity) == 0 && buffer.Length > 0)
            {
                plugins.Add(new PluginChoice(index, buffer.ToString()));
            }
        }

        return plugins;
    }

    public string ScanPlugins(IEnumerable<string> customFolders)
    {
        if (!_attached)
        {
            return _lastStatus;
        }

        var status = new StringBuilder(512);
        var folderText = string.Join(";", customFolders
            .Where(static folder => !string.IsNullOrWhiteSpace(folder))
            .Select(static folder => folder.Trim())
            .Distinct(StringComparer.OrdinalIgnoreCase));
        var result = ElkaFx_ScanPluginFolders(folderText, includeDefaults: 1, status, status.Capacity);
        _lastStatus = status.ToString();
        return result >= 0 ? _lastStatus : $"Scan failed: {_lastStatus}";
    }

    public void SetRequestedMode(CallbackMode mode)
    {
        _requestedMode = mode == CallbackMode.None ? CallbackMode.Input : mode;
        SetMode(_requestedMode);
    }

    public void ApplyChannelSettings(EndpointChannelSettings settings)
    {
        if (!_attached)
        {
            return;
        }

        ElkaFx_SetTargetRange((int)settings.Mode, settings.Endpoint.Range.Start, settings.Endpoint.ChannelCount);
        for (var offset = 0; offset < settings.Endpoint.ChannelCount; offset++)
        {
            ElkaFx_SetChannelSettings(
                (int)settings.Mode,
                settings.Endpoint.Range.Start + offset,
                settings.Enabled[offset] ? 1 : 0,
                (int)Math.Round(settings.DelayMilliseconds[offset]),
                (int)Math.Round(settings.VolumePercent[offset]));
        }
    }

    public void ApplyRoutes(IEnumerable<DirectRouteSummary> routes)
    {
        if (!_attached)
        {
            return;
        }

        var routeArray = routes
            .Select(static route => new NativeDirectRoute
            {
                SourceChannel = route.SourceChannel,
                DestinationChannel = route.DestinationChannel,
                DelayMilliseconds = route.DelayMilliseconds,
                GainPercent = route.GainPercent,
                MuteSource = route.MuteNormal ? 1 : 0
            })
            .ToArray();

        ElkaFx_SetDirectRoutes((int)CallbackMode.Input, routeArray, routeArray.Length);
        var effectiveMode = routeArray.Length > 0
            ? _requestedMode | CallbackMode.Input | CallbackMode.Output
            : _requestedMode;
        SetMode(effectiveMode);
    }

    public PluginNodeSnapshot? AddPluginNode(
        PluginChoice choice,
        CallbackMode mode,
        int mainInputPins,
        int sidechainInputPins,
        int outputPins,
        int x,
        int y)
    {
        if (!_attached)
        {
            return null;
        }

        var status = new StringBuilder(512);
        var slot = 0;
        var loadedInputPins = 0;
        var loadedOutputPins = 0;
        var result = ElkaFx_AddPluginNode(
            choice.Index,
            (int)mode,
            mainInputPins,
            sidechainInputPins,
            outputPins,
            x,
            y,
            ref slot,
            ref loadedInputPins,
            ref loadedOutputPins,
            status,
            status.Capacity);

        if (status.Length > 0)
        {
            _lastStatus = status.ToString();
        }

        if (result != 0)
        {
            return null;
        }

        var loadedMainInputPins = Math.Clamp(Math.Max(1, mainInputPins), 1, Math.Max(1, loadedInputPins));

        return new PluginNodeSnapshot
        {
            PluginIndex = choice.Index,
            Slot = slot,
            Name = choice.Name,
            X = x,
            Y = y,
            MainInputPins = loadedMainInputPins,
            SidechainInputPins = Math.Max(0, loadedInputPins - loadedMainInputPins),
            InputPins = Math.Max(1, loadedInputPins),
            OutputPins = Math.Max(1, loadedOutputPins),
            Mode = mode
        };
    }

    public void SetPluginNodeBypassed(int slot, bool bypassed)
    {
        if (_attached)
        {
            ElkaFx_SetPluginNodeBypassed(slot, bypassed ? 1 : 0);
        }
    }

    public bool TogglePluginInputRoute(int slot, int sourceChannel, int pluginPin)
    {
        return _attached && ElkaFx_TogglePluginNodeInputRoute(slot, sourceChannel, pluginPin) != 0;
    }

    public bool TogglePluginOutputRoute(int slot, int pluginPin, int destinationChannel)
    {
        return _attached && ElkaFx_TogglePluginNodeOutputRoute(slot, pluginPin, destinationChannel) != 0;
    }

    public bool TogglePluginModuleRoute(int sourceSlot, int sourcePin, int destinationSlot, int destinationPin)
    {
        return _attached && ElkaFx_TogglePluginNodeModuleRoute(sourceSlot, sourcePin, destinationSlot, destinationPin) != 0;
    }

    public string OpenPluginEditor(int slot)
    {
        if (!_attached)
        {
            return _lastStatus;
        }

        var status = new StringBuilder(512);
        ElkaFx_OpenPluginEditor(slot, status, status.Capacity);
        _lastStatus = status.Length > 0 ? status.ToString() : _lastStatus;
        return _lastStatus;
    }

    public void RemovePluginNode(int slot)
    {
        if (_attached)
        {
            ElkaFx_RemovePluginNode(slot);
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        if (_attached)
        {
            ElkaFx_Shutdown();
        }

        _disposed = true;
    }

    private void SetMode(CallbackMode mode)
    {
        if (!_attached)
        {
            return;
        }

        if (mode == _appliedMode)
        {
            return;
        }

        var status = new StringBuilder(512);
        var result = ElkaFx_SetMode((int)mode, status, status.Capacity);
        if (result == 0)
        {
            _appliedMode = mode;
        }

        if (result == 0 || status.Length > 0)
        {
            _lastStatus = status.ToString();
        }
    }

    private static NativeStats GetStats()
    {
        var stats = new NativeStats();
        ElkaFx_GetStats(ref stats);
        return stats;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeDirectRoute
    {
        public int SourceChannel;
        public int DestinationChannel;
        public int DelayMilliseconds;
        public int GainPercent;
        public int MuteSource;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeStats
    {
        public int ConnectionState;
        public int Mode;
        public int SampleRate;
        public int BlockSize;
        public int InputChannels;
        public int OutputChannels;
        public ulong CallbackCount;
        public double LastProcessUsec;
        public double PeakProcessUsec;
        public double CallbackCpuPercent;
        public int DelayBufferSampleRate;
    }

    [DllImport(DllName, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ElkaFx_Initialize(StringBuilder status, int statusChars);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void ElkaFx_Shutdown();

    [DllImport(DllName, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ElkaFx_SetMode(int mode, StringBuilder status, int statusChars);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void ElkaFx_SetTargetRange(int kind, int startChannel, int channelCount);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void ElkaFx_SetChannelSettings(
        int kind,
        int channel,
        int enabled,
        int delayMilliseconds,
        int gainPercent);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void ElkaFx_SetDirectRoutes(
        int kind,
        [In] NativeDirectRoute[] routes,
        int routeCount);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void ElkaFx_GetStats(ref NativeStats stats);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ElkaFx_GetPluginCount();

    [DllImport(DllName, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ElkaFx_GetPluginName(int index, StringBuilder buffer, int bufferChars);

    [DllImport(DllName, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ElkaFx_ScanDefaultVst3(StringBuilder status, int statusChars);

    [DllImport(DllName, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ElkaFx_ScanPluginFolders(
        string folders,
        int includeDefaults,
        StringBuilder status,
        int statusChars);

    [DllImport(DllName, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ElkaFx_AddPluginNode(
        int pluginIndex,
        int mode,
        int mainInputPins,
        int sidechainInputPins,
        int outputPins,
        int x,
        int y,
        ref int slot,
        ref int inputPinsOut,
        ref int outputPinsOut,
        StringBuilder status,
        int statusChars);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ElkaFx_SetPluginNodeBypassed(int slot, int bypassed);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ElkaFx_TogglePluginNodeInputRoute(int slot, int sourceChannel, int pluginPin);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ElkaFx_TogglePluginNodeOutputRoute(int slot, int pluginPin, int destinationChannel);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ElkaFx_TogglePluginNodeModuleRoute(int sourceSlot, int sourcePin, int destinationSlot, int destinationPin);

    [DllImport(DllName, CharSet = CharSet.Unicode, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ElkaFx_OpenPluginEditor(int slot, StringBuilder status, int statusChars);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int ElkaFx_RemovePluginNode(int slot);
}
