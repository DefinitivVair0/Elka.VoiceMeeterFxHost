#pragma once

#include "engine/AudioBufferView.h"

#include <atomic>
#include <algorithm>

namespace elka
{
class GainProcessor
{
public:
    void setGain(float gain) noexcept
    {
        gainLinear.store(std::clamp(gain, 0.0f, 2.0f), std::memory_order_release);
    }

    float getGain() const noexcept
    {
        return gainLinear.load(std::memory_order_acquire);
    }

    void processRange(AudioBufferView buffer, int readOffset, int targetStart, int targetCount) noexcept
    {
        const auto gain = gainLinear.load(std::memory_order_relaxed);
        const int availableChannels = std::max(0, std::min(buffer.outputChannels, buffer.inputChannels - readOffset));

        const int start = std::clamp(targetStart, 0, availableChannels);
        const int end = targetCount < 0
            ? availableChannels
            : std::min(availableChannels, start + std::max(0, targetCount));

        for (int ch = start; ch < end; ++ch)
        {
            const float* in = buffer.read[readOffset + ch];
            float* out = buffer.write[ch];

            if (in == nullptr || out == nullptr)
                continue;

            for (int sample = 0; sample < buffer.samplesPerFrame; ++sample)
                out[sample] = in[sample] * gain;
        }
    }

private:
    std::atomic<float> gainLinear { 1.0f };
};
}
