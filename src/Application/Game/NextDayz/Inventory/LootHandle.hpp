#pragma once

#include <cstdint>

namespace NextDayz
{
    struct FLootHandle
    {
        uint32_t index = 0;
        uint32_t nodeInstanceId = 0;
        uint32_t generation = 0;

        bool IsValid() const { return generation != 0; }
    };
}
