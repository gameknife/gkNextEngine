#pragma once

// ============================================================================
// BulletPool.hpp - Every bullet in flight, ours and theirs.
//
// One node pool per visual, because each visual is its own prefab and therefore
// its own model. A bullet occupies a slot in its visual's pool for as long as it
// is alive; running a visual dry drops the shot rather than stealing another
// visual's node, which keeps a heavy boss pattern from making the player's own
// gun stop drawing.
// ============================================================================

#include <array>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextRaiden/Combat/BulletPattern.hpp"
#include "Application/Game/NextRaiden/NextRaidenTypes.hpp"
#include "Engine/Assets/AssetsFwd.hpp"

namespace NextRaiden
{
    class FPrefabRack;
    class FScrollCamera;

    struct FBullet
    {
        glm::vec3 Position{0.0f};
        glm::vec3 Velocity{0.0f};
        float Radius = 0.35f;
        float Damage = 1.0f;
        bool Hostile = false;
        bool Active = false;
        EBulletVisual Visual = EBulletVisual::Bolt;
        int Slot = -1; // index into the visual's node pool
    };

    /// How many nodes to clone per visual, and how big to draw them. Sized for the worst
    /// volley the stage can throw, not the average: a dropped bullet in a boss pattern is a
    /// hole the player can walk through without knowing it was ever there.
    ///
    /// The scales are not decoration. The kit draws ordnance at life size, and from 47 m up a
    /// life-size 20 cm cannon round is three pixels of dark grey - invisible against the
    /// runway, which in a bullet-hell game is the difference between hard and unfair. Gameplay
    /// radii stay independent of this, so scaling up what the player sees never quietly
    /// enlarges what actually hits them.
    struct FBulletPoolSizes
    {
        std::array<int, static_cast<size_t>(EBulletVisual::Count)> PerVisual{{64, 192, 96, 96, 32, 24, 8}};
        std::array<float, static_cast<size_t>(EBulletVisual::Count)> Scale{{2.6f, 2.6f, 2.0f, 2.4f, 1.6f, 1.4f, 1.2f}};
    };

    class FBulletPool
    {
    public:
        /// Scene-build time: clones every bullet visual the stage will need.
        void Create(FPrefabRack& rack, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                    const FBulletPoolSizes& sizes);
        void Clear();

        /// Fires one seed. Returns false when that visual's pool is full.
        bool Emit(const FBulletSeed& seed, bool hostile, float radius);

        /// Integrates and culls. Bullets die when they leave the play area with a margin,
        /// measured against the flight plane rather than the ground: a bullet that vanished
        /// while still visible is the kind of unfairness players feel but cannot name.
        void Step(float deltaSeconds, const FScrollCamera& camera);
        void Apply();

        std::vector<FBullet>& Bullets() { return bullets_; }
        const std::vector<FBullet>& Bullets() const { return bullets_; }
        int AliveCount() const { return aliveCount_; }
        /// Live enemy fire only. The total is dominated by the player's own stream, which
        /// auto-fire keeps topped up forever, so "is the screen clear" is a question only this
        /// number can answer.
        int HostileCount() const;
        /// Kills every hostile bullet on screen. This is what a bomb does.
        int ClearHostile();

        void Kill(FBullet& bullet);

    private:
        struct FVisualPool
        {
            std::vector<std::shared_ptr<Assets::Node>> Nodes;
            std::vector<int> Free;
        };

        std::array<FVisualPool, static_cast<size_t>(EBulletVisual::Count)> visuals_{};
        FBulletPoolSizes sizes_{};
        std::vector<FBullet> bullets_;
        int aliveCount_ = 0;
    };
}
