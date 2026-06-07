#include "engine/RealtimeEngine.h"
#include "plugins/PluginHostLayer.h"
#include "voicemeeter/VoicemeeterClient.h"

#include <algorithm>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
using namespace elka;

struct NativeDirectRoute
{
    int sourceChannel = -1;
    int destinationChannel = -1;
    int delayMilliseconds = 0;
    int gainPercent = 100;
    int muteSource = 0;
};

struct NativeStats
{
    int connectionState = 0;
    int mode = 1;
    int sampleRate = 0;
    int blockSize = 0;
    int inputChannels = 0;
    int outputChannels = 0;
    unsigned long long callbackCount = 0;
    double lastProcessUsec = 0.0;
    double peakProcessUsec = 0.0;
    double callbackCpuPercent = 0.0;
    int delayBufferSampleRate = 0;
};

class NativeHost
{
public:
    NativeHost()
        : client(engine)
    {
    }

    RealtimeEngine engine;
    PluginHostLayer plugins;
    VoicemeeterClient client;
    CallbackMode mode = CallbackMode::InputInsert;
    std::wstring lastStatus = L"Native engine ready";
};

std::mutex g_mutex;
std::unique_ptr<NativeHost> g_host;

CallbackMode callbackModeFromApi(int mode) noexcept
{
    const int validBits =
        static_cast<int>(CallbackMode::InputInsert) |
        static_cast<int>(CallbackMode::OutputInsert) |
        static_cast<int>(CallbackMode::Main);
    const int safeMode = mode & validBits;
    return static_cast<CallbackMode>(safeMode != 0 ? safeMode : static_cast<int>(CallbackMode::InputInsert));
}

CallbackStreamKind streamKindFromApi(int mode) noexcept
{
    switch (mode)
    {
    case 2:
        return CallbackStreamKind::OutputInsert;
    case 4:
        return CallbackStreamKind::Main;
    case 1:
    default:
        return CallbackStreamKind::InputInsert;
    }
}

int apiModeFromCallbackMode(CallbackMode mode) noexcept
{
    return static_cast<int>(mode);
}

void writeWide(const std::wstring& text, wchar_t* buffer, int bufferChars) noexcept
{
    if (buffer == nullptr || bufferChars <= 0)
        return;

    const auto count = std::min<int>(static_cast<int>(text.size()), bufferChars - 1);
    if (count > 0)
        std::wmemcpy(buffer, text.c_str(), static_cast<size_t>(count));

    buffer[count] = L'\0';
}

std::wstring widenUtf8(const std::string& value)
{
    if (value.empty())
        return {};

    const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required <= 0)
        return {};

    std::wstring result(static_cast<size_t>(required - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), required);
    return result;
}

std::string narrowWide(const wchar_t* value)
{
    if (value == nullptr || value[0] == L'\0')
        return {};

    const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0)
        return {};

    std::string result(static_cast<size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr);
    return result;
}

std::vector<std::string> splitPluginFolders(const wchar_t* folders)
{
    std::vector<std::string> result;
    std::wstring text = folders != nullptr ? folders : L"";
    std::wstring current;
    for (wchar_t ch : text)
    {
        if (ch == L';' || ch == L'\n' || ch == L'\r')
        {
            if (!current.empty())
            {
                result.push_back(narrowWide(current.c_str()));
                current.clear();
            }
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty())
        result.push_back(narrowWide(current.c_str()));

    result.erase(
        std::remove_if(result.begin(), result.end(), [](const std::string& value) { return value.empty(); }),
        result.end());
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

NativeHost& host()
{
    if (!g_host)
        g_host = std::make_unique<NativeHost>();

    return *g_host;
}

bool startLocked(NativeHost& target, std::wstring& error)
{
    if (!target.client.connect(error))
        return false;

    int configuredSampleRate = 48000;
    target.client.getConfiguredSampleRate(configuredSampleRate);
    if (!target.engine.prepareDelayBuffers(configuredSampleRate))
    {
        error = L"Failed to allocate delay buffers for " + std::to_wstring(configuredSampleRate) + L" Hz";
        return false;
    }

    if (!target.client.start(error))
        return false;

    target.lastStatus = target.client.statusText();
    return true;
}

bool setModeLocked(NativeHost& target, CallbackMode mode, std::wstring& error)
{
    target.mode = mode;
    const bool wasRunning = target.client.state() == ConnectionState::Running;
    if (wasRunning)
        target.client.stop();

    if (target.client.state() == ConnectionState::Disconnected)
    {
        target.client.setPreferredMode(mode);
    }
    else if (!target.client.registerCallback(mode, error))
    {
        return false;
    }

    return startLocked(target, error);
}

void syncPluginNodeLocked(NativeHost& target, int slot)
{
    if (slot < 0 || slot >= PluginHostLayer::MaxPluginNodes)
        return;

    auto nodes = target.plugins.pluginNodes();
    const auto nodeIt = std::find_if(nodes.begin(), nodes.end(), [slot](const PluginNodeSummary& node) {
        return node.slot == slot;
    });

    if (nodeIt == nodes.end())
    {
        target.engine.clearPluginSlot(slot);
        return;
    }

    int inputRouteCount = 0;
    int outputRouteCount = 0;
    const auto inputRoutes = target.plugins.pluginNodeInputRoutes(slot, inputRouteCount);
    const auto outputRoutes = target.plugins.pluginNodeOutputRoutes(slot, outputRouteCount);
    target.engine.setPluginSlot(
        slot,
        target.plugins.realtimeProcessorForSlot(slot),
        static_cast<CallbackStreamKind>(nodeIt->kind),
        inputRoutes.data(),
        inputRouteCount,
        outputRoutes.data(),
        outputRouteCount,
        true,
        nodeIt->bypassed);
}

void syncAllPluginNodesLocked(NativeHost& target)
{
    const auto nodes = target.plugins.pluginNodes();
    for (const auto& node : nodes)
        syncPluginNodeLocked(target, node.slot);
}

void markGraphChannel(bool* active, int channel) noexcept
{
    constexpr int maxChannels = 64;
    if (channel < 0 || channel >= maxChannels)
        return;

    active[channel] = true;

    const int pairedChannel = (channel % 2 == 0) ? channel + 1 : channel - 1;
    if (pairedChannel >= 0 && pairedChannel < maxChannels)
        active[pairedChannel] = true;
}

void resyncPluginGraphGatesLocked(NativeHost& target)
{
    constexpr int maxChannels = 64;
    bool inputActive[maxChannels] {};
    bool outputActive[maxChannels] {};
    bool mainActive[maxChannels] {};

    const auto nodes = target.plugins.pluginNodes();
    for (const auto& node : nodes)
    {
        if (node.slot < 0)
            continue;

        bool* active = inputActive;
        if (node.kind == static_cast<int>(CallbackStreamKind::OutputInsert))
            active = outputActive;
        else if (node.kind == static_cast<int>(CallbackStreamKind::Main))
            active = mainActive;

        for (int route = 0; route < node.inputRouteCount; ++route)
        {
            const int pluginPin = node.inputRoutes[static_cast<size_t>(route)].to;
            if (pluginPin < 0 || pluginPin >= node.mainInputPins)
                continue;

            const int channel = node.inputRoutes[static_cast<size_t>(route)].from;
            markGraphChannel(active, channel);
        }

        for (int route = 0; route < node.outputRouteCount; ++route)
        {
            const int channel = node.outputRoutes[static_cast<size_t>(route)].to;
            markGraphChannel(active, channel);
        }
    }

    for (int channel = 0; channel < maxChannels; ++channel)
    {
        target.engine.setChannelPluginGraphEnabled(CallbackStreamKind::InputInsert, channel, inputActive[channel]);
        target.engine.setChannelPluginGraphEnabled(CallbackStreamKind::OutputInsert, channel, outputActive[channel]);
        target.engine.setChannelPluginGraphEnabled(CallbackStreamKind::Main, channel, mainActive[channel]);
    }
}
}

extern "C"
{
__declspec(dllexport) int __cdecl ElkaFx_Initialize(wchar_t* status, int statusChars)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();
    std::wstring error;
    const bool started = setModeLocked(target, target.mode, error);
    writeWide(started ? target.client.statusText() : error, status, statusChars);
    return started ? 0 : -1;
}

__declspec(dllexport) void __cdecl ElkaFx_Shutdown()
{
    std::lock_guard lock(g_mutex);
    if (g_host)
        g_host->client.disconnect();

    g_host.reset();
}

__declspec(dllexport) int __cdecl ElkaFx_SetMode(int mode, wchar_t* status, int statusChars)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();
    std::wstring error;
    const bool ok = setModeLocked(target, callbackModeFromApi(mode), error);
    writeWide(ok ? target.client.statusText() : error, status, statusChars);
    return ok ? 0 : -1;
}

__declspec(dllexport) int __cdecl ElkaFx_Start(wchar_t* status, int statusChars)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();
    std::wstring error;
    const bool ok = startLocked(target, error);
    writeWide(ok ? target.client.statusText() : error, status, statusChars);
    return ok ? 0 : -1;
}

__declspec(dllexport) void __cdecl ElkaFx_Disconnect()
{
    std::lock_guard lock(g_mutex);
    if (g_host)
        g_host->client.disconnect();
}

__declspec(dllexport) void __cdecl ElkaFx_SetTargetRange(int kind, int startChannel, int channelCount)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();
    target.engine.setTargetRange(streamKindFromApi(kind), startChannel, channelCount);
}

__declspec(dllexport) void __cdecl ElkaFx_SetChannelSettings(
    int kind,
    int channel,
    int enabled,
    int delayMilliseconds,
    int gainPercent)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();
    const auto streamKind = streamKindFromApi(kind);
    target.engine.setChannelEnabled(streamKind, channel, enabled != 0);
    target.engine.setChannelDelayMilliseconds(streamKind, channel, delayMilliseconds);
    target.engine.setChannelGainPercent(streamKind, channel, gainPercent);
}

__declspec(dllexport) void __cdecl ElkaFx_SetPluginGraphEnabled(int kind, int channel, int enabled)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();
    target.engine.setChannelPluginGraphEnabled(streamKindFromApi(kind), channel, enabled != 0);
}

__declspec(dllexport) void __cdecl ElkaFx_SetDirectRoutes(int kind, const NativeDirectRoute* routes, int routeCount)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();

    std::vector<DirectAudioRoute> nativeRoutes;
    const int safeCount = routes != nullptr ? std::clamp(routeCount, 0, RealtimeEngine::MaxDirectRoutes) : 0;
    nativeRoutes.reserve(static_cast<size_t>(safeCount));
    for (int i = 0; i < safeCount; ++i)
    {
        nativeRoutes.push_back(DirectAudioRoute {
            routes[i].sourceChannel,
            routes[i].destinationChannel,
            routes[i].delayMilliseconds,
            routes[i].gainPercent,
            routes[i].muteSource != 0
        });
    }

    target.engine.setDirectRoutes(
        streamKindFromApi(kind),
        nativeRoutes.empty() ? nullptr : nativeRoutes.data(),
        static_cast<int>(nativeRoutes.size()));
}

__declspec(dllexport) void __cdecl ElkaFx_GetStats(NativeStats* stats)
{
    if (stats == nullptr)
        return;

    std::lock_guard lock(g_mutex);
    auto& target = host();
    const auto realtimeStats = target.engine.getStats();
    stats->connectionState = static_cast<int>(target.client.state());
    stats->mode = apiModeFromCallbackMode(target.mode);
    stats->sampleRate = realtimeStats.sampleRate;
    stats->blockSize = realtimeStats.blockSize;
    stats->inputChannels = realtimeStats.inputChannels;
    stats->outputChannels = realtimeStats.outputChannels;
    stats->callbackCount = realtimeStats.callbackCount;
    stats->lastProcessUsec = realtimeStats.lastProcessUsec;
    stats->peakProcessUsec = realtimeStats.peakProcessUsec;
    stats->callbackCpuPercent = realtimeStats.callbackCpuPercent;
    stats->delayBufferSampleRate = realtimeStats.delayBufferSampleRate;
}

__declspec(dllexport) int __cdecl ElkaFx_GetPluginCount()
{
    std::lock_guard lock(g_mutex);
    return static_cast<int>(host().plugins.plugins().size());
}

__declspec(dllexport) int __cdecl ElkaFx_GetPluginName(int index, wchar_t* buffer, int bufferChars)
{
    std::lock_guard lock(g_mutex);
    const auto& plugins = host().plugins.plugins();
    if (index < 0 || index >= static_cast<int>(plugins.size()))
    {
        writeWide(L"", buffer, bufferChars);
        return -1;
    }

    const auto& plugin = plugins[static_cast<size_t>(index)];
    std::wstring label = widenUtf8(plugin.name);
    if (!plugin.manufacturer.empty())
        label += L" - " + widenUtf8(plugin.manufacturer);
    if (!plugin.format.empty())
        label += L" [" + widenUtf8(plugin.format) + L"]";

    writeWide(label, buffer, bufferChars);
    return 0;
}

__declspec(dllexport) int __cdecl ElkaFx_ScanDefaultVst3(wchar_t* status, int statusChars)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();
    const int count = target.plugins.scanDefaultPluginLocations();
    if (!target.plugins.lastError().empty())
    {
        writeWide(widenUtf8(target.plugins.lastError()), status, statusChars);
        return -1;
    }

    writeWide(L"Plugin scan complete: " + std::to_wstring(count) + L" plugin(s)", status, statusChars);
    return count;
}

__declspec(dllexport) int __cdecl ElkaFx_ScanPluginFolders(
    const wchar_t* folders,
    int includeDefaults,
    wchar_t* status,
    int statusChars)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();

    std::vector<std::string> paths;
    if (includeDefaults != 0)
    {
        auto defaults = target.plugins.defaultPluginSearchPaths();
        paths.insert(paths.end(), defaults.begin(), defaults.end());
    }

    auto customFolders = splitPluginFolders(folders);
    paths.insert(paths.end(), customFolders.begin(), customFolders.end());
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    const int count = target.plugins.scanPluginPaths(paths, false);
    if (!target.plugins.lastError().empty())
    {
        writeWide(widenUtf8(target.plugins.lastError()), status, statusChars);
        return -1;
    }

    std::wstring message = L"Plugin scan complete: " + std::to_wstring(count) + L" plugin(s)";
    if (!customFolders.empty())
        message += L" from standard locations + " + std::to_wstring(customFolders.size()) + L" folder(s)";

#if ELKA_ENABLE_VST2_HOST
    message += L" | VST2 enabled";
#else
    message += L" | VST2 disabled";
#endif

    const auto& report = target.plugins.lastScanReport();
    if (!report.empty())
    {
        message += L"\n" + widenUtf8(report);
    }

    writeWide(message, status, statusChars);
    return count;
}

__declspec(dllexport) int __cdecl ElkaFx_AddPluginNode(
    int pluginIndex,
    int mode,
    int mainInputPins,
    int sidechainInputPins,
    int outputPins,
    int x,
    int y,
    int* slotOut,
    int* inputPinsOut,
    int* outputPinsOut,
    wchar_t* status,
    int statusChars)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();

    const auto streamKind = streamKindFromApi(mode);
    const int safeMainInputPins = std::clamp(mainInputPins, 1, 8);
    const int safeSidechainInputPins = std::clamp(sidechainInputPins, 0, RealtimeEngine::MaxPluginPins - safeMainInputPins);
    const int safeInputPins = std::clamp(safeMainInputPins + safeSidechainInputPins, 1, RealtimeEngine::MaxPluginPins);
    const int safeOutputPins = std::clamp(outputPins, 1, 8);
    const int layoutChannels = std::max(safeMainInputPins, safeOutputPins);
    const int layoutId = layoutChannels == 1 ? 0 : layoutChannels == 2 ? 1 : layoutChannels;
    const std::string layoutName =
        layoutChannels == 1 ? "Mono" : layoutChannels == 2 ? "Stereo" : std::to_string(layoutChannels) + " channel";

    int sampleRate = 48000;
    target.client.getConfiguredSampleRate(sampleRate);
    const auto stats = target.engine.getStats();
    const int maxBlockSize = std::max(4096, stats.blockSize > 0 ? stats.blockSize : 512);

    const int slot = target.plugins.addDiscoveredPluginNode(
        static_cast<size_t>(std::max(0, pluginIndex)),
        sampleRate,
        maxBlockSize,
        safeMainInputPins,
        safeSidechainInputPins,
        safeOutputPins,
        layoutId,
        layoutName,
        static_cast<int>(streamKind),
        0,
        layoutChannels);

    if (slot < 0)
    {
        writeWide(L"Plugin node add failed: " + widenUtf8(target.plugins.lastError()), status, statusChars);
        return -1;
    }

    target.plugins.setPluginNodePosition(slot, x, y);

    syncPluginNodeLocked(target, slot);

    if (slotOut != nullptr)
        *slotOut = slot;
    int actualInputPins = safeInputPins;
    int actualOutputPins = safeOutputPins;
    const auto nodes = target.plugins.pluginNodes();
    const auto nodeIt = std::find_if(nodes.begin(), nodes.end(), [slot](const PluginNodeSummary& node) {
        return node.slot == slot;
    });
    if (nodeIt != nodes.end())
    {
        actualInputPins = nodeIt->inputPins;
        actualOutputPins = nodeIt->outputPins;
    }

    if (inputPinsOut != nullptr)
        *inputPinsOut = actualInputPins;
    if (outputPinsOut != nullptr)
        *outputPinsOut = actualOutputPins;

    std::wstring error;
    startLocked(target, error);
    writeWide(error.empty() ? L"VST node loaded as free module" : error, status, statusChars);
    return 0;
}

__declspec(dllexport) int __cdecl ElkaFx_SetPluginNodeBypassed(int slot, int bypassed)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();
    target.plugins.setPluginNodeBypassed(slot, bypassed != 0);
    syncPluginNodeLocked(target, slot);
    return 0;
}

__declspec(dllexport) int __cdecl ElkaFx_TogglePluginNodeInputRoute(int slot, int sourceChannel, int pluginPin)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();
    const bool active = target.plugins.togglePluginNodeInputRoute(slot, sourceChannel, pluginPin);
    syncPluginNodeLocked(target, slot);
    resyncPluginGraphGatesLocked(target);
    return active ? 1 : 0;
}

__declspec(dllexport) int __cdecl ElkaFx_TogglePluginNodeOutputRoute(int slot, int pluginPin, int destinationChannel)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();
    const bool active = target.plugins.togglePluginNodeOutputRoute(slot, pluginPin, destinationChannel);
    syncPluginNodeLocked(target, slot);
    resyncPluginGraphGatesLocked(target);
    return active ? 1 : 0;
}

__declspec(dllexport) int __cdecl ElkaFx_TogglePluginNodeModuleRoute(
    int sourceSlot,
    int sourcePin,
    int destinationSlot,
    int destinationPin)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();
    const bool active = target.plugins.togglePluginNodeModuleRoute(sourceSlot, sourcePin, destinationSlot, destinationPin);
    syncPluginNodeLocked(target, sourceSlot);
    syncPluginNodeLocked(target, destinationSlot);
    resyncPluginGraphGatesLocked(target);
    return active ? 1 : 0;
}

__declspec(dllexport) int __cdecl ElkaFx_OpenPluginEditor(int slot, wchar_t* status, int statusChars)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();
    if (!target.plugins.openPluginEditor(slot))
    {
        writeWide(L"Plugin editor failed: " + widenUtf8(target.plugins.lastError()), status, statusChars);
        return -1;
    }

    writeWide(L"Plugin editor opened", status, statusChars);
    return 0;
}

__declspec(dllexport) int __cdecl ElkaFx_RemovePluginNode(int slot)
{
    std::lock_guard lock(g_mutex);
    auto& target = host();
    target.engine.clearPluginSlot(slot);
    target.plugins.removePluginNode(slot);
    syncAllPluginNodesLocked(target);
    resyncPluginGraphGatesLocked(target);
    return 0;
}
}
