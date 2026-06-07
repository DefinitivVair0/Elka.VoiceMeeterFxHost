#pragma once

#include <cstdint>

namespace elka
{
struct AudioBufferView
{
    int sampleRate = 0;
    int samplesPerFrame = 0;
    int inputChannels = 0;
    int outputChannels = 0;
    float** read = nullptr;
    float** write = nullptr;
};
}

