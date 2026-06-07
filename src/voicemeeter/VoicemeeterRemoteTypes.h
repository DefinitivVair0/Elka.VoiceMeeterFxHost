#pragma once

#include <cstdint>

namespace elka::vmr
{
struct AudioInfo
{
    long sampleRate;
    long samplesPerFrame;
};

struct AudioBuffer
{
    long sampleRate;
    long samplesPerFrame;
    long inputChannels;
    long outputChannels;
    float* read[128];
    float* write[128];
};

using AudioCallback = long(__stdcall*)(void* user, long command, void* data, long reserved);

constexpr long CommandStarting = 1;
constexpr long CommandEnding = 2;
constexpr long CommandChange = 3;
constexpr long CommandBufferIn = 10;
constexpr long CommandBufferOut = 11;
constexpr long CommandBufferMain = 20;

constexpr long ModeInputInsert = 0x00000001;
constexpr long ModeOutputInsert = 0x00000002;
constexpr long ModeMain = 0x00000004;
}

