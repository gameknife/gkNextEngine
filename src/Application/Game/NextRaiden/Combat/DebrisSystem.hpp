#pragma once

// ============================================================================
// DebrisSystem.hpp - The only place NextRaiden touches NextPhysics.
//
// Shot-off pieces get a real dynamic body: they are thrown clear, fall, tumble,
// land on the runway or a roof, roll, pile up, and are recycled once they have
// slid off the bottom of the screen (design section 7.4).
//
// The hard boundary: debris does NOT participate in gameplay. It deals no
// damage, blocks nothing, scores nothing. It runs on the engine's physics tick
// rather than the game's fixed step, so anything it fed back into would stop
// being reproducible - and the validation assertions with it.
//
// Bodies are created once per pool slot and reused. Each slot is tied to one
// part prefab, so its box extent never changes, which makes create-once the
// obviously cheaper half of the trade the design left open.
// ============================================================================

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextRaiden/Combat/RaidenRandom.hpp"
#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Runtime/Subsystems/NextPhysicsTypes.hpp"

class NextPhysics;

namespace NextRaiden
{
    class FPrefabRack;

    struct FDebrisConfig
    {
        int Budget = 64;             // live pieces; 0 disables physics debris entirely
        int PerPartPrefab = 10;      // pool slots per nr_pf_part_* prefab
        float MinSpeed = 6.0f;       // outward impulse from the blast centre
        float MaxSpeed = 14.0f;
        float MinLift = 4.0f;        // upward component
        float MaxLift = 9.0f;
        float SpinRadiansPerSec = 6.0f;
        float MaxAgeSeconds = 12.0f; // only recycles a piece that has also gone to sleep
        float SleepSpeed = 0.35f;
        float RecycleMargin = 4.0f;  // past the ground's near edge
        float KillPlaneY = -5.0f;
    };

    class FDebrisSystem
    {
    public:
        /// Scene-build time: clone the part prefabs into node pools.
        void Create(FPrefabRack& rack, std::vector<std::shared_ptr<Assets::Node>>& nodes, const FDebrisConfig& config);
        /// Scene-live time: measure each prefab's box from its model bounds and give every slot
        /// a dynamic body. Measured rather than transcribed from the .scad, so an asset edit
        /// cannot silently leave the collider the wrong size.
        void Bind(Assets::Scene& scene, NextPhysics* physics);
        void Unbind();

        void SetConfig(const FDebrisConfig& config) { config_ = config; }
        void SetBudget(int budget);

        /// Throws `pieces` stand-in fragments from `at`, inheriting the victim's velocity.
        /// Used for objects whose geometry is merged and therefore has no piece to take off.
        void Spawn(const glm::vec3& at, const glm::vec3& inheritVelocity, int pieces, FRaidenRandom& random);

        /// Takes *the actual node* off whatever it was attached to and throws it. The piece that
        /// falls is the piece that was there a frame ago, which is the whole reason the kit
        /// authors its vehicles as separable chunks.
        ///
        /// One-shot: a detached chunk never goes back on, so its body is created here and
        /// destroyed on recycle rather than pooled. Returns false when the budget is full or
        /// the node cannot be measured, in which case the caller should just hide it.
        bool Detach(const std::shared_ptr<Assets::Node>& node, const glm::vec3& inheritVelocity,
                    FRaidenRandom& random);

        /// Recycling only - the bodies themselves are simulated by the engine.
        void Step(float deltaSeconds, float recycleZ);

        int AliveCount() const { return aliveCount_; }
        bool IsAvailable() const { return physics_ != nullptr && !slots_.empty(); }

    private:
        struct FSlot
        {
            std::shared_ptr<Assets::Node> Node;
            NextBodyID Body{};
            glm::vec3 HalfExtent{0.25f};
            /// Pivot-to-centre offset. Chunk pivots are bottom-centres and physics bodies are
            /// centred on the centre of mass; without this every piece floats half its own
            /// height above where it should be.
            glm::vec3 CentreOffset{0.0f};
            glm::vec3 Park{0.0f};
            float Age = 0.0f;
            bool Active = false;
        };

        /// A chunk taken off a live object. Same lifetime rules as a pooled slot, but it owns
        /// its body outright and its node is never reused.
        struct FDetached
        {
            std::shared_ptr<Assets::Node> Node;
            NextBodyID Body{};
            glm::vec3 CentreOffset{0.0f};
            float Age = 0.0f;
        };

        void Recycle(FSlot& slot);
        void RecycleDetached(FDetached& piece);
        FSlot* TakeSlot();
        /// True when recycling the oldest live piece freed room. Detached pieces go first:
        /// they are gone for good either way, and a stand-in fragment is cheaper to lose.
        bool MakeRoom();
        bool MeasureNode(const Assets::Node& node, glm::vec3& outHalfExtent, glm::vec3& outCentreOffset) const;
        glm::vec3 RandomLaunchVelocity(const glm::vec3& inheritVelocity, FRaidenRandom& random) const;
        glm::vec3 RandomSpin(FRaidenRandom& random) const;

        std::vector<FSlot> slots_;
        std::vector<FDetached> detached_;
        FDebrisConfig config_{};
        NextPhysics* physics_ = nullptr;
        Assets::Scene* scene_ = nullptr;
        int aliveCount_ = 0;
        /// Round-robin start, so successive explosions do not all draw the same part.
        size_t cursor_ = 0;
    };
}
