#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <glm/vec3.hpp>
#include "Application/Game/NextDayz/Data/LootTables.hpp"

namespace NextDayz
{
    enum class ELootSource : uint8_t
    {
        Authored,
        Spot,
        Pile,
    };

    // One loot entry as a network client needs it: where, what, and whether it is there.
    struct FLootReplica
    {
        std::string id;            // FLootEntry::persistentId
        ELootSource source = ELootSource::Spot;
        glm::vec3 position{};
        float yaw = 0.0f;
        bool available = false;
        std::vector<FLootStack> contents;
    };
}
