#pragma once

// ============================================================================
// BossSystem.hpp - The rig-driven bosses.
//
// A boss is a ScadRig character (rotorcraft / gunship / fortress) plus a list of
// phases. Each phase owns a clip, a movement style, up to two guns, and the set
// of bones that come off when the fight enters it - so "the gunship loses an
// engine at 30%" is a line of JSON, not a special case in code.
//
// The rig pool has to be declared while the scene is being built; the engine
// instantiates it before the game's OnSceneLoaded, which is why Declare and Bind
// are separate calls at different times.
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextRaiden/Combat/BulletPattern.hpp"
#include "Engine/Assets/AssetsFwd.hpp"

class NextRig;

namespace NextRaiden
{
    class FBulletPool;
    class FDebrisSystem;
    class FScrollCamera;
    class FRaidenRandom;

    enum class EBossMove : uint8_t
    {
        Hold,     // sit at the hold depth
        Strafe,   // slide side to side
        Charge,   // lunge towards the player's column and back
        Count
    };

    bool ParseBossMove(std::string_view name, EBossMove& out);

    struct FBossPhase
    {
        /// Entered when the remaining HP fraction drops to or below this.
        float HpFraction = 1.0f;
        std::string Clip;
        EBossMove Move = EBossMove::Strafe;
        float MoveSpeedScale = 1.0f;
        /// Bones shed on entering this phase, named exactly as the rig's modules are - with the
        /// bone_ prefix. FScadRigLoader keeps the module name verbatim for bones and only strips
        /// the prefix from *clip* names, so "pod_l" finds nothing and "bone_pod_l" works.
        std::vector<std::string> ShedBones;
        /// The bone the weak point sits on, empty when this phase has none. Taken from the rig
        /// rather than written as an offset so the exposed core is literally where the asset
        /// draws it - the fortress's doors swing open over `bone_reactor`, and that is the
        /// thing the player is being told to shoot.
        std::string WeakPointBone;
        float WeakPointRadius = 2.5f;
        float WeakPointMultiplier = 3.0f;
        /// What a hit anywhere else is worth while the weak point is exposed. Below 1 it is
        /// what makes the core worth aiming for instead of a bonus for hitting the middle.
        float BodyMultiplier = 1.0f;
        bool HasFire = false;
        bool HasFire2 = false;
        FPatternSpec Fire{};
        FPatternSpec Fire2{};
    };

    /// Damage multiplier for a hit landing at `hitPosition` in this phase, given where the weak
    /// point currently is. Pure on purpose: whether a boss fight lasts forty seconds or two
    /// minutes comes down to this arithmetic, and it should be checkable without a rig, a scene
    /// or a frame. Distance is measured on the XZ plane, like every other hit test in the game.
    float WeakPointDamageScale(const FBossPhase& phase, const glm::vec3& weakPoint,
                               const glm::vec3& hitPosition);

    struct FBossDesc
    {
        std::string Id;
        std::string RigPath;
        std::string EntryClip = "idle";
        float TriggerTime = 0.0f;   // stage seconds
        float HitPoints = 900.0f;
        float Radius = 5.0f;
        int Score = 20000;
        int DebrisPieces = 8;
        float HoldDepth = 12.0f;    // ahead of the camera centre
        float EntrySeconds = 2.0f;  // invulnerable arrival
        float MoveAmplitude = 9.0f;
        float MovePeriod = 4.0f;
        EItemKind Drop = EItemKind::None;
        /// A midboss lets the stage carry on; the stage boss ends it.
        bool EndsStage = false;
        std::vector<FBossPhase> Phases;
    };

    class FBossSystem
    {
    public:
        /// Scene-build time: one rig pool per boss the stage can spawn.
        void Declare(NextRig* rig, const std::vector<FBossDesc>& bosses, std::vector<Assets::Model>& models,
                     std::vector<Assets::FMaterial>& materials);
        void Bind(NextRig* rig, Assets::Scene* scene);
        void Unbind();

        /// Starts the boss with this index in the stage's list. Returns false when its rig pool
        /// is unavailable, in which case the caller should skip straight past the encounter
        /// rather than wait forever for a fight that cannot happen.
        bool Begin(size_t bossIndex, const FScrollCamera& camera);
        void Step(float deltaSeconds, const FScrollCamera& camera, const glm::vec3& playerPosition,
                  const glm::vec3& playerVelocity, FBulletPool& bullets, FDebrisSystem& debris,
                  FRaidenRandom& random);
        /// A shot, at the point it landed - which is what decides whether it hit the exposed
        /// weak point. Returns true when the hit killed it.
        bool ApplyDamage(float damage, const glm::vec3& hitPosition, FDebrisSystem& debris,
                         FRaidenRandom& random);
        /// A hit that covers the whole screen: the bomb. There is no aim in it, so it never
        /// crits - and this has to be its own entry point rather than "pass the boss's own
        /// position", because a weak point on the hull centreline (which the fortress's
        /// reactor is) would then be dead centre of the blast and score triple.
        bool ApplyAreaDamage(float damage, FDebrisSystem& debris, FRaidenRandom& random);
        void Despawn();
        /// Debug only (nr.bossHp): drops the live boss straight to a share of its health, so a
        /// fourth phase can be looked at without first playing the other three. Deliberately
        /// silent - no debris, no score - because it is not a hit.
        void SetHitPointsFraction(float fraction);

        bool IsActive() const { return active_; }
        bool IsVulnerable() const { return active_ && entryTimer_ <= 0.0f; }
        const glm::vec3& Position() const { return position_; }
        float Radius() const { return current_ ? current_->Radius : 0.0f; }
        float HitPoints() const { return hitPoints_; }
        float HitPointsFraction() const
        {
            return current_ && current_->HitPoints > 0.0f ? hitPoints_ / current_->HitPoints : 0.0f;
        }
        int PhaseIndex() const { return phase_; }
        const FBossDesc* Current() const { return current_; }
        bool HasPool(size_t bossIndex) const;

        /// True while this phase has a weak point and the boss can be hurt at all. The HUD says
        /// so out loud: a critical spot nobody can see is a tuning value, not a mechanic.
        bool WeakPointExposed() const;
        const glm::vec3& WeakPointPosition() const { return weakPoint_; }
        float WeakPointRadius() const;

    private:
        /// Shared tail of both damage entry points: subtract, then check for death.
        bool ApplyScaledDamage(float damage, FDebrisSystem& debris, FRaidenRandom& random);
        void EnterPhase(int phase, FDebrisSystem& debris, FRaidenRandom& random);
        void FirePattern(const FPatternSpec& spec, int volleyIndex, const glm::vec3& playerPosition,
                         const glm::vec3& playerVelocity, FBulletPool& bullets);

        std::vector<FBossDesc> bosses_;
        std::vector<uint32_t> pools_;
        NextRig* rig_ = nullptr;
        Assets::Scene* scene_ = nullptr;

        void UpdateWeakPoint();

        const FBossDesc* current_ = nullptr;
        size_t currentIndex_ = 0;
        uint32_t instance_ = 0;
        glm::vec3 position_{0.0f};
        glm::vec3 velocity_{0.0f};
        /// Refreshed once per step from the bone's world transform, which the rig recalculates
        /// when it ticks. A frame of lag on a 2.5 m circle is not something a player can feel.
        glm::vec3 weakPoint_{0.0f};
        float hitPoints_ = 0.0f;
        float age_ = 0.0f;
        float entryTimer_ = 0.0f;
        float fireTimer_ = 0.0f;
        float fire2Timer_ = 0.0f;
        float phaseDegrees_ = 0.0f;
        int volleyIndex_ = 0;
        int volleyIndex2_ = 0;
        int phase_ = -1;
        bool active_ = false;
    };
}
