#pragma once

namespace elka
{
struct ChannelRange
{
    int start = 0;
    int count = 0;
};

struct VoiceMeeterPotatoLayout
{
    static constexpr ChannelRange inputInsert { 0, 34 };
    static constexpr ChannelRange outputInsert { 0, 64 };
    static constexpr int mainInputChannels = 34;
    static constexpr int mainOutputChannels = 64;

    static constexpr ChannelRange strip1 { 0, 2 };
    static constexpr ChannelRange strip2 { 2, 2 };
    static constexpr ChannelRange strip3 { 4, 2 };
    static constexpr ChannelRange strip4 { 6, 2 };
    static constexpr ChannelRange strip5 { 8, 2 };
    static constexpr ChannelRange virtualInput1 { 10, 8 };
    static constexpr ChannelRange virtualAux { 18, 8 };
    static constexpr ChannelRange virtualVaio3 { 26, 8 };

    static constexpr ChannelRange busA1 { 0, 8 };
    static constexpr ChannelRange busA2 { 8, 8 };
    static constexpr ChannelRange busA3 { 16, 8 };
    static constexpr ChannelRange busA4 { 24, 8 };
    static constexpr ChannelRange busA5 { 32, 8 };
    static constexpr ChannelRange busB1 { 40, 8 };
    static constexpr ChannelRange busB2 { 48, 8 };
    static constexpr ChannelRange busB3 { 56, 8 };
};
}
