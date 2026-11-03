#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

class NextEngine;

namespace NextDayz
{
    enum class EWorldAnchorType : uint8_t
    {
        PlayerSafe,
        Zombie,
        Loot,
        Well,
        Poi,        // a named place for the map (nd_poi(name, kind))
    };

    struct FWorldAnchorHandle
    {
        uint32_t index = 0;
        uint32_t generation = 0;
        bool IsValid() const { return generation != 0; }
    };

    struct FWorldAnchor
    {
        EWorldAnchorType type = EWorldAnchorType::Loot;
        std::string profile;        // spawn/loot profile, or the POI kind
        std::string label;          // POI name
        glm::vec3 worldPos{};
        uint32_t nodeInstanceId = 0;
        int group = 1;              // infected anchors: how many spawn together
    };

    class WorldAnchorRegistry
    {
    public:
        void Scan(NextEngine& engine);
        void Clear();

        const FWorldAnchor* Resolve(FWorldAnchorHandle handle) const;
        std::vector<FWorldAnchorHandle> Find(EWorldAnchorType type) const;
        size_t Count(EWorldAnchorType type) const;
        const std::vector<FWorldAnchor>& All() const { return anchors_; }
        uint32_t Generation() const { return generation_; }

    private:
        std::vector<FWorldAnchor> anchors_;
        uint32_t generation_ = 0;
    };
}
