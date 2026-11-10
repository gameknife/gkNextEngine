#pragma once

// ============================================================================
// ItemSystem.hpp - Drops: power, option, bomb, repair, medal.
//
// Items sit still in the world, so the scrolling camera carries them down the
// screen on its own - the classic behaviour falls out of the scroll rather than
// being animated. Close to the player they are pulled in, because a power-up the
// player can see and cannot reach is worse than no drop at all.
// ============================================================================

#include <array>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextRaiden/NextRaidenTypes.hpp"
#include "Engine/Assets/AssetsFwd.hpp"

namespace NextRaiden
{
    class FPrefabRack;
    class FScrollCamera;

    struct FItemConfig
    {
        int PoolPerKind = 8;
        float DriftSpeed = 2.0f;      // extra m/s down the screen, on top of the scroll
        float MagnetRadius = 9.0f;
        float MagnetSpeed = 22.0f;
        float PickupRadius = 2.6f;
        float SpinDegreesPerSec = 90.0f;
        float Scale = 2.2f;           // same readability problem the bullets have
        /// Medals are worth more the more of them you take without missing one.
        std::array<int, 4> MedalValues{{100, 200, 500, 1000}};
        int PowerFullScore = 2000;
        int OptionFullScore = 3000;
    };

    struct FItem
    {
        EItemKind Kind = EItemKind::Power;
        glm::vec3 Position{0.0f};
        float Spin = 0.0f;
        bool Active = false;
        int Slot = -1;
    };

    class FItemSystem
    {
    public:
        void Create(FPrefabRack& rack, std::vector<std::shared_ptr<Assets::Node>>& nodes, const FItemConfig& config);
        void Clear();
        void Reset();

        bool Spawn(EItemKind kind, const glm::vec3& position);

        /// Moves items, pulls in nearby ones, and reports what the player picked up this step.
        void Step(float deltaSeconds, const FScrollCamera& camera, const glm::vec3& playerPosition,
                  std::vector<EItemKind>& outPickups);
        void Apply();

        int AliveCount() const { return aliveCount_; }
        const FItemConfig& Config() const { return config_; }

    private:
        struct FKindPool
        {
            std::vector<std::shared_ptr<Assets::Node>> Nodes;
            std::vector<int> Free;
        };

        void Release(FItem& item);

        std::array<FKindPool, static_cast<size_t>(EItemKind::Count)> kinds_{};
        std::vector<FItem> items_;
        FItemConfig config_{};
        int aliveCount_ = 0;
    };
}
