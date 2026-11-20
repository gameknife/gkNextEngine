#pragma once

// ============================================================================
// ZombieSystem.hpp - Infected simulation: perception (sight + noise), a small
// state machine, steering with turn-rate and acceleration limits, separation,
// attacks timed to their clips, and hit reactions (flinch, poise stagger, leg
// crippling, directional deaths with momentum). Pure data + rules: rendering
// reads FZombieRuntime (ZombieRigVisual), damage comes in through ApplyDamage.
// ============================================================================

#include "Application/Game/NextDayz/Zombies/ZombieState.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

#include <span>

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"
#include "Application/Game/NextDayz/Combat/NoiseSystem.hpp"
#include "Application/Game/NextDayz/Data/ZombieDefs.hpp"

namespace NextDayz
{
    inline constexpr uint32_t kLocalPlayerTargetId = 1;

    struct FZombieDamage
    {
        float amount = 0.0f;                   // before the zone multiplier
        EHitZone zone = EHitZone::Torso;
        glm::vec3 direction{0.0f, 0.0f, 1.0f}; // travel direction of the round
        glm::vec3 point{};
        glm::vec3 source{};                    // where the attacker stands
        float impact = 0.0f;                   // poise damage
        uint32_t attackerId = 0;               // FZombieTarget::id of the shooter, 0 = unknown
    };

    // A player the infected may perceive and hunt. Single player passes one
    // (id kLocalPlayerTargetId); a server passes every connected survivor.
    struct FZombieTarget
    {
        uint32_t id = 0;                       // non-zero, stable while the player is in the world
        glm::vec3 position{};                  // feet
        bool alive = true;
        float sightScale = 1.0f;               // crouch / lantern, multiplied with the system-wide scale
    };

    struct FZombieDamageResult
    {
        bool applied = false;
        bool killed = false;
        bool staggered = false;
        bool crippled = false;                 // this hit lamed it
        float dealt = 0.0f;
    };

    struct FPlayerDamageRequest
    {
        uint32_t targetId = 0;                 // FZombieTarget::id of the survivor struck
        FZombieHandle source{};
        float amount = 0.0f;
        glm::vec3 sourcePosition{};
    };

    class ZombieSystem
    {
    public:
        using FPathResolver = std::function<std::vector<glm::vec3>(const glm::vec3&, const glm::vec3&)>;
        using FMovementResolver = std::function<glm::vec3(const glm::vec3&, const glm::vec3&)>;

        explicit ZombieSystem(size_t capacity = 56);

        void Configure(const FZombieConfig& config) { config_ = config; }
        // How far the infected see the player relative to their profile's
        // sight: darkness, a crouch and a lit lantern all change it.
        void SetSightScale(float scale) { sightScale_ = std::max(scale, 0.05f); }
        float SightScale() const { return sightScale_; }
        const FZombieConfig& Config() const { return config_; }

        void Reset();
        FZombieHandle Spawn(EZombieProfile profile, const glm::vec3& position);
        bool Recycle(FZombieHandle handle);
        // Network client: the server's infected become exactly these slots
        // (same index and generation); every other slot goes inactive. A slot
        // that keeps its handle keeps its position and facing, so the caller
        // can ease them toward the server's (they are overwritten otherwise).
        void ApplyReplicas(std::span<const FZombieRuntime> replicas);
        FZombieDamageResult ApplyDamage(FZombieHandle handle, const FZombieDamage& damage);

        using FLineOfSight = std::function<bool(const glm::vec3&, const glm::vec3&)>;

        // Every survivor in the world. Each infected keeps hunting its current
        // target while it can see it, otherwise turns to the nearest one it sees.
        void Update(float deltaSeconds, std::span<const FZombieTarget> targets, const FLineOfSight& hasLineOfSight,
                    std::span<const FNoiseEvent> noises, const FPathResolver& resolvePath = {},
                    const FMovementResolver& resolveMovement = {});
        // Single-survivor convenience: the player is target kLocalPlayerTargetId.
        void Update(float deltaSeconds, const glm::vec3& playerPosition, const glm::vec3& playerForward,
                    bool playerAlive, const FLineOfSight& hasLineOfSight,
                    std::span<const FNoiseEvent> noises, const FPathResolver& resolvePath = {},
                    const FMovementResolver& resolveMovement = {});

        const FZombieRuntime* Resolve(FZombieHandle handle) const;
        FZombieRuntime* Resolve(FZombieHandle handle);
        std::vector<FPlayerDamageRequest> ConsumePlayerDamageRequests();

        // Attack timing for the current attack (seconds).
        float AttackDuration(EZombieAttack attack) const;
        float AttackStrikeTime(EZombieAttack attack) const;

        int ActiveCount() const;
        int AlertedCount() const;
        int KillCount() const { return killCount_; }
        int StaggerCount() const { return staggerCount_; }
        // Route planning in the last Update: solves run, and re-plans that had
        // to wait for a later frame because the per-update budget was spent.
        int PathSolvesLastUpdate() const { return pathSolves_; }
        int PathDeferredLastUpdate() const { return pathDeferred_; }
        size_t Capacity() const { return slots_.size(); }
        const std::vector<FZombieRuntime>& Slots() const { return slots_; }

    private:
        // Turns toward `direction` at `turnDegrees`/s and accelerates along the
        // facing toward `speed` (slower while still turning).
        void Steer(FZombieRuntime& zombie, const glm::vec3& direction, float speed, float turnDegrees,
                   float deltaSeconds) const;
        void Face(FZombieRuntime& zombie, const glm::vec3& target, float turnDegrees, float deltaSeconds) const;
        // Returns false when no route exists; outDirection points at the current
        // waypoint (zero once there).
        bool NavigateToward(FZombieRuntime& zombie, const glm::vec3& target, float deltaSeconds,
                            const FPathResolver& resolvePath, glm::vec3& outDirection) const;
        bool CanSee(const FZombieRuntime& zombie, const FZombieTarget& target, const FLineOfSight& hasLineOfSight) const;
        // The target this infected perceives this update (nullptr: none in sight).
        // Prefers its current target, then the nearest visible survivor.
        const FZombieTarget* Perceive(const FZombieRuntime& zombie, std::span<const FZombieTarget> targets,
                                      const FLineOfSight& hasLineOfSight) const;
        void UpdateAttack(FZombieRuntime& zombie, const FZombieTarget* target, float deltaSeconds);
        void Move(FZombieRuntime& zombie, const glm::vec3& displacement, const FMovementResolver& resolveMovement) const;
        void Separate(float deltaSeconds, std::span<const FZombieTarget> targets, const FMovementResolver& resolveMovement);
        FZombieVariant MakeVariant(FZombieHandle handle) const;
        glm::vec3 NextRoamTarget(FZombieRuntime& zombie) const;

        FZombieConfig config_{};
        std::vector<FZombieRuntime> slots_;
        std::vector<uint32_t> generations_;
        std::vector<FPlayerDamageRequest> damageRequests_;
        int killCount_ = 0;
        int staggerCount_ = 0;
        float sightScale_ = 1.0f;
        mutable int pathBudget_ = 0;   // path solves left this update
        mutable int pathSolves_ = 0;
        mutable int pathDeferred_ = 0;
    };
}
