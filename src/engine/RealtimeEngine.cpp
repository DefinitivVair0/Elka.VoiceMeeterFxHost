#include "engine/RealtimeEngine.h"

#include <algorithm>
#include <memory>
#include <xmmintrin.h>
#include <windows.h>

#ifndef _MM_DENORMALS_ZERO_ON
#define _MM_DENORMALS_ZERO_ON 0x0040
#endif

namespace elka
{
namespace
{
double queryPerformanceFrequency() noexcept
{
    LARGE_INTEGER frequency {};
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(frequency.QuadPart);
}

double elapsedMicroseconds(LARGE_INTEGER start, LARGE_INTEGER end) noexcept
{
    static const double frequency = queryPerformanceFrequency();
    return (static_cast<double>(end.QuadPart - start.QuadPart) * 1000000.0) / frequency;
}

constexpr int MaxCallbackChannels = 64;
constexpr int MaxCallbackReadChannels = 128;
constexpr int PingStatusIdle = 0;
constexpr int PingStatusArmed = 1;
constexpr int PingStatusWaiting = 2;
constexpr int PingStatusDetected = 3;
constexpr int PingStatusTimeout = 4;
constexpr int PingPulseSamples = 8;
constexpr float PingPulseAmplitude = 0.80f;
constexpr float PingDetectThreshold = 0.18f;
constexpr int DelaySmoothingMilliseconds = 8;

const float* sourceChannelPointer(AudioBufferView buffer, int readOffset, int channel) noexcept
{
    if (channel < 0 || channel >= MaxCallbackChannels)
        return nullptr;

    const int readChannel = readOffset + channel;
    if (buffer.read != nullptr &&
        readChannel >= 0 &&
        readChannel < buffer.inputChannels &&
        buffer.read[readChannel] != nullptr)
    {
        return buffer.read[readChannel];
    }

    if (buffer.write != nullptr &&
        channel >= 0 &&
        channel < buffer.outputChannels)
    {
        return buffer.write[channel];
    }

    return nullptr;
}

const float* currentChannelPointer(AudioBufferView buffer, int readOffset, int channel) noexcept
{
    if (channel < 0 || channel >= MaxCallbackChannels)
        return nullptr;

    if (buffer.write != nullptr &&
        channel < buffer.outputChannels &&
        buffer.write[channel] != nullptr)
    {
        return buffer.write[channel];
    }

    return sourceChannelPointer(buffer, readOffset, channel);
}

void markStereoPair(std::array<bool, MaxCallbackChannels>& active, int channel) noexcept
{
    if (channel < 0 || channel >= MaxCallbackChannels)
        return;

    const int base = channel - (channel % 2);
    active[static_cast<size_t>(base)] = true;
    if (base + 1 < MaxCallbackChannels)
        active[static_cast<size_t>(base + 1)] = true;
}

const float* readChannelPointer(AudioBufferView buffer, int channel) noexcept
{
    if (buffer.read == nullptr ||
        channel < 0 ||
        channel >= buffer.inputChannels ||
        channel >= MaxCallbackReadChannels)
    {
        return nullptr;
    }

    return buffer.read[channel];
}

const float* writeChannelPointer(AudioBufferView buffer, int channel) noexcept
{
    if (buffer.write == nullptr ||
        channel < 0 ||
        channel >= buffer.outputChannels ||
        channel >= MaxCallbackChannels)
    {
        return nullptr;
    }

    return buffer.write[channel];
}

int peakPercent(const float* samples, int sampleCount) noexcept
{
    if (samples == nullptr || sampleCount <= 0)
        return 0;

    float peak = 0.0f;
    for (int sample = 0; sample < sampleCount; ++sample)
    {
        const float value = samples[sample] < 0.0f ? -samples[sample] : samples[sample];
        if (value > peak)
            peak = value;
    }

    return std::clamp(static_cast<int>((peak * 100.0f) + 0.5f), 0, 1000);
}

struct ProbePeak
{
    int channel = -1;
    int percent = 0;
};

ProbePeak maxReadPeak(AudioBufferView buffer, int startChannel, int channelCount, int reportOffset) noexcept
{
    ProbePeak result {};
    const int first = std::clamp(startChannel, 0, MaxCallbackReadChannels);
    const int last = std::clamp(first + std::max(0, channelCount), first, std::min(buffer.inputChannels, MaxCallbackReadChannels));

    for (int channel = first; channel < last; ++channel)
    {
        const int peak = peakPercent(readChannelPointer(buffer, channel), buffer.samplesPerFrame);
        if (peak <= result.percent)
            continue;

        result.percent = peak;
        result.channel = channel - reportOffset;
    }

    return result;
}

ProbePeak maxWritePeak(AudioBufferView buffer, int channelCount) noexcept
{
    ProbePeak result {};
    const int last = std::clamp(channelCount, 0, std::min(buffer.outputChannels, MaxCallbackChannels));

    for (int channel = 0; channel < last; ++channel)
    {
        const int peak = peakPercent(writeChannelPointer(buffer, channel), buffer.samplesPerFrame);
        if (peak <= result.percent)
            continue;

        result.percent = peak;
        result.channel = channel;
    }

    return result;
}

int delayStreamIndex(CallbackStreamKind kind) noexcept
{
    if (kind == CallbackStreamKind::InputInsert)
        return 0;

    if (kind == CallbackStreamKind::OutputInsert)
        return 1;

    return 2;
}

float gainFromPercent(int percent) noexcept
{
    return static_cast<float>(std::clamp(percent, 0, 800)) / 100.0f;
}

int moveToward(int current, int target, int maximumStep) noexcept
{
    const int safeStep = std::max(1, maximumStep);
    if (current < target)
        return std::min(target, current + safeStep);

    if (current > target)
        return std::max(target, current - safeStep);

    return current;
}

int delaySmoothingStepSamples(int sampleRate, int blockSize) noexcept
{
    const int fromMilliseconds = sampleRate > 0
        ? (sampleRate * DelaySmoothingMilliseconds) / 1000
        : 0;
    return std::max(1, std::max(blockSize, fromMilliseconds));
}

size_t lineBufferOffset(int lineIndex, int length) noexcept
{
    return static_cast<size_t>(std::max(0, lineIndex)) *
        static_cast<size_t>(std::max(0, length));
}

size_t pluginBusFlatIndex(int slot, int pin) noexcept
{
    return static_cast<size_t>((slot * RealtimeEngine::MaxPluginPins) + pin);
}

class ScopedDenormalFlush
{
public:
    ScopedDenormalFlush() noexcept
        : previousControl(_mm_getcsr())
    {
        _mm_setcsr(previousControl | _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON);
    }

    ~ScopedDenormalFlush() noexcept
    {
        _mm_setcsr(previousControl);
    }

private:
    unsigned int previousControl = 0;
};
}

RealtimeEngine::RealtimeEngine() noexcept
{
    for (int channel = 0; channel < MaxChannels; ++channel)
    {
        inputGainPercent[channel].store(100, std::memory_order_relaxed);
        outputGainPercent[channel].store(100, std::memory_order_relaxed);
        inputDelayMilliseconds[channel].store(0, std::memory_order_relaxed);
        outputDelayMilliseconds[channel].store(0, std::memory_order_relaxed);
        inputEnabled[channel].store(true, std::memory_order_relaxed);
        outputEnabled[channel].store(true, std::memory_order_relaxed);
        inputPluginGraphEnabled[channel].store(false, std::memory_order_relaxed);
        outputPluginGraphEnabled[channel].store(false, std::memory_order_relaxed);
        mainPluginGraphEnabled[channel].store(false, std::memory_order_relaxed);
        for (auto& positions : delayWritePositions)
            positions[channel] = 0;
        for (auto& gains : smoothedChannelGains)
            gains[channel] = 1.0f;
        for (auto& delays : smoothedDelaySamples)
            delays[channel] = 0;
        for (auto& initialized : smoothedDelayInitialized)
            initialized[channel] = false;
    }

    for (int route = 0; route < MaxDirectRoutes; ++route)
    {
        inputDirectRoutes.gainPercent[route].store(100, std::memory_order_relaxed);
        outputDirectRoutes.gainPercent[route].store(100, std::memory_order_relaxed);
        mainDirectRoutes.gainPercent[route].store(100, std::memory_order_relaxed);
        routeReadPositions[route] = 0;
        for (auto& positions : routeWritePositions)
            positions[route].store(0, std::memory_order_relaxed);
        for (auto& primedSamples : routePrimedSamples)
            primedSamples[route].store(0, std::memory_order_relaxed);
        routeCounts[route] = 0;
        for (auto& gains : smoothedRouteGains)
            gains[route] = 1.0f;
    }

    rebuildDynamicPluginScratchBuffers();
}

void RealtimeEngine::setEnabled(bool shouldEnable) noexcept
{
    const auto kind = static_cast<CallbackStreamKind>(selectedKind.load(std::memory_order_acquire));
    auto& enabledBank = enableBankFor(kind);
    const int start = targetStart.load(std::memory_order_acquire);
    const int count = getSelectedChannelCount();

    for (int channel = start; channel < start + count; ++channel)
        enabledBank[channel].store(shouldEnable, std::memory_order_release);

    refreshDynamicBuffersForCurrentSampleRate();
}

bool RealtimeEngine::isEnabled() const noexcept
{
    const auto kind = static_cast<CallbackStreamKind>(selectedKind.load(std::memory_order_acquire));
    const auto& enabledBank = enableBankFor(kind);
    const int start = targetStart.load(std::memory_order_acquire);
    const int count = getSelectedChannelCount();

    for (int channel = start; channel < start + count; ++channel)
    {
        if (!enabledBank[channel].load(std::memory_order_acquire))
            return false;
    }

    return true;
}

void RealtimeEngine::setGainPercent(int percent) noexcept
{
    const int clamped = std::clamp(percent, 0, 200);
    const auto kind = static_cast<CallbackStreamKind>(selectedKind.load(std::memory_order_acquire));
    auto& gainBank = gainBankFor(kind);
    const int start = targetStart.load(std::memory_order_acquire);
    const int count = getSelectedChannelCount();

    for (int channel = start; channel < start + count; ++channel)
        gainBank[channel].store(clamped, std::memory_order_release);
}

int RealtimeEngine::getGainPercent() const noexcept
{
    const auto kind = static_cast<CallbackStreamKind>(selectedKind.load(std::memory_order_acquire));
    const auto& gainBank = gainBankFor(kind);
    const int start = targetStart.load(std::memory_order_acquire);
    return gainBank[start].load(std::memory_order_acquire);
}

void RealtimeEngine::setChannelEnabled(CallbackStreamKind kind, int channel, bool shouldEnable) noexcept
{
    if (channel < 0 || channel >= MaxChannels)
        return;

    enableBankFor(kind)[channel].store(shouldEnable, std::memory_order_release);
    refreshDynamicBuffersForCurrentSampleRate();
}

bool RealtimeEngine::isChannelEnabled(CallbackStreamKind kind, int channel) const noexcept
{
    if (channel < 0 || channel >= MaxChannels)
        return false;

    return enableBankFor(kind)[channel].load(std::memory_order_acquire);
}

void RealtimeEngine::setChannelGainPercent(CallbackStreamKind kind, int channel, int percent) noexcept
{
    if (channel < 0 || channel >= MaxChannels)
        return;

    gainBankFor(kind)[channel].store(std::clamp(percent, 0, 200), std::memory_order_release);
}

int RealtimeEngine::getChannelGainPercent(CallbackStreamKind kind, int channel) const noexcept
{
    if (channel < 0 || channel >= MaxChannels)
        return 100;

    return gainBankFor(kind)[channel].load(std::memory_order_acquire);
}

void RealtimeEngine::setChannelDelayMilliseconds(CallbackStreamKind kind, int channel, int milliseconds) noexcept
{
    if (channel < 0 || channel >= MaxChannels)
        return;

    delayBankFor(kind)[channel].store(std::clamp(milliseconds, 0, MaxDelayMilliseconds), std::memory_order_release);
    refreshDynamicBuffersForCurrentSampleRate();
}

int RealtimeEngine::getChannelDelayMilliseconds(CallbackStreamKind kind, int channel) const noexcept
{
    if (channel < 0 || channel >= MaxChannels)
        return 0;

    return delayBankFor(kind)[channel].load(std::memory_order_acquire);
}

void RealtimeEngine::setChannelPluginGraphEnabled(CallbackStreamKind kind, int channel, bool shouldEnable) noexcept
{
    if (channel < 0 || channel >= MaxChannels)
        return;

    pluginGraphBankFor(kind)[channel].store(shouldEnable, std::memory_order_release);
}

bool RealtimeEngine::isChannelPluginGraphEnabled(CallbackStreamKind kind, int channel) const noexcept
{
    if (channel < 0 || channel >= MaxChannels)
        return false;

    return pluginGraphBankFor(kind)[channel].load(std::memory_order_acquire);
}

void RealtimeEngine::setInputCallbackSuppressedChannel(int channel, bool shouldSuppress) noexcept
{
    if (channel < 0 || channel >= MaxChannels)
        return;

    inputCallbackSuppressed[channel].store(shouldSuppress, std::memory_order_release);
}

bool RealtimeEngine::prepareDelayBuffers(int requestedSampleRate) noexcept
{
    return rebuildDynamicBuffers(requestedSampleRate);
}

bool RealtimeEngine::rebuildDynamicBuffers(int requestedSampleRate) noexcept
{
    const int preparedSampleRate = std::clamp(requestedSampleRate, 8000, 192000);
    const int requiredLength = static_cast<int>(
        ((static_cast<int64_t>(preparedSampleRate) * MaxDelayMilliseconds) / 1000) + 1);

    std::shared_ptr<DynamicAudioBuffers> next;
    try
    {
        next = std::make_shared<DynamicAudioBuffers>();
    }
    catch (...)
    {
        return false;
    }

    next->sampleRate = preparedSampleRate;

    for (auto& indexes : next->delayLineIndexes)
        indexes.fill(-1);
    for (auto& indexes : next->routeLineIndexes)
        indexes.fill(-1);

    const auto addDelayLine = [&](int stream, int channel) noexcept {
        if (stream < 0 || stream >= DelayStreamCount || channel < 0 || channel >= MaxChannels)
            return;

        next->delayLineIndexes[static_cast<size_t>(stream)][static_cast<size_t>(channel)] =
            next->delayLineCount++;
    };

    for (int channel = 0; channel < MaxChannels; ++channel)
    {
        const bool inputDelayActive =
            inputEnabled[static_cast<size_t>(channel)].load(std::memory_order_acquire) &&
            inputDelayMilliseconds[static_cast<size_t>(channel)].load(std::memory_order_acquire) > 0;
        if (inputDelayActive)
        {
            addDelayLine(0, channel);
            addDelayLine(3, channel);
        }

        const bool outputDelayActive =
            outputEnabled[static_cast<size_t>(channel)].load(std::memory_order_acquire) &&
            outputDelayMilliseconds[static_cast<size_t>(channel)].load(std::memory_order_acquire) > 0;
        if (outputDelayActive)
        {
            addDelayLine(1, channel);
            addDelayLine(2, channel);
        }
    }

    bool routeDelayActive = false;
    const auto addRouteLine = [&](int stream, int route, bool needsDelayLine) noexcept {
        if (stream < 0 || stream >= DelayStreamCount || route < 0 || route >= MaxDirectRoutes)
            return;

        next->routeLineIndexes[static_cast<size_t>(stream)][static_cast<size_t>(route)] =
            next->routeLineCount++;
        routeDelayActive = routeDelayActive || needsDelayLine;
    };

    const int inputRouteCount = std::clamp(inputDirectRoutes.routeCount.load(std::memory_order_acquire), 0, MaxDirectRoutes);
    for (int route = 0; route < inputRouteCount; ++route)
    {
        const int source = inputDirectRoutes.sourceChannels[static_cast<size_t>(route)].load(std::memory_order_acquire);
        const int destination = inputDirectRoutes.destinationChannels[static_cast<size_t>(route)].load(std::memory_order_acquire);
        if (source < 0 || source >= MaxChannels || destination < 0 || destination >= MaxChannels)
            continue;

        const int delayMs = inputDirectRoutes.delayMilliseconds[static_cast<size_t>(route)].load(std::memory_order_acquire);
        addRouteLine(0, route, delayMs > 0);
    }

    const int outputRouteCount = std::clamp(outputDirectRoutes.routeCount.load(std::memory_order_acquire), 0, MaxDirectRoutes);
    for (int route = 0; route < outputRouteCount; ++route)
    {
        const int source = outputDirectRoutes.sourceChannels[static_cast<size_t>(route)].load(std::memory_order_acquire);
        const int destination = outputDirectRoutes.destinationChannels[static_cast<size_t>(route)].load(std::memory_order_acquire);
        const int delayMs = outputDirectRoutes.delayMilliseconds[static_cast<size_t>(route)].load(std::memory_order_acquire);
        if (delayMs <= 0 || source < 0 || source >= MaxChannels || destination < 0 || destination >= MaxChannels)
            continue;

        addRouteLine(1, route, true);
    }

    const int mainRouteCount = std::clamp(mainDirectRoutes.routeCount.load(std::memory_order_acquire), 0, MaxDirectRoutes);
    for (int route = 0; route < mainRouteCount; ++route)
    {
        const int source = mainDirectRoutes.sourceChannels[static_cast<size_t>(route)].load(std::memory_order_acquire);
        const int destination = mainDirectRoutes.destinationChannels[static_cast<size_t>(route)].load(std::memory_order_acquire);
        const int delayMs = mainDirectRoutes.delayMilliseconds[static_cast<size_t>(route)].load(std::memory_order_acquire);
        if (delayMs <= 0 || source < 0 || source >= MaxChannels || destination < 0 || destination >= MaxChannels)
            continue;

        addRouteLine(2, route, true);
    }

    next->delayLength = next->delayLineCount > 0 ? requiredLength : 0;
    next->routeLength = next->routeLineCount > 0
        ? (routeDelayActive ? std::max(requiredLength, MaxPluginScratchSamples) : MaxPluginScratchSamples)
        : 0;

    const auto current = dynamicBuffers.load(std::memory_order_acquire);
    if (current != nullptr &&
        current->sampleRate == next->sampleRate &&
        current->delayLength == next->delayLength &&
        current->routeLength == next->routeLength &&
        current->delayLineCount == next->delayLineCount &&
        current->routeLineCount == next->routeLineCount &&
        current->delayLineIndexes == next->delayLineIndexes &&
        current->routeLineIndexes == next->routeLineIndexes)
    {
        delayBufferSampleRate.store(preparedSampleRate, std::memory_order_release);
        delayBufferLength.store(next->delayLength, std::memory_order_release);
        routeBufferLength.store(next->routeLength, std::memory_order_release);
        delayBufferLineCount.store(next->delayLineCount, std::memory_order_release);
        routeBufferLineCount.store(next->routeLineCount, std::memory_order_release);
        return true;
    }

    try
    {
        if (next->delayLineCount > 0 && next->delayLength > 0)
        {
            next->delayBuffer.assign(
                static_cast<size_t>(next->delayLineCount) *
                    static_cast<size_t>(next->delayLength),
                0.0f);
        }

        if (next->routeLineCount > 0 && next->routeLength > 0)
        {
            next->routeBuffer.assign(
                static_cast<size_t>(next->routeLineCount) *
                    static_cast<size_t>(next->routeLength),
                0.0f);
        }
    }
    catch (...)
    {
        return false;
    }

    for (auto& positions : delayWritePositions)
    {
        for (auto& writePosition : positions)
            writePosition = 0;
    }

    for (auto& delays : smoothedDelaySamples)
    {
        for (auto& delay : delays)
            delay = 0;
    }

    for (auto& initialized : smoothedDelayInitialized)
    {
        for (auto&& state : initialized)
            state = false;
    }

    for (int route = 0; route < MaxDirectRoutes; ++route)
    {
        routeReadPositions[route] = 0;
        for (auto& positions : routeWritePositions)
            positions[route].store(0, std::memory_order_relaxed);
        for (auto& primedSamples : routePrimedSamples)
            primedSamples[route].store(0, std::memory_order_relaxed);
        routeCounts[route] = 0;
    }

    dynamicBuffers.store(next, std::memory_order_release);
    delayBufferLength.store(next->delayLength, std::memory_order_release);
    delayBufferSampleRate.store(preparedSampleRate, std::memory_order_release);
    routeBufferLength.store(next->routeLength, std::memory_order_release);
    delayBufferLineCount.store(next->delayLineCount, std::memory_order_release);
    routeBufferLineCount.store(next->routeLineCount, std::memory_order_release);
    return true;
}

void RealtimeEngine::refreshDynamicBuffersForCurrentSampleRate() noexcept
{
    int preparedSampleRate = delayBufferSampleRate.load(std::memory_order_acquire);
    if (preparedSampleRate <= 0)
        preparedSampleRate = sampleRate.load(std::memory_order_acquire);

    if (preparedSampleRate > 0)
        rebuildDynamicBuffers(preparedSampleRate);
}

bool RealtimeEngine::rebuildDynamicPluginScratchBuffers() noexcept
{
    std::shared_ptr<DynamicPluginScratchBuffers> next;
    try
    {
        next = std::make_shared<DynamicPluginScratchBuffers>();
    }
    catch (...)
    {
        return false;
    }

    next->pluginBusLineIndexes.fill(-1);
    next->passthroughRouteCapacities.fill(0);
    next->passthroughRouteStartLines.fill(-1);

    const auto markPluginBusLine = [&](int slot, int pin) noexcept {
        if (slot < 0 || slot >= MaxPluginSlots || pin < 0 || pin >= MaxPluginPins)
            return;

        const auto index = pluginBusFlatIndex(slot, pin);
        if (index >= next->pluginBusLineIndexes.size() ||
            next->pluginBusLineIndexes[index] >= 0)
        {
            return;
        }

        next->pluginBusLineIndexes[index] = next->pluginBusLineCount++;
    };

    for (int slot = 0; slot < MaxPluginSlots; ++slot)
    {
        const auto& pluginSlot = pluginSlots[static_cast<size_t>(slot)];
        const bool active =
            pluginSlot.enabled.load(std::memory_order_acquire) &&
            pluginSlot.processor.load(std::memory_order_acquire) != nullptr;
        if (!active)
            continue;

        const int inputRouteCount = std::clamp(pluginSlot.inputRouteCount.load(std::memory_order_acquire), 0, MaxPluginRoutes);
        for (int route = 0; route < inputRouteCount; ++route)
        {
            const int sourceKind = pluginSlot.inputSourceKinds[static_cast<size_t>(route)].load(std::memory_order_acquire);
            if (sourceKind != static_cast<int>(PluginRouteEndpointKind::PluginPin))
                continue;

            const int sourceSlot = pluginSlot.inputSourceSlots[static_cast<size_t>(route)].load(std::memory_order_acquire);
            const int sourcePin = pluginSlot.inputSourcePins[static_cast<size_t>(route)].load(std::memory_order_acquire);
            markPluginBusLine(sourceSlot, sourcePin);
        }

        const int outputRouteCount = std::clamp(pluginSlot.outputRouteCount.load(std::memory_order_acquire), 0, MaxPluginRoutes);
        for (int route = 0; route < outputRouteCount; ++route)
        {
            const int destinationKind = pluginSlot.outputDestinationKinds[static_cast<size_t>(route)].load(std::memory_order_acquire);
            if (destinationKind != static_cast<int>(PluginRouteEndpointKind::PluginPin))
                continue;

            const int pin = pluginSlot.outputPluginPins[static_cast<size_t>(route)].load(std::memory_order_acquire);
            markPluginBusLine(slot, pin);
        }
    }

    const auto passthroughRouteCapacity = [](const DirectRouteBank& bank) noexcept {
        return std::clamp(bank.routeCount.load(std::memory_order_acquire), 0, MaxDirectRoutes);
    };

    next->passthroughRouteCapacities[0] = passthroughRouteCapacity(inputPluginPassthroughRoutes);
    next->passthroughRouteCapacities[1] = passthroughRouteCapacity(outputPluginPassthroughRoutes);
    next->passthroughRouteCapacities[2] = passthroughRouteCapacity(mainPluginPassthroughRoutes);
    next->passthroughRouteCapacities[3] = next->passthroughRouteCapacities[0];

    for (int stream = 0; stream < DelayStreamCount; ++stream)
    {
        const int capacity = next->passthroughRouteCapacities[static_cast<size_t>(stream)];
        if (capacity <= 0)
            continue;

        next->passthroughRouteStartLines[static_cast<size_t>(stream)] = next->passthroughLineCount;
        next->passthroughLineCount += capacity;
    }

    const auto current = dynamicPluginScratchBuffers.load(std::memory_order_acquire);
    if (current != nullptr &&
        current->pluginBusLineCount == next->pluginBusLineCount &&
        current->passthroughLineCount == next->passthroughLineCount &&
        current->pluginBusLineIndexes == next->pluginBusLineIndexes &&
        current->passthroughRouteCapacities == next->passthroughRouteCapacities &&
        current->passthroughRouteStartLines == next->passthroughRouteStartLines)
    {
        return true;
    }

    try
    {
        if (next->pluginBusLineCount > 0)
        {
            next->pluginBusBuffer.assign(
                static_cast<size_t>(next->pluginBusLineCount) *
                    static_cast<size_t>(MaxPluginScratchSamples),
                0.0f);
        }

        if (next->passthroughLineCount > 0)
        {
            next->pluginPassthroughScratchBuffer.assign(
                static_cast<size_t>(next->passthroughLineCount) *
                    static_cast<size_t>(MaxPluginScratchSamples),
                0.0f);
        }
    }
    catch (...)
    {
        return false;
    }

    dynamicPluginScratchBuffers.store(next, std::memory_order_release);
    return true;
}

void RealtimeEngine::refreshDynamicPluginScratchBuffers() noexcept
{
    rebuildDynamicPluginScratchBuffers();
}

int RealtimeEngine::getDelayBufferSampleRate() const noexcept
{
    return delayBufferSampleRate.load(std::memory_order_acquire);
}

void RealtimeEngine::setTargetRange(CallbackStreamKind kind, int startChannel, int channelCount) noexcept
{
    const int start = std::clamp(startChannel, 0, MaxChannels - 1);
    selectedKind.store(static_cast<int>(kind), std::memory_order_release);
    targetStart.store(start, std::memory_order_release);
    targetCount.store(clampChannelCount(start, channelCount), std::memory_order_release);
}

void RealtimeEngine::setDirectRoutes(CallbackStreamKind kind, const DirectAudioRoute* routes, int routeCount) noexcept
{
    auto& bank = directRouteBankFor(kind);
    const int previousCount = std::clamp(bank.routeCount.load(std::memory_order_acquire), 0, MaxDirectRoutes);
    const int safeCount = routes != nullptr ? std::clamp(routeCount, 0, MaxDirectRoutes) : 0;
    const int streamIndex = delayStreamIndex(kind);
    for (int route = 0; route < safeCount; ++route)
    {
        const int oldSource = route < previousCount
            ? bank.sourceChannels[static_cast<size_t>(route)].load(std::memory_order_relaxed)
            : -1;
        const int oldDestination = route < previousCount
            ? bank.destinationChannels[static_cast<size_t>(route)].load(std::memory_order_relaxed)
            : -1;
        const int oldDelay = route < previousCount
            ? bank.delayMilliseconds[static_cast<size_t>(route)].load(std::memory_order_relaxed)
            : 0;
        const int newDelay = std::clamp(routes[route].delayMilliseconds, 0, MaxDelayMilliseconds);
        const int newGain = std::clamp(routes[route].gainPercent, 0, 800);
        const bool routePathChanged =
            route >= previousCount ||
            oldSource != routes[route].sourceChannel ||
            oldDestination != routes[route].destinationChannel;
        const bool delayLineBecameActive = oldDelay <= 0 && newDelay > 0;

        if (routePathChanged || delayLineBecameActive)
        {
            routeReadPositions[static_cast<size_t>(route)] = 0;
            routeCounts[route] = 0;
            routeWritePositions[static_cast<size_t>(streamIndex)][static_cast<size_t>(route)].store(0, std::memory_order_relaxed);
            routePrimedSamples[static_cast<size_t>(streamIndex)][static_cast<size_t>(route)].store(0, std::memory_order_relaxed);
            smoothedRouteGains[static_cast<size_t>(streamIndex)][static_cast<size_t>(route)] = gainFromPercent(newGain);
        }

        bank.sourceChannels[static_cast<size_t>(route)].store(routes[route].sourceChannel, std::memory_order_relaxed);
        bank.destinationChannels[static_cast<size_t>(route)].store(routes[route].destinationChannel, std::memory_order_relaxed);
        bank.delayMilliseconds[static_cast<size_t>(route)].store(newDelay, std::memory_order_relaxed);
        bank.gainPercent[static_cast<size_t>(route)].store(newGain, std::memory_order_relaxed);
        bank.muteSource[static_cast<size_t>(route)].store(routes[route].muteSource, std::memory_order_relaxed);
    }

    bank.routeCount.store(safeCount, std::memory_order_release);
    refreshDynamicBuffersForCurrentSampleRate();
}

void RealtimeEngine::setPluginPassthroughRoutes(CallbackStreamKind kind, const DirectAudioRoute* routes, int routeCount) noexcept
{
    auto& bank = pluginPassthroughBankFor(kind);
    const int safeCount = routes != nullptr ? std::clamp(routeCount, 0, MaxDirectRoutes) : 0;
    for (int route = 0; route < safeCount; ++route)
    {
        bank.sourceChannels[static_cast<size_t>(route)].store(routes[route].sourceChannel, std::memory_order_relaxed);
        bank.destinationChannels[static_cast<size_t>(route)].store(routes[route].destinationChannel, std::memory_order_relaxed);
    }

    bank.routeCount.store(safeCount, std::memory_order_release);
    refreshDynamicPluginScratchBuffers();
}

bool RealtimeEngine::startSinglePing(int inputChannel, int outputChannel, int timeoutMilliseconds) noexcept
{
    if (inputChannel < 0 ||
        inputChannel >= MaxChannels ||
        outputChannel < 0 ||
        outputChannel >= MaxChannels)
    {
        return false;
    }

    const int safeTimeoutMilliseconds = std::clamp(timeoutMilliseconds, 100, MaxDelayMilliseconds + 2000);
    const int currentSampleRate = std::max(8000, sampleRate.load(std::memory_order_acquire));
    singlePingInputChannel.store(inputChannel, std::memory_order_relaxed);
    singlePingOutputChannel.store(outputChannel, std::memory_order_relaxed);
    singlePingSampleRate.store(currentSampleRate, std::memory_order_relaxed);
    singlePingLatencySamples.store(-1, std::memory_order_relaxed);
    singlePingElapsedSamples.store(0, std::memory_order_relaxed);
    singlePingPeakPercent.store(0, std::memory_order_relaxed);
    singlePingTimeoutMilliseconds.store(safeTimeoutMilliseconds, std::memory_order_relaxed);
    singlePingTimeoutSamples.store(
        static_cast<int>((static_cast<int64_t>(safeTimeoutMilliseconds) * currentSampleRate) / 1000),
        std::memory_order_relaxed);
    singlePingPulsePosition.store(0, std::memory_order_relaxed);
    singlePingStatus.store(PingStatusArmed, std::memory_order_release);
    return true;
}

SinglePingResult RealtimeEngine::singlePingResult() const noexcept
{
    return SinglePingResult {
        singlePingStatus.load(std::memory_order_acquire),
        singlePingInputChannel.load(std::memory_order_relaxed),
        singlePingOutputChannel.load(std::memory_order_relaxed),
        singlePingSampleRate.load(std::memory_order_relaxed),
        singlePingLatencySamples.load(std::memory_order_relaxed),
        singlePingElapsedSamples.load(std::memory_order_relaxed),
        singlePingPeakPercent.load(std::memory_order_relaxed),
        singlePingTimeoutMilliseconds.load(std::memory_order_relaxed)
    };
}

void RealtimeEngine::clearPluginSlots() noexcept
{
    for (int slot = 0; slot < MaxPluginSlots; ++slot)
        clearPluginSlot(slot);
}

void RealtimeEngine::setPluginSlot(
    int slot,
    RealtimePluginProcessor* processor,
    CallbackStreamKind kind,
    const PluginInputRoute* inputRoutes,
    int inputRouteCount,
    const PluginOutputRoute* outputRoutes,
    int outputRouteCount,
    bool enabled,
    bool bypassed) noexcept
{
    if (slot < 0 || slot >= MaxPluginSlots)
        return;

    auto& target = pluginSlots[static_cast<size_t>(slot)];
    target.enabled.store(false, std::memory_order_release);
    target.kind.store(static_cast<int>(kind), std::memory_order_release);
    target.processor.store(processor, std::memory_order_release);
    target.bypassed.store(bypassed, std::memory_order_release);
    setPluginSlotRoutes(slot, inputRoutes, inputRouteCount, outputRoutes, outputRouteCount);
    target.enabled.store(processor != nullptr && enabled, std::memory_order_release);
    refreshDynamicPluginScratchBuffers();
}

void RealtimeEngine::clearPluginSlot(int slot) noexcept
{
    if (slot < 0 || slot >= MaxPluginSlots)
        return;

    auto& target = pluginSlots[static_cast<size_t>(slot)];
    target.enabled.store(false, std::memory_order_release);
    target.processor.store(nullptr, std::memory_order_release);
    target.bypassed.store(false, std::memory_order_release);
    target.inputRouteCount.store(0, std::memory_order_release);
    target.outputRouteCount.store(0, std::memory_order_release);
    refreshDynamicPluginScratchBuffers();
}

void RealtimeEngine::setPluginSlotRoutes(
    int slot,
    const PluginInputRoute* inputRoutes,
    int inputRouteCount,
    const PluginOutputRoute* outputRoutes,
    int outputRouteCount) noexcept
{
    if (slot < 0 || slot >= MaxPluginSlots)
        return;

    auto& target = pluginSlots[static_cast<size_t>(slot)];
    const bool wasEnabled = target.enabled.load(std::memory_order_acquire);
    target.enabled.store(false, std::memory_order_release);

    const int safeInputCount = inputRoutes != nullptr ? std::clamp(inputRouteCount, 0, MaxPluginRoutes) : 0;
    const int safeOutputCount = outputRoutes != nullptr ? std::clamp(outputRouteCount, 0, MaxPluginRoutes) : 0;

    for (int i = 0; i < safeInputCount; ++i)
    {
        target.inputSourceKinds[static_cast<size_t>(i)].store(inputRoutes[i].sourceKind, std::memory_order_relaxed);
        target.inputSourceChannels[static_cast<size_t>(i)].store(inputRoutes[i].sourceChannel, std::memory_order_relaxed);
        target.inputSourceSlots[static_cast<size_t>(i)].store(inputRoutes[i].sourceSlot, std::memory_order_relaxed);
        target.inputSourcePins[static_cast<size_t>(i)].store(inputRoutes[i].sourcePin, std::memory_order_relaxed);
        target.inputPluginPins[static_cast<size_t>(i)].store(inputRoutes[i].pluginPin, std::memory_order_relaxed);
    }

    for (int i = 0; i < safeOutputCount; ++i)
    {
        target.outputDestinationKinds[static_cast<size_t>(i)].store(outputRoutes[i].destinationKind, std::memory_order_relaxed);
        target.outputPluginPins[static_cast<size_t>(i)].store(outputRoutes[i].pluginPin, std::memory_order_relaxed);
        target.outputDestinationChannels[static_cast<size_t>(i)].store(outputRoutes[i].destinationChannel, std::memory_order_relaxed);
        target.outputDestinationSlots[static_cast<size_t>(i)].store(outputRoutes[i].destinationSlot, std::memory_order_relaxed);
        target.outputDestinationPins[static_cast<size_t>(i)].store(outputRoutes[i].destinationPin, std::memory_order_relaxed);
    }

    target.inputRouteCount.store(safeInputCount, std::memory_order_release);
    target.outputRouteCount.store(safeOutputCount, std::memory_order_release);
    target.enabled.store(wasEnabled, std::memory_order_release);
    refreshDynamicPluginScratchBuffers();
}

void RealtimeEngine::setPluginSlotEnabled(int slot, bool shouldEnable) noexcept
{
    if (slot < 0 || slot >= MaxPluginSlots)
        return;

    auto& target = pluginSlots[static_cast<size_t>(slot)];
    target.enabled.store(target.processor.load(std::memory_order_acquire) != nullptr && shouldEnable, std::memory_order_release);
    refreshDynamicPluginScratchBuffers();
}

bool RealtimeEngine::isPluginSlotEnabled(int slot) const noexcept
{
    if (slot < 0 || slot >= MaxPluginSlots)
        return false;

    return pluginSlots[static_cast<size_t>(slot)].enabled.load(std::memory_order_acquire);
}

void RealtimeEngine::setProbeChannels(int inputChannel, int outputChannel) noexcept
{
    probeInputChannel.store(std::clamp(inputChannel, 0, MaxChannels - 1), std::memory_order_release);
    probeOutputChannel.store(std::clamp(outputChannel, 0, MaxChannels - 1), std::memory_order_release);
    inputInsertReadPeakPercent.store(0, std::memory_order_relaxed);
    inputInsertWritePeakPercent.store(0, std::memory_order_relaxed);
    mainInputReadPeakPercent.store(0, std::memory_order_relaxed);
    mainOutputReadPeakPercent.store(0, std::memory_order_relaxed);
    mainWritePeakPercent.store(0, std::memory_order_relaxed);
    outputInsertReadPeakPercent.store(0, std::memory_order_relaxed);
    outputInsertWritePeakPercent.store(0, std::memory_order_relaxed);
    inputInsertMaxReadPeakPercent.store(0, std::memory_order_relaxed);
    inputInsertMaxReadChannel.store(-1, std::memory_order_relaxed);
    inputInsertMaxWritePeakPercent.store(0, std::memory_order_relaxed);
    inputInsertMaxWriteChannel.store(-1, std::memory_order_relaxed);
    mainSourceMaxReadPeakPercent.store(0, std::memory_order_relaxed);
    mainSourceMaxReadChannel.store(-1, std::memory_order_relaxed);
    mainBusMaxReadPeakPercent.store(0, std::memory_order_relaxed);
    mainBusMaxReadChannel.store(-1, std::memory_order_relaxed);
    mainMaxWritePeakPercent.store(0, std::memory_order_relaxed);
    mainMaxWriteChannel.store(-1, std::memory_order_relaxed);
    outputInsertMaxReadPeakPercent.store(0, std::memory_order_relaxed);
    outputInsertMaxReadChannel.store(-1, std::memory_order_relaxed);
    outputInsertMaxWritePeakPercent.store(0, std::memory_order_relaxed);
    outputInsertMaxWriteChannel.store(-1, std::memory_order_relaxed);
}

void RealtimeEngine::updateFormat(int newSampleRate, int newBlockSize) noexcept
{
    sampleRate.store(newSampleRate, std::memory_order_release);
    blockSize.store(newBlockSize, std::memory_order_release);
}

void RealtimeEngine::process(AudioBufferView buffer, CallbackStreamKind kind) noexcept
{
    processInternal(buffer, kind, true, delayStreamIndex(kind));
}

void RealtimeEngine::processInsertAsio(AudioBufferView buffer) noexcept
{
    processInternal(buffer, CallbackStreamKind::InputInsert, false, 3);
}

void RealtimeEngine::processInternal(
    AudioBufferView buffer,
    CallbackStreamKind kind,
    bool enableInputOutputRoutes,
    int delayStream) noexcept
{
    const ScopedDenormalFlush denormalFlush;

    LARGE_INTEGER start {};
    LARGE_INTEGER end {};
    QueryPerformanceCounter(&start);

    sampleRate.store(buffer.sampleRate, std::memory_order_relaxed);
    blockSize.store(buffer.samplesPerFrame, std::memory_order_relaxed);
    inputChannels.store(buffer.inputChannels, std::memory_order_relaxed);
    outputChannels.store(buffer.outputChannels, std::memory_order_relaxed);

    const int readOffset = getReadOffset(buffer, kind);
    const bool suppressInputCallbackChannels = kind == CallbackStreamKind::InputInsert && enableInputOutputRoutes;
    std::array<bool, MaxChannels> pluginOutputWritten {};

    captureProbeRead(buffer, kind, readOffset);
    copyPassthrough(buffer, readOffset, suppressInputCallbackChannels);
    if (kind == CallbackStreamKind::InputInsert)
        processSinglePingInput(buffer);

    applyConfiguredDelays(buffer, kind, readOffset, delayStream, suppressInputCallbackChannels);
    const int sourceReadOffset = kind == CallbackStreamKind::Main ? 0 : readOffset;
    const bool pluginsProcessed = !pluginProcessingActive.exchange(true, std::memory_order_acquire);
    if (pluginsProcessed)
    {
        struct PluginProcessingGuard
        {
            std::atomic<bool>& active;

            ~PluginProcessingGuard()
            {
                active.store(false, std::memory_order_release);
            }
        };

        PluginProcessingGuard guard { pluginProcessingActive };
        applyPlugins(buffer, kind, sourceReadOffset, suppressInputCallbackChannels, pluginOutputWritten);
    }

    applyPluginPassthroughRoutes(buffer, kind, sourceReadOffset, delayStream, suppressInputCallbackChannels, pluginOutputWritten);
    if (pluginsProcessed)
        applyPluginGraphGate(buffer, kind, suppressInputCallbackChannels, pluginOutputWritten);

    applyDirectRoutes(buffer, kind, sourceReadOffset, enableInputOutputRoutes, suppressInputCallbackChannels, pluginOutputWritten);
    applyConfiguredGains(buffer, kind, delayStream, suppressInputCallbackChannels);
    if (kind == CallbackStreamKind::OutputInsert)
        processSinglePingOutput(buffer);
    captureProbeWrite(buffer, kind, readOffset);

    QueryPerformanceCounter(&end);
    publishTiming(elapsedMicroseconds(start, end), buffer);
}

RealtimeStats RealtimeEngine::getStats() const noexcept
{
    return RealtimeStats {
        sampleRate.load(std::memory_order_acquire),
        blockSize.load(std::memory_order_acquire),
        inputChannels.load(std::memory_order_acquire),
        outputChannels.load(std::memory_order_acquire),
        callbackCount.load(std::memory_order_acquire),
        lastProcessUsec.load(std::memory_order_acquire),
        peakProcessUsec.load(std::memory_order_acquire),
        callbackCpuPercent.load(std::memory_order_acquire),
        delayBufferSampleRate.load(std::memory_order_acquire),
        probeInputChannel.load(std::memory_order_acquire),
        probeOutputChannel.load(std::memory_order_acquire),
        inputInsertReadPeakPercent.load(std::memory_order_acquire),
        inputInsertWritePeakPercent.load(std::memory_order_acquire),
        mainInputReadPeakPercent.load(std::memory_order_acquire),
        mainOutputReadPeakPercent.load(std::memory_order_acquire),
        mainWritePeakPercent.load(std::memory_order_acquire),
        outputInsertReadPeakPercent.load(std::memory_order_acquire),
        outputInsertWritePeakPercent.load(std::memory_order_acquire),
        inputInsertMaxReadPeakPercent.load(std::memory_order_acquire),
        inputInsertMaxReadChannel.load(std::memory_order_acquire),
        inputInsertMaxWritePeakPercent.load(std::memory_order_acquire),
        inputInsertMaxWriteChannel.load(std::memory_order_acquire),
        mainSourceMaxReadPeakPercent.load(std::memory_order_acquire),
        mainSourceMaxReadChannel.load(std::memory_order_acquire),
        mainBusMaxReadPeakPercent.load(std::memory_order_acquire),
        mainBusMaxReadChannel.load(std::memory_order_acquire),
        mainMaxWritePeakPercent.load(std::memory_order_acquire),
        mainMaxWriteChannel.load(std::memory_order_acquire),
        outputInsertMaxReadPeakPercent.load(std::memory_order_acquire),
        outputInsertMaxReadChannel.load(std::memory_order_acquire),
        outputInsertMaxWritePeakPercent.load(std::memory_order_acquire),
        outputInsertMaxWriteChannel.load(std::memory_order_acquire),
        inputInsertInputChannels.load(std::memory_order_acquire),
        inputInsertOutputChannels.load(std::memory_order_acquire),
        mainInputChannels.load(std::memory_order_acquire),
        mainOutputChannels.load(std::memory_order_acquire),
        outputInsertInputChannels.load(std::memory_order_acquire),
        outputInsertOutputChannels.load(std::memory_order_acquire)
    };
}

int RealtimeEngine::getReadOffset(AudioBufferView buffer, CallbackStreamKind kind) const noexcept
{
    if (kind != CallbackStreamKind::Main)
        return 0;

    return std::max(0, buffer.inputChannels - buffer.outputChannels);
}

int RealtimeEngine::getSelectedChannelCount() const noexcept
{
    const int start = targetStart.load(std::memory_order_acquire);
    return clampChannelCount(start, targetCount.load(std::memory_order_acquire));
}

int RealtimeEngine::clampChannelCount(int start, int count) const noexcept
{
    const int safeStart = std::clamp(start, 0, MaxChannels - 1);
    return std::clamp(count, 1, MaxChannels - safeStart);
}

RealtimeEngine::GainBank& RealtimeEngine::gainBankFor(CallbackStreamKind kind) noexcept
{
    return kind == CallbackStreamKind::InputInsert ? inputGainPercent : outputGainPercent;
}

const RealtimeEngine::GainBank& RealtimeEngine::gainBankFor(CallbackStreamKind kind) const noexcept
{
    return kind == CallbackStreamKind::InputInsert ? inputGainPercent : outputGainPercent;
}

RealtimeEngine::DelayBank& RealtimeEngine::delayBankFor(CallbackStreamKind kind) noexcept
{
    return kind == CallbackStreamKind::InputInsert ? inputDelayMilliseconds : outputDelayMilliseconds;
}

const RealtimeEngine::DelayBank& RealtimeEngine::delayBankFor(CallbackStreamKind kind) const noexcept
{
    return kind == CallbackStreamKind::InputInsert ? inputDelayMilliseconds : outputDelayMilliseconds;
}

RealtimeEngine::EnableBank& RealtimeEngine::enableBankFor(CallbackStreamKind kind) noexcept
{
    return kind == CallbackStreamKind::InputInsert ? inputEnabled : outputEnabled;
}

const RealtimeEngine::EnableBank& RealtimeEngine::enableBankFor(CallbackStreamKind kind) const noexcept
{
    return kind == CallbackStreamKind::InputInsert ? inputEnabled : outputEnabled;
}

RealtimeEngine::EnableBank& RealtimeEngine::pluginGraphBankFor(CallbackStreamKind kind) noexcept
{
    if (kind == CallbackStreamKind::InputInsert)
        return inputPluginGraphEnabled;

    if (kind == CallbackStreamKind::Main)
        return mainPluginGraphEnabled;

    return outputPluginGraphEnabled;
}

const RealtimeEngine::EnableBank& RealtimeEngine::pluginGraphBankFor(CallbackStreamKind kind) const noexcept
{
    if (kind == CallbackStreamKind::InputInsert)
        return inputPluginGraphEnabled;

    if (kind == CallbackStreamKind::Main)
        return mainPluginGraphEnabled;

    return outputPluginGraphEnabled;
}

RealtimeEngine::DirectRouteBank& RealtimeEngine::directRouteBankFor(CallbackStreamKind kind) noexcept
{
    if (kind == CallbackStreamKind::InputInsert)
        return inputDirectRoutes;

    if (kind == CallbackStreamKind::Main)
        return mainDirectRoutes;

    return outputDirectRoutes;
}

const RealtimeEngine::DirectRouteBank& RealtimeEngine::directRouteBankFor(CallbackStreamKind kind) const noexcept
{
    if (kind == CallbackStreamKind::InputInsert)
        return inputDirectRoutes;

    if (kind == CallbackStreamKind::Main)
        return mainDirectRoutes;

    return outputDirectRoutes;
}

RealtimeEngine::DirectRouteBank& RealtimeEngine::pluginPassthroughBankFor(CallbackStreamKind kind) noexcept
{
    if (kind == CallbackStreamKind::InputInsert)
        return inputPluginPassthroughRoutes;

    if (kind == CallbackStreamKind::Main)
        return mainPluginPassthroughRoutes;

    return outputPluginPassthroughRoutes;
}

const RealtimeEngine::DirectRouteBank& RealtimeEngine::pluginPassthroughBankFor(CallbackStreamKind kind) const noexcept
{
    if (kind == CallbackStreamKind::InputInsert)
        return inputPluginPassthroughRoutes;

    if (kind == CallbackStreamKind::Main)
        return mainPluginPassthroughRoutes;

    return outputPluginPassthroughRoutes;
}

bool RealtimeEngine::isInputCallbackSuppressed(bool suppressInputCallbackChannels, int channel) const noexcept
{
    return suppressInputCallbackChannels &&
           channel >= 0 &&
           channel < MaxChannels &&
           inputCallbackSuppressed[static_cast<size_t>(channel)].load(std::memory_order_relaxed);
}

void RealtimeEngine::copyPassthrough(
    AudioBufferView buffer,
    int readOffset,
    bool suppressInputCallbackChannels) noexcept
{
    const int channels = std::max(0, std::min(buffer.outputChannels, buffer.inputChannels - readOffset));

    for (int ch = 0; ch < channels; ++ch)
    {
        const float* in = buffer.read[readOffset + ch];
        float* out = buffer.write[ch];

        if (out == nullptr)
            continue;

        if (isInputCallbackSuppressed(suppressInputCallbackChannels, ch))
        {
            for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                out[sample] = 0.0f;
            continue;
        }

        if (in == nullptr)
            continue;

        for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
            out[sample] = in[sample];
    }

    for (int ch = channels; ch < buffer.outputChannels; ++ch)
    {
        float* out = buffer.write[ch];
        if (out == nullptr)
            continue;

        for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
            out[sample] = 0.0f;
    }
}

void RealtimeEngine::applyConfiguredDelays(
    AudioBufferView buffer,
    CallbackStreamKind kind,
    int readOffset,
    int streamIndex,
    bool suppressInputCallbackChannels) noexcept
{
    const auto buffers = dynamicBuffers.load(std::memory_order_acquire);
    if (buffers == nullptr ||
        buffers->delayLength <= 1 ||
        buffers->sampleRate <= 0 ||
        buffers->delayBuffer.empty())
    {
        return;
    }

    const int channels = std::max(0, std::min(buffer.outputChannels, buffer.inputChannels - readOffset));
    const int safeChannels = std::min(channels, MaxChannels);
    const auto& delayBank = delayBankFor(kind);
    const auto& enabledBank = enableBankFor(kind);
    streamIndex = std::clamp(streamIndex, 0, DelayStreamCount - 1);
    const int length = buffers->delayLength;

    for (int ch = 0; ch < safeChannels; ++ch)
    {
        if (isInputCallbackSuppressed(suppressInputCallbackChannels, ch))
            continue;

        float* out = buffer.write[ch];
        if (out == nullptr)
            continue;

        const int delayMs = delayBank[ch].load(std::memory_order_relaxed);
        const bool enabled = enabledBank[ch].load(std::memory_order_relaxed);
        if (!enabled || delayMs <= 0)
            continue;

        const int lineIndex = buffers->delayLineIndexes[static_cast<size_t>(streamIndex)][static_cast<size_t>(ch)];
        if (lineIndex < 0 || lineIndex >= buffers->delayLineCount)
            continue;

        const int targetDelaySamples = std::min(
            length - 1,
            static_cast<int>((static_cast<int64_t>(delayMs) * buffer.sampleRate) / 1000));
        if (targetDelaySamples <= 0)
            continue;

        auto& delayInitialized = smoothedDelayInitialized[static_cast<size_t>(streamIndex)][static_cast<size_t>(ch)];
        auto& smoothedDelay = smoothedDelaySamples[static_cast<size_t>(streamIndex)][static_cast<size_t>(ch)];
        if (!delayInitialized)
        {
            smoothedDelay = targetDelaySamples;
            delayInitialized = true;
        }
        else
        {
            smoothedDelay = moveToward(
                smoothedDelay,
                targetDelaySamples,
                delaySmoothingStepSamples(buffer.sampleRate, buffer.samplesPerFrame));
        }
        const int delaySamples = std::clamp(smoothedDelay, 0, length - 1);

        const auto lineOffset = lineBufferOffset(lineIndex, length);
        if (lineOffset + static_cast<size_t>(length) > buffers->delayBuffer.size())
            continue;

        float* line = buffers->delayBuffer.data() + lineOffset;
        int write = delayWritePositions[static_cast<size_t>(streamIndex)][static_cast<size_t>(ch)];

        for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
        {
            const float dry = out[sample];
            line[write] = dry;

            int read = write - delaySamples;
            if (read < 0)
                read += length;

            out[sample] = line[read];

            ++write;
            if (write >= length)
                write = 0;
        }

        delayWritePositions[static_cast<size_t>(streamIndex)][static_cast<size_t>(ch)] = write;
    }
}

void RealtimeEngine::applyPlugins(
    AudioBufferView buffer,
    CallbackStreamKind kind,
    int readOffset,
    bool suppressInputCallbackChannels,
    std::array<bool, MaxChannels>& pluginOutputWritten) noexcept
{
    std::array<bool, MaxPluginSlots * MaxPluginPins> pluginBusWritten {};
    const auto scratchBuffers = dynamicPluginScratchBuffers.load(std::memory_order_acquire);

    const auto pluginBusPointer = [&scratchBuffers](int slot, int pin) noexcept -> float* {
        if (slot < 0 || slot >= MaxPluginSlots || pin < 0 || pin >= MaxPluginPins)
            return nullptr;

        if (scratchBuffers == nullptr ||
            scratchBuffers->pluginBusBuffer.empty())
            return nullptr;

        const auto index = pluginBusFlatIndex(slot, pin);
        if (index >= scratchBuffers->pluginBusLineIndexes.size())
            return nullptr;

        const int lineIndex = scratchBuffers->pluginBusLineIndexes[index];
        if (lineIndex < 0 || lineIndex >= scratchBuffers->pluginBusLineCount)
            return nullptr;

        const auto offset = lineBufferOffset(lineIndex, MaxPluginScratchSamples);
        if (offset + static_cast<size_t>(MaxPluginScratchSamples) > scratchBuffers->pluginBusBuffer.size())
            return nullptr;

        return scratchBuffers->pluginBusBuffer.data() + offset;
    };

    std::array<int, MaxPluginSlots> visitState {};
    const auto processSlot = [&](auto&& self, int slot) noexcept -> void {
        if (slot < 0 || slot >= MaxPluginSlots)
            return;

        if (visitState[static_cast<size_t>(slot)] == 2)
            return;

        if (visitState[static_cast<size_t>(slot)] == 1)
            return;

        visitState[static_cast<size_t>(slot)] = 1;
        auto finish = [&]() noexcept {
            visitState[static_cast<size_t>(slot)] = 2;
        };

        auto& pluginSlot = pluginSlots[static_cast<size_t>(slot)];

        if (!pluginSlot.enabled.load(std::memory_order_relaxed))
        {
            finish();
            return;
        }

        if (pluginSlot.kind.load(std::memory_order_relaxed) != static_cast<int>(kind))
        {
            finish();
            return;
        }

        auto* processor = pluginSlot.processor.load(std::memory_order_acquire);
        if (processor == nullptr)
        {
            finish();
            return;
        }

        const int inputRouteCount = std::clamp(pluginSlot.inputRouteCount.load(std::memory_order_acquire), 0, MaxPluginRoutes);
        const int outputRouteCount = std::clamp(pluginSlot.outputRouteCount.load(std::memory_order_acquire), 0, MaxPluginRoutes);
        if (inputRouteCount == 0 || buffer.samplesPerFrame > MaxPluginScratchSamples)
        {
            finish();
            return;
        }

        for (int route = 0; route < inputRouteCount; ++route)
        {
            const int sourceKind = pluginSlot.inputSourceKinds[static_cast<size_t>(route)].load(std::memory_order_relaxed);
            if (sourceKind != static_cast<int>(PluginRouteEndpointKind::PluginPin))
                continue;

            const int sourceSlot = pluginSlot.inputSourceSlots[static_cast<size_t>(route)].load(std::memory_order_relaxed);
            if (sourceSlot >= 0 && sourceSlot < MaxPluginSlots && sourceSlot != slot)
                self(self, sourceSlot);
        }

        std::array<PluginAudioInputRoute, MaxPluginRoutes> inputRoutes {};
        std::array<PluginAudioOutputRoute, MaxPluginRoutes> outputRoutes {};
        int validInputRoutes = 0;
        int validOutputRoutes = 0;

        for (int route = 0; route < inputRouteCount; ++route)
        {
            const int sourceKind = pluginSlot.inputSourceKinds[static_cast<size_t>(route)].load(std::memory_order_relaxed);
            const int source = pluginSlot.inputSourceChannels[static_cast<size_t>(route)].load(std::memory_order_relaxed);
            const int sourceSlot = pluginSlot.inputSourceSlots[static_cast<size_t>(route)].load(std::memory_order_relaxed);
            const int sourcePin = pluginSlot.inputSourcePins[static_cast<size_t>(route)].load(std::memory_order_relaxed);
            const int pin = pluginSlot.inputPluginPins[static_cast<size_t>(route)].load(std::memory_order_relaxed);

            const float* sourcePointer = nullptr;

            if (sourceKind == static_cast<int>(PluginRouteEndpointKind::VoiceMeeterChannel))
            {
                if (source >= 0 &&
                    source < MaxChannels &&
                    !isInputCallbackSuppressed(suppressInputCallbackChannels, source))
                {
                    sourcePointer = kind == CallbackStreamKind::Main
                        ? sourceChannelPointer(buffer, readOffset, source)
                        : currentChannelPointer(buffer, readOffset, source);
                }
            }
            else if (sourceKind == static_cast<int>(PluginRouteEndpointKind::PluginPin) &&
                     sourceSlot >= 0 &&
                     sourceSlot < MaxPluginSlots &&
                     sourcePin >= 0 &&
                     sourcePin < MaxPluginPins)
            {
                const auto busIndex = pluginBusFlatIndex(sourceSlot, sourcePin);
                if (busIndex < pluginBusWritten.size() && pluginBusWritten[busIndex])
                    sourcePointer = pluginBusPointer(sourceSlot, sourcePin);
            }

            if (sourcePointer != nullptr && pin >= 0)
                inputRoutes[static_cast<size_t>(validInputRoutes++)] = PluginAudioInputRoute { sourcePointer, pin };
        }

        for (int route = 0; route < outputRouteCount; ++route)
        {
            const int destinationKind = pluginSlot.outputDestinationKinds[static_cast<size_t>(route)].load(std::memory_order_relaxed);
            const int pin = pluginSlot.outputPluginPins[static_cast<size_t>(route)].load(std::memory_order_relaxed);
            const int destination = pluginSlot.outputDestinationChannels[static_cast<size_t>(route)].load(std::memory_order_relaxed);
            const int destinationSlot = pluginSlot.outputDestinationSlots[static_cast<size_t>(route)].load(std::memory_order_relaxed);
            const int destinationPin = pluginSlot.outputDestinationPins[static_cast<size_t>(route)].load(std::memory_order_relaxed);

            float* destinationPointer = nullptr;
            int resolvedDestinationChannel = -1;
            int resolvedDestinationSlot = -1;
            int resolvedDestinationPin = -1;

            if (destinationKind == static_cast<int>(PluginRouteEndpointKind::VoiceMeeterChannel))
            {
                if (destination >= 0 &&
                    destination < buffer.outputChannels &&
                    destination < MaxChannels &&
                    !isInputCallbackSuppressed(suppressInputCallbackChannels, destination))
                {
                    destinationPointer = buffer.write[destination];
                    resolvedDestinationChannel = destination;
                }
            }
            else if (destinationKind == static_cast<int>(PluginRouteEndpointKind::PluginPin) &&
                     destinationSlot >= 0 &&
                     destinationSlot < MaxPluginSlots &&
                     destinationSlot != slot &&
                     destinationPin >= 0 &&
                     destinationPin < MaxPluginPins)
            {
                destinationPointer = pluginBusPointer(slot, pin);
                resolvedDestinationSlot = slot;
                resolvedDestinationPin = pin;
            }

            if (destinationPointer != nullptr && pin >= 0)
            {
                outputRoutes[static_cast<size_t>(validOutputRoutes++)] = PluginAudioOutputRoute {
                    destinationPointer,
                    pin,
                    resolvedDestinationChannel,
                    resolvedDestinationSlot,
                    resolvedDestinationPin
                };
            }
        }

        if (validInputRoutes > 0)
        {
            const bool bypassed = pluginSlot.bypassed.load(std::memory_order_relaxed);
            bool rendered = false;

            if (bypassed)
            {
                std::array<float*, MaxPluginRoutes> clearedDestinations {};
                int clearedCount = 0;

                for (int outputIndex = 0; outputIndex < validOutputRoutes; ++outputIndex)
                {
                    auto& outputRoute = outputRoutes[static_cast<size_t>(outputIndex)];
                    auto* destination = outputRoute.destination;
                    if (destination == nullptr)
                        continue;

                    const PluginAudioInputRoute* passthroughInput = nullptr;
                    for (int inputIndex = 0; inputIndex < validInputRoutes; ++inputIndex)
                    {
                        const auto& inputRoute = inputRoutes[static_cast<size_t>(inputIndex)];
                        if (inputRoute.pluginPin == outputRoute.pluginPin && inputRoute.source != nullptr)
                        {
                            passthroughInput = &inputRoute;
                            break;
                        }
                    }

                    if (passthroughInput == nullptr)
                        continue;

                    if (passthroughInput->source == destination)
                    {
                        rendered = true;
                        continue;
                    }

                    bool alreadyCleared = false;
                    for (int i = 0; i < clearedCount; ++i)
                        alreadyCleared = alreadyCleared || clearedDestinations[static_cast<size_t>(i)] == destination;

                    if (!alreadyCleared)
                    {
                        for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                            destination[sample] = 0.0f;

                        if (clearedCount < static_cast<int>(clearedDestinations.size()))
                            clearedDestinations[static_cast<size_t>(clearedCount++)] = destination;
                    }

                    for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                        destination[sample] += passthroughInput->source[sample];

                    rendered = true;
                }
            }
            else
            {
                rendered = processor->process(
                    buffer,
                    PluginRoutingView {
                        inputRoutes.data(),
                        validInputRoutes,
                        outputRoutes.data(),
                        validOutputRoutes
                    });
            }

            if (rendered)
            {
                for (int route = 0; route < validOutputRoutes; ++route)
                {
                    const auto& outputRoute = outputRoutes[static_cast<size_t>(route)];
                    const int destination = outputRoute.destinationChannel;
                    if (destination >= 0 && destination < MaxChannels)
                        pluginOutputWritten[static_cast<size_t>(destination)] = true;

                    if (outputRoute.destinationSlot >= 0 &&
                        outputRoute.destinationSlot < MaxPluginSlots &&
                        outputRoute.destinationPin >= 0 &&
                        outputRoute.destinationPin < MaxPluginPins)
                    {
                        const auto busIndex = pluginBusFlatIndex(outputRoute.destinationSlot, outputRoute.destinationPin);
                        if (busIndex < pluginBusWritten.size())
                            pluginBusWritten[busIndex] = true;
                    }
                }
            }
        }

        finish();
    };

    for (int slot = 0; slot < MaxPluginSlots; ++slot)
        processSlot(processSlot, slot);
}

void RealtimeEngine::applyDirectRoutes(
    AudioBufferView buffer,
    CallbackStreamKind kind,
    int readOffset,
    bool enableInputOutputRoutes,
    bool suppressInputCallbackChannels,
    std::array<bool, MaxChannels>& pluginOutputWritten) noexcept
{
    if (kind == CallbackStreamKind::InputInsert)
    {
        if (enableInputOutputRoutes)
            captureInputRoutes(buffer, readOffset, suppressInputCallbackChannels, pluginOutputWritten);
        return;
    }

    if (kind == CallbackStreamKind::OutputInsert)
    {
        if (enableInputOutputRoutes)
            mixCapturedInputRoutes(buffer, pluginOutputWritten);
        applySameBufferDirectRoutes(buffer, kind, readOffset, suppressInputCallbackChannels, pluginOutputWritten);
        return;
    }

    applySameBufferDirectRoutes(buffer, kind, readOffset, suppressInputCallbackChannels, pluginOutputWritten);
}

void RealtimeEngine::applyPluginPassthroughRoutes(
    AudioBufferView buffer,
    CallbackStreamKind kind,
    int readOffset,
    int streamIndex,
    bool suppressInputCallbackChannels,
    std::array<bool, MaxChannels>& pluginOutputWritten) noexcept
{
    const auto& routeBank = pluginPassthroughBankFor(kind);
    const int routeCount = std::clamp(routeBank.routeCount.load(std::memory_order_acquire), 0, MaxDirectRoutes);
    streamIndex = std::clamp(streamIndex, 0, DelayStreamCount - 1);
    const auto scratchBuffers = dynamicPluginScratchBuffers.load(std::memory_order_acquire);
    std::array<bool, MaxChannels> gatedPairs {};
    const bool gateSameBufferRoutes = kind != CallbackStreamKind::Main;
    const int passthroughCapacity = scratchBuffers != nullptr
        ? scratchBuffers->passthroughRouteCapacities[static_cast<size_t>(streamIndex)]
        : 0;
    const int passthroughStartLine = scratchBuffers != nullptr
        ? scratchBuffers->passthroughRouteStartLines[static_cast<size_t>(streamIndex)]
        : -1;
    const bool canSnapshotRoutes =
        gateSameBufferRoutes &&
        buffer.samplesPerFrame >= 0 &&
        buffer.samplesPerFrame <= MaxPluginScratchSamples &&
        scratchBuffers != nullptr &&
        passthroughStartLine >= 0 &&
        passthroughCapacity >= routeCount &&
        !scratchBuffers->pluginPassthroughScratchBuffer.empty();

    std::array<int, MaxDirectRoutes> routeDestinations {};
    std::array<const float*, MaxDirectRoutes> routeInputs {};
    int validRouteCount = 0;

    for (int route = 0; route < routeCount; ++route)
    {
        const int source = routeBank.sourceChannels[static_cast<size_t>(route)].load(std::memory_order_relaxed);
        const int destination = routeBank.destinationChannels[static_cast<size_t>(route)].load(std::memory_order_relaxed);

        if (source < 0 || source >= MaxChannels)
            continue;

        if (destination < 0)
        {
            if (isInputCallbackSuppressed(suppressInputCallbackChannels, source))
                continue;

            if (gateSameBufferRoutes)
            {
                markStereoPair(gatedPairs, source);
            }
            else
            {
                const int pairBase = source - (source % 2);
                const int channels = std::max(0, std::min(buffer.outputChannels, MaxChannels));
                for (int channel = pairBase; channel < pairBase + 2 && channel < channels; ++channel)
                {
                    if (channel < 0 || pluginOutputWritten[static_cast<size_t>(channel)])
                        continue;

                    float* output = buffer.write[channel];
                    if (output == nullptr)
                        continue;

                    for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                        output[sample] = 0.0f;

                    pluginOutputWritten[static_cast<size_t>(channel)] = true;
                }
            }

            continue;
        }

        if (destination >= buffer.outputChannels || destination >= MaxChannels)
            continue;

        if (isInputCallbackSuppressed(suppressInputCallbackChannels, source) ||
            isInputCallbackSuppressed(suppressInputCallbackChannels, destination))
        {
            continue;
        }

        const float* input = sourceChannelPointer(buffer, readOffset, source);
        float* output = buffer.write[destination];
        if (input == nullptr || output == nullptr)
            continue;

        if (gateSameBufferRoutes)
        {
            markStereoPair(gatedPairs, source);
            markStereoPair(gatedPairs, destination);
        }

        routeDestinations[static_cast<size_t>(validRouteCount)] = destination;

        if (canSnapshotRoutes)
        {
            const int lineIndex = passthroughStartLine + validRouteCount;
            const auto snapshotOffset = lineBufferOffset(lineIndex, MaxPluginScratchSamples);
            float* snapshot = nullptr;
            if (snapshotOffset + static_cast<size_t>(MaxPluginScratchSamples) <=
                scratchBuffers->pluginPassthroughScratchBuffer.size())
            {
                snapshot = scratchBuffers->pluginPassthroughScratchBuffer.data() + snapshotOffset;
            }

            if (snapshot == nullptr)
            {
                routeInputs[static_cast<size_t>(validRouteCount)] = input;
                ++validRouteCount;
                continue;
            }

            for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                snapshot[sample] = input[sample];

            routeInputs[static_cast<size_t>(validRouteCount)] = snapshot;
        }
        else
        {
            routeInputs[static_cast<size_t>(validRouteCount)] = input;
        }

        ++validRouteCount;
    }

    for (int route = 0; route < validRouteCount; ++route)
    {
        const int destination = routeDestinations[static_cast<size_t>(route)];
        const float* input = routeInputs[static_cast<size_t>(route)];
        float* output = buffer.write[destination];
        if (input == nullptr || output == nullptr)
            continue;

        if (!gateSameBufferRoutes && input == output)
        {
            pluginOutputWritten[static_cast<size_t>(destination)] = true;
            continue;
        }

        if (!pluginOutputWritten[static_cast<size_t>(destination)])
        {
            for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                output[sample] = 0.0f;
        }

        for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
            output[sample] += input[sample];

        pluginOutputWritten[static_cast<size_t>(destination)] = true;
    }

    if (!gateSameBufferRoutes)
        return;

    const int channels = std::max(0, std::min(buffer.outputChannels, MaxChannels));
    for (int channel = 0; channel < channels; ++channel)
    {
        if (!gatedPairs[static_cast<size_t>(channel)] ||
            pluginOutputWritten[static_cast<size_t>(channel)])
        {
            continue;
        }

        float* output = buffer.write[channel];
        if (output == nullptr)
            continue;

        for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
            output[sample] = 0.0f;

        pluginOutputWritten[static_cast<size_t>(channel)] = true;
    }
}

void RealtimeEngine::processSinglePingInput(AudioBufferView buffer) noexcept
{
    if (singlePingStatus.load(std::memory_order_acquire) != PingStatusArmed)
        return;

    const int channel = singlePingInputChannel.load(std::memory_order_relaxed);
    if (channel < 0 || channel >= buffer.outputChannels || channel >= MaxChannels)
    {
        singlePingStatus.store(PingStatusTimeout, std::memory_order_release);
        return;
    }

    float* output = buffer.write[channel];
    if (output == nullptr)
    {
        singlePingStatus.store(PingStatusTimeout, std::memory_order_release);
        return;
    }

    const int currentSampleRate = buffer.sampleRate > 0
        ? buffer.sampleRate
        : std::max(8000, sampleRate.load(std::memory_order_acquire));
    const int timeoutMilliseconds = singlePingTimeoutMilliseconds.load(std::memory_order_relaxed);
    singlePingSampleRate.store(currentSampleRate, std::memory_order_relaxed);
    singlePingTimeoutSamples.store(
        static_cast<int>((static_cast<int64_t>(timeoutMilliseconds) * currentSampleRate) / 1000),
        std::memory_order_relaxed);
    singlePingElapsedSamples.store(0, std::memory_order_relaxed);
    singlePingLatencySamples.store(-1, std::memory_order_relaxed);
    singlePingPeakPercent.store(0, std::memory_order_relaxed);

    const int pulseSamples = std::min(PingPulseSamples, std::max(0, buffer.samplesPerFrame));
    for (int sample = 0; sample < pulseSamples; ++sample)
    {
        const float sign = (sample % 2) == 0 ? 1.0f : -1.0f;
        output[sample] += sign * PingPulseAmplitude;
    }

    singlePingPulsePosition.store(pulseSamples, std::memory_order_relaxed);
    singlePingStatus.store(PingStatusWaiting, std::memory_order_release);
}

void RealtimeEngine::processSinglePingOutput(AudioBufferView buffer) noexcept
{
    if (singlePingStatus.load(std::memory_order_acquire) != PingStatusWaiting)
        return;

    const int channel = singlePingOutputChannel.load(std::memory_order_relaxed);
    if (channel < 0 || channel >= buffer.outputChannels || channel >= MaxChannels)
    {
        singlePingStatus.store(PingStatusTimeout, std::memory_order_release);
        return;
    }

    const float* input = buffer.write[channel];
    if (input == nullptr)
    {
        singlePingStatus.store(PingStatusTimeout, std::memory_order_release);
        return;
    }

    int elapsed = singlePingElapsedSamples.load(std::memory_order_relaxed);
    for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
    {
        const float absolute = input[sample] < 0.0f ? -input[sample] : input[sample];
        if (absolute < PingDetectThreshold)
            continue;

        singlePingLatencySamples.store(elapsed + sample, std::memory_order_relaxed);
        singlePingPeakPercent.store(
            std::clamp(static_cast<int>(absolute * 100.0f), 0, 1000),
            std::memory_order_relaxed);
        singlePingStatus.store(PingStatusDetected, std::memory_order_release);
        return;
    }

    elapsed += std::max(0, buffer.samplesPerFrame);
    singlePingElapsedSamples.store(elapsed, std::memory_order_relaxed);
    if (elapsed >= singlePingTimeoutSamples.load(std::memory_order_relaxed))
        singlePingStatus.store(PingStatusTimeout, std::memory_order_release);
}

void RealtimeEngine::captureInputRoutes(
    AudioBufferView buffer,
    int readOffset,
    bool suppressInputCallbackChannels,
    std::array<bool, MaxChannels>& pluginOutputWritten) noexcept
{
    const auto buffers = dynamicBuffers.load(std::memory_order_acquire);
    if (buffers == nullptr ||
        buffers->routeLength <= 0 ||
        buffers->routeBuffer.empty())
    {
        return;
    }

    const auto& routeBank = inputDirectRoutes;
    const int routeCount = std::clamp(routeBank.routeCount.load(std::memory_order_acquire), 0, MaxDirectRoutes);
    if (routeCount <= 0)
        return;

    const int length = buffers->routeLength;
    std::array<bool, MaxChannels> muteSources {};

    for (int route = 0; route < routeCount; ++route)
    {
        const int source = routeBank.sourceChannels[static_cast<size_t>(route)].load(std::memory_order_relaxed);
        if (source < 0 || source >= MaxChannels)
            continue;

        if (isInputCallbackSuppressed(suppressInputCallbackChannels, source))
            continue;

        const float* input = currentChannelPointer(buffer, readOffset, source);
        if (input == nullptr)
            continue;

        const int lineIndex = buffers->routeLineIndexes[0][static_cast<size_t>(route)];
        if (lineIndex < 0 || lineIndex >= buffers->routeLineCount)
            continue;

        const int gainPercent = routeBank.gainPercent[static_cast<size_t>(route)].load(std::memory_order_relaxed);
        auto& currentGain = smoothedRouteGains[0][static_cast<size_t>(route)];
        const float targetGain = gainFromPercent(gainPercent);
        const float gainStep = buffer.samplesPerFrame > 0
            ? (targetGain - currentGain) / static_cast<float>(buffer.samplesPerFrame)
            : 0.0f;
        float gain = currentGain;
        const auto lineOffset = lineBufferOffset(lineIndex, length);
        if (lineOffset + static_cast<size_t>(length) > buffers->routeBuffer.size())
            continue;

        float* fifo = buffers->routeBuffer.data() + lineOffset;

        int write = routeWritePositions[0][static_cast<size_t>(route)].load(std::memory_order_relaxed);
        const int previouslyPrimed = routePrimedSamples[0][static_cast<size_t>(route)].load(std::memory_order_relaxed);

        for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
        {
            gain += gainStep;
            fifo[write] = input[sample] * gain;
            ++write;
            if (write >= length)
                write = 0;
        }

        currentGain = targetGain;
        routeWritePositions[0][static_cast<size_t>(route)].store(write, std::memory_order_release);
        routePrimedSamples[0][static_cast<size_t>(route)].store(
            std::min(length, previouslyPrimed + std::max(0, buffer.samplesPerFrame)),
            std::memory_order_release);

        if (routeBank.muteSource[static_cast<size_t>(route)].load(std::memory_order_relaxed))
            muteSources[static_cast<size_t>(source)] = true;
    }

    const int channels = std::min(buffer.outputChannels, MaxChannels);
    for (int source = 0; source < channels; ++source)
    {
        if (!muteSources[static_cast<size_t>(source)])
            continue;

        float* output = buffer.write[source];
        if (output == nullptr)
            continue;

        for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
            output[sample] = 0.0f;

        pluginOutputWritten[static_cast<size_t>(source)] = true;
    }
}

void RealtimeEngine::mixCapturedInputRoutes(
    AudioBufferView buffer,
    std::array<bool, MaxChannels>& pluginOutputWritten) noexcept
{
    const auto buffers = dynamicBuffers.load(std::memory_order_acquire);
    if (buffers == nullptr ||
        buffers->routeLength <= 0 ||
        buffers->routeBuffer.empty())
    {
        return;
    }

    const auto& routeBank = inputDirectRoutes;
    const int routeCount = std::clamp(routeBank.routeCount.load(std::memory_order_acquire), 0, MaxDirectRoutes);
    if (routeCount <= 0)
        return;

    const int length = buffers->routeLength;
    for (int route = 0; route < routeCount; ++route)
    {
        const int destination = routeBank.destinationChannels[static_cast<size_t>(route)].load(std::memory_order_relaxed);
        if (destination < 0 || destination >= buffer.outputChannels || destination >= MaxChannels)
            continue;

        float* output = buffer.write[destination];
        if (output == nullptr)
            continue;

        const int lineIndex = buffers->routeLineIndexes[0][static_cast<size_t>(route)];
        if (lineIndex < 0 || lineIndex >= buffers->routeLineCount)
            continue;

        const int delayMs = routeBank.delayMilliseconds[static_cast<size_t>(route)].load(std::memory_order_relaxed);
        const int delaySamples = std::clamp(
            buffer.sampleRate > 0 ? static_cast<int>((static_cast<int64_t>(delayMs) * buffer.sampleRate) / 1000) : 0,
            0,
            length - 1);
        const int primedSamples = routePrimedSamples[0][static_cast<size_t>(route)].load(std::memory_order_acquire);
        if (primedSamples < delaySamples + buffer.samplesPerFrame)
            continue;

        const auto lineOffset = lineBufferOffset(lineIndex, length);
        if (lineOffset + static_cast<size_t>(length) > buffers->routeBuffer.size())
            continue;

        float* fifo = buffers->routeBuffer.data() + lineOffset;
        const int write = routeWritePositions[0][static_cast<size_t>(route)].load(std::memory_order_acquire);
        int read = write - delaySamples - buffer.samplesPerFrame;
        while (read < 0)
            read += length;
        while (read >= length)
            read -= length;

        for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
        {
            output[sample] += fifo[read];
            ++read;
            if (read >= length)
                read = 0;
        }

        pluginOutputWritten[static_cast<size_t>(destination)] = true;
    }
}

void RealtimeEngine::applySameBufferDirectRoutes(
    AudioBufferView buffer,
    CallbackStreamKind kind,
    int readOffset,
    bool suppressInputCallbackChannels,
    std::array<bool, MaxChannels>& pluginOutputWritten) noexcept
{
    const auto& routeBank = directRouteBankFor(kind);
    const int routeCount = std::clamp(routeBank.routeCount.load(std::memory_order_acquire), 0, MaxDirectRoutes);
    const int streamIndex = delayStreamIndex(kind);
    const auto buffers = dynamicBuffers.load(std::memory_order_acquire);

    for (int route = 0; route < routeCount; ++route)
    {
        const int source = routeBank.sourceChannels[static_cast<size_t>(route)].load(std::memory_order_relaxed);
        const int destination = routeBank.destinationChannels[static_cast<size_t>(route)].load(std::memory_order_relaxed);

        if (source < 0 || source >= buffer.outputChannels || source >= MaxChannels)
            continue;

        if (destination < 0)
            continue;

        if (destination >= buffer.outputChannels || destination >= MaxChannels)
            continue;

        if (isInputCallbackSuppressed(suppressInputCallbackChannels, source) ||
            isInputCallbackSuppressed(suppressInputCallbackChannels, destination))
        {
            continue;
        }

        const float* input = kind == CallbackStreamKind::Main
            ? sourceChannelPointer(buffer, readOffset, source)
            : currentChannelPointer(buffer, readOffset, source);
        float* output = buffer.write[destination];
        if (input == nullptr || output == nullptr)
            continue;

        const int gainPercent = routeBank.gainPercent[static_cast<size_t>(route)].load(std::memory_order_relaxed);
        auto& currentGain = smoothedRouteGains[static_cast<size_t>(streamIndex)][static_cast<size_t>(route)];
        const float targetGain = gainFromPercent(gainPercent);
        const float gainStep = buffer.samplesPerFrame > 0
            ? (targetGain - currentGain) / static_cast<float>(buffer.samplesPerFrame)
            : 0.0f;
        float gain = currentGain;

        const int delayMs = routeBank.delayMilliseconds[static_cast<size_t>(route)].load(std::memory_order_relaxed);
        const bool delayRequested = delayMs > 0;
        int length = 0;
        int delaySamples = 0;
        int lineIndex = -1;
        if (delayRequested && buffers != nullptr)
        {
            length = buffers->routeLength;
            lineIndex = buffers->routeLineIndexes[static_cast<size_t>(streamIndex)][static_cast<size_t>(route)];
            delaySamples = std::clamp(
                buffer.sampleRate > 0 ? static_cast<int>((static_cast<int64_t>(delayMs) * buffer.sampleRate) / 1000) : 0,
                0,
                std::max(0, length - 1));
        }

        const bool useDelay =
            delaySamples > 0 &&
            length > 1 &&
            buffers != nullptr &&
            lineIndex >= 0 &&
            lineIndex < buffers->routeLineCount &&
            !buffers->routeBuffer.empty();

        if (delayRequested && !useDelay)
        {
            currentGain = targetGain;
            if (kind != CallbackStreamKind::Main)
            {
                for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                    output[sample] = 0.0f;

                if (routeBank.muteSource[static_cast<size_t>(route)].load(std::memory_order_relaxed) &&
                    source < buffer.outputChannels)
                {
                    float* sourceOutput = buffer.write[source];
                    if (sourceOutput != nullptr)
                    {
                        for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                            sourceOutput[sample] = 0.0f;
                    }
                }
            }

            pluginOutputWritten[static_cast<size_t>(destination)] = true;
            continue;
        }

        if (useDelay)
        {
            const auto lineOffset = lineBufferOffset(lineIndex, length);
            if (lineOffset + static_cast<size_t>(length) > buffers->routeBuffer.size())
                continue;

            float* fifo = buffers->routeBuffer.data() + lineOffset;
            int write = routeWritePositions[static_cast<size_t>(streamIndex)][static_cast<size_t>(route)].load(std::memory_order_relaxed);
            const int previouslyPrimed = routePrimedSamples[static_cast<size_t>(streamIndex)][static_cast<size_t>(route)].load(std::memory_order_relaxed);
            for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
            {
                gain += gainStep;
                fifo[write] = input[sample] * gain;
                ++write;
                if (write >= length)
                    write = 0;
            }

            routeWritePositions[static_cast<size_t>(streamIndex)][static_cast<size_t>(route)].store(write, std::memory_order_release);
            const int primedSamples = std::min(length, previouslyPrimed + std::max(0, buffer.samplesPerFrame));
            routePrimedSamples[static_cast<size_t>(streamIndex)][static_cast<size_t>(route)].store(
                primedSamples,
                std::memory_order_release);

            if (primedSamples < delaySamples + buffer.samplesPerFrame)
            {
                currentGain = targetGain;
                if (kind != CallbackStreamKind::Main)
                {
                    for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                        output[sample] = 0.0f;

                    if (routeBank.muteSource[static_cast<size_t>(route)].load(std::memory_order_relaxed) &&
                        source < buffer.outputChannels)
                    {
                        float* sourceOutput = buffer.write[source];
                        if (sourceOutput != nullptr)
                        {
                            for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                                sourceOutput[sample] = 0.0f;
                        }
                    }
                }

                pluginOutputWritten[static_cast<size_t>(destination)] = true;
                continue;
            }

            int read = write - delaySamples - buffer.samplesPerFrame;
            while (read < 0)
                read += length;
            while (read >= length)
                read -= length;

            if (kind == CallbackStreamKind::Main)
            {
                for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                {
                    output[sample] += fifo[read];
                    ++read;
                    if (read >= length)
                        read = 0;
                }
            }
            else
            {
                for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                {
                    output[sample] = fifo[read];
                    ++read;
                    if (read >= length)
                        read = 0;
                }
            }

            currentGain = targetGain;
        }
        else if (input != output)
        {
            if (kind == CallbackStreamKind::Main)
            {
                for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                {
                    gain += gainStep;
                    output[sample] += input[sample] * gain;
                }
            }
            else
            {
                for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                {
                    gain += gainStep;
                    output[sample] = input[sample] * gain;
                }
            }

            currentGain = targetGain;
        }

        if (kind != CallbackStreamKind::Main &&
            routeBank.muteSource[static_cast<size_t>(route)].load(std::memory_order_relaxed) &&
            source < buffer.outputChannels)
        {
            float* sourceOutput = buffer.write[source];
            if (sourceOutput != nullptr)
            {
                for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                    sourceOutput[sample] = 0.0f;
            }
        }

        pluginOutputWritten[static_cast<size_t>(destination)] = true;
    }
}

void RealtimeEngine::applyPluginGraphGate(
    AudioBufferView buffer,
    CallbackStreamKind kind,
    bool suppressInputCallbackChannels,
    const std::array<bool, MaxChannels>& pluginOutputWritten) noexcept
{
    const int channels = std::max(0, std::min(buffer.outputChannels, MaxChannels));
    const auto& graphBank = pluginGraphBankFor(kind);

    for (int ch = 0; ch < channels; ++ch)
    {
        if (isInputCallbackSuppressed(suppressInputCallbackChannels, ch))
            continue;

        if (!graphBank[static_cast<size_t>(ch)].load(std::memory_order_relaxed))
            continue;

        if (pluginOutputWritten[static_cast<size_t>(ch)])
            continue;

        float* out = buffer.write[ch];
        if (out == nullptr)
            continue;

        for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
            out[sample] = 0.0f;
    }
}

void RealtimeEngine::applyConfiguredGains(
    AudioBufferView buffer,
    CallbackStreamKind kind,
    int streamIndex,
    bool suppressInputCallbackChannels) noexcept
{
    const int channels = std::max(0, std::min(buffer.outputChannels, buffer.inputChannels - getReadOffset(buffer, kind)));
    const int safeChannels = std::min(channels, MaxChannels);
    const auto& gainBank = gainBankFor(kind);
    const auto& enabledBank = enableBankFor(kind);
    streamIndex = std::clamp(streamIndex, 0, DelayStreamCount - 1);

    for (int ch = 0; ch < safeChannels; ++ch)
    {
        if (isInputCallbackSuppressed(suppressInputCallbackChannels, ch))
            continue;

        if (!enabledBank[ch].load(std::memory_order_relaxed))
        {
            smoothedChannelGains[static_cast<size_t>(streamIndex)][static_cast<size_t>(ch)] = 1.0f;
            continue;
        }

        const int gainPercent = gainBank[ch].load(std::memory_order_relaxed);
        auto& currentGain = smoothedChannelGains[static_cast<size_t>(streamIndex)][static_cast<size_t>(ch)];
        const float targetGain = gainFromPercent(gainPercent);
        if (gainPercent == 100 && currentGain == 1.0f)
            continue;

        float* out = buffer.write[ch];
        if (out == nullptr)
            continue;

        const float gainStep = buffer.samplesPerFrame > 0
            ? (targetGain - currentGain) / static_cast<float>(buffer.samplesPerFrame)
            : 0.0f;
        float gain = currentGain;
        for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
        {
            gain += gainStep;
            out[sample] *= gain;
        }

        currentGain = targetGain;
    }
}

void RealtimeEngine::captureProbeRead(AudioBufferView buffer, CallbackStreamKind kind, int readOffset) noexcept
{
    const int inputChannel = probeInputChannel.load(std::memory_order_relaxed);
    const int outputChannel = probeOutputChannel.load(std::memory_order_relaxed);

    if (kind == CallbackStreamKind::InputInsert)
    {
        inputInsertInputChannels.store(buffer.inputChannels, std::memory_order_relaxed);
        inputInsertOutputChannels.store(buffer.outputChannels, std::memory_order_relaxed);
        inputInsertReadPeakPercent.store(
            peakPercent(readChannelPointer(buffer, inputChannel), buffer.samplesPerFrame),
            std::memory_order_relaxed);
        const auto inputPeak = maxReadPeak(buffer, 0, buffer.inputChannels, 0);
        inputInsertMaxReadPeakPercent.store(inputPeak.percent, std::memory_order_relaxed);
        inputInsertMaxReadChannel.store(inputPeak.channel, std::memory_order_relaxed);
        return;
    }

    if (kind == CallbackStreamKind::Main)
    {
        mainInputChannels.store(buffer.inputChannels, std::memory_order_relaxed);
        mainOutputChannels.store(buffer.outputChannels, std::memory_order_relaxed);
        mainInputReadPeakPercent.store(
            peakPercent(readChannelPointer(buffer, inputChannel), buffer.samplesPerFrame),
            std::memory_order_relaxed);
        mainOutputReadPeakPercent.store(
            peakPercent(readChannelPointer(buffer, readOffset + outputChannel), buffer.samplesPerFrame),
            std::memory_order_relaxed);
        const int sourceChannels = std::clamp(readOffset, 0, buffer.inputChannels);
        const int busChannels = std::clamp(buffer.inputChannels - readOffset, 0, buffer.outputChannels);
        const auto sourcePeak = maxReadPeak(buffer, 0, sourceChannels, 0);
        const auto busPeak = maxReadPeak(buffer, readOffset, busChannels, readOffset);
        mainSourceMaxReadPeakPercent.store(sourcePeak.percent, std::memory_order_relaxed);
        mainSourceMaxReadChannel.store(sourcePeak.channel, std::memory_order_relaxed);
        mainBusMaxReadPeakPercent.store(busPeak.percent, std::memory_order_relaxed);
        mainBusMaxReadChannel.store(busPeak.channel, std::memory_order_relaxed);
        return;
    }

    outputInsertInputChannels.store(buffer.inputChannels, std::memory_order_relaxed);
    outputInsertOutputChannels.store(buffer.outputChannels, std::memory_order_relaxed);
    outputInsertReadPeakPercent.store(
        peakPercent(readChannelPointer(buffer, outputChannel), buffer.samplesPerFrame),
        std::memory_order_relaxed);
    const auto outputPeak = maxReadPeak(buffer, 0, buffer.inputChannels, 0);
    outputInsertMaxReadPeakPercent.store(outputPeak.percent, std::memory_order_relaxed);
    outputInsertMaxReadChannel.store(outputPeak.channel, std::memory_order_relaxed);
}

void RealtimeEngine::captureProbeWrite(AudioBufferView buffer, CallbackStreamKind kind, int) noexcept
{
    const int inputChannel = probeInputChannel.load(std::memory_order_relaxed);
    const int outputChannel = probeOutputChannel.load(std::memory_order_relaxed);

    if (kind == CallbackStreamKind::InputInsert)
    {
        inputInsertWritePeakPercent.store(
            peakPercent(writeChannelPointer(buffer, inputChannel), buffer.samplesPerFrame),
            std::memory_order_relaxed);
        const auto inputPeak = maxWritePeak(buffer, buffer.outputChannels);
        inputInsertMaxWritePeakPercent.store(inputPeak.percent, std::memory_order_relaxed);
        inputInsertMaxWriteChannel.store(inputPeak.channel, std::memory_order_relaxed);
        return;
    }

    if (kind == CallbackStreamKind::Main)
    {
        mainWritePeakPercent.store(
            peakPercent(writeChannelPointer(buffer, outputChannel), buffer.samplesPerFrame),
            std::memory_order_relaxed);
        const auto mainPeak = maxWritePeak(buffer, buffer.outputChannels);
        mainMaxWritePeakPercent.store(mainPeak.percent, std::memory_order_relaxed);
        mainMaxWriteChannel.store(mainPeak.channel, std::memory_order_relaxed);
        return;
    }

    outputInsertWritePeakPercent.store(
        peakPercent(writeChannelPointer(buffer, outputChannel), buffer.samplesPerFrame),
        std::memory_order_relaxed);
    const auto outputPeak = maxWritePeak(buffer, buffer.outputChannels);
    outputInsertMaxWritePeakPercent.store(outputPeak.percent, std::memory_order_relaxed);
    outputInsertMaxWriteChannel.store(outputPeak.channel, std::memory_order_relaxed);
}

void RealtimeEngine::publishTiming(double elapsedUsec, AudioBufferView buffer) noexcept
{
    callbackCount.fetch_add(1, std::memory_order_relaxed);
    lastProcessUsec.store(elapsedUsec, std::memory_order_relaxed);

    auto currentPeak = peakProcessUsec.load(std::memory_order_relaxed);
    while (elapsedUsec > currentPeak &&
           !peakProcessUsec.compare_exchange_weak(currentPeak, elapsedUsec, std::memory_order_relaxed))
    {
    }

    if (buffer.sampleRate > 0 && buffer.samplesPerFrame > 0)
    {
        const double budgetUsec =
            (static_cast<double>(buffer.samplesPerFrame) / static_cast<double>(buffer.sampleRate)) * 1000000.0;
        callbackCpuPercent.store((elapsedUsec / budgetUsec) * 100.0, std::memory_order_relaxed);
    }
}
}
