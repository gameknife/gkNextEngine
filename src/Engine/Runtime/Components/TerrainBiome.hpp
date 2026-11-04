#pragma once
#include <cstdint>

namespace Runtime
{
    // Stable terrain semantic IDs, shared by producers and runtime vegetation.
    enum class ETerrainBiome : uint8_t
    {
        Grass = 0, GrassDark, DryGrass, Sand, Rock, RockHigh, Snow, Bed, Road, Pad, Count
    };
}
