#pragma once

// ============================================================================
// ZombieSystem.hpp - Infected simulation: perception (sight + noise), a small
// state machine, steering with turn-rate and acceleration limits, separation,
// attacks timed to their clips, and hit reactions (flinch, poise stagger, leg
// crippling, directional deaths with momentum). Pure data + rules: rendering
// reads FZombieRuntime (ZombieRigVisual), damage comes in through ApplyDamage.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

#include <span>

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"
#include "Application/Game/NextDayz/Combat/NoiseSystem.hpp"
#include "Application/Game/NextDayz/Data/ZombieDefs.hpp"

namespace NextDayz
{
    enum class EZombieState : uint8_t
    {
        Dormant,
        Wander,
        Investigate,
        Alert,
        Chase,
        Attack,
        Stagger,
        Dead,
    };
    inline constexpr size_t kZombieStateCount = static_cast<size_t>(EZombieState::Dead) + 1;

    inline const char* ZombieStateName(EZombieState state)
    {
        switch (state)
        {
        case EZombieState::Dormant: return "Dormant";
        case EZombieState::Wander: return "Wander";
        case EZombieState::Investigate: return "Investigate";
        case EZombieState::Alert: return "Alert";
        case EZombieState::Chase: return "Chase";
        case EZombieState::Attack: return "Attack";
        case EZombieState::Stagger: return "Stagger";
        case EZombieState::Dead: return "Dead";
        }
        return "Unknown";
    }

    enum class EZombieAttack : uint8_t
    {
        Lunge,
        Swipe,
    };

    enum class EZombieDeath : uint8_t
    {
        Back,
        Forward,
        Crumple,
    };

    inline const char* ZombieDeathName(EZombieDeath death)
    {
        switch (death)
        {
        case EZombieDeath::Forward: return "forward";
        case EZombieDeath::Crumple: return "crumple";
        case EZombieDeath::Back: break;
        }
        return "back";
    }

    // Per-spawn body language, hashed from the handle so a replay looks the same.
    struct FZombieVariant
    {
        float speedScale = 1.0f;
        float hunchDegrees = 0.0f;
        float headTiltDegrees = 0.0f;
        float reach = 0.5f;          // how eagerly the arms come up in a chase
        float phase = 0.0f;          // gait phase offset
        bool lame = false;           // born with a dragging leg
    };

    struct FZombieDamage
    {
        float amount = 0.0f;                   // before the zone multiplier
        EHitZone zone = EHitZone::Torso;
        glm::vec3 direction{0.0f, 0.0f, 1.0f}; // travel direction of the round
        glm::vec3 point{};
        glm::vec3 source{};                    // where the attacker stands
        float impact = 0.0f;                   // poise damage
    };

    struct FZombieDamageResult
    {
        bool applied = false;
        bool killed = false;
        bool staggered = false;
        bool crippled = false;                 // this hit lamed it
        float dealt = 0.0f;
    };

    struct FZombieRuntime
    {
        FZombieHandle handle{};
        EZombieProfile profile = EZombieProfile::Civilian;
        EZombieState state = EZombieState::Dormant;
        glm::vec3 position{};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
        glm::vec3 velocity{};
        glm::vec3 spawnPosition{};
        glm::vec3 lastKnownPlayerPosition{};
        FZombieVariant variant{};
        float health = 100.0f;
        float stateSeconds = 0.0f;
        float attackTimer = 0.0f;
        float attackCooldown = 0.0f;
        float lostSightSeconds = 0.0f;
        float corpseSeconds = 0.0f;
        float repathSeconds = 0.0f;
        float stuckSeconds = 0.0f;
        float poise = 0.0f;
        float staggerImmunity = 0.0f;
        float flinchSeconds = 0.0f;
        float legDamage = 0.0f;
        float roamSeconds = 0.0f;
        uint32_t roamCount = 0;
        uint32_t attackCount = 0;
        EZombieAttack attack = EZombieAttack::Lunge;
        EZombieDeath death = EZombieDeath::Back;
        glm::vec3 staggerDirection{0.0f, 0.0f, -1.0f};
        glm::vec3 deathVelocity{};
        glm::vec3 roamTarget{};
        glm::vec3 pathTarget{};
        glm::vec3 progressPosition{};
        std::vector<glm::vec3> path;
        size_t pathWaypoint = 0;
        uint64_t lastNoiseSequence = 0;
        bool attackCommitted = false;
        bool crippled = false;
        bool aware = false;          // has seen or been shot by the player since spawning
        bool active = false;
        // Diagnostics for the debugger: written by Update, never read by the rules.
        bool seesPlayer = false;     // the last perception pass saw the player
        float heardNoise = 0.0f;     // loudest new noise heard in the last update (0 = none)
        uint32_t pathSolves = 0;     // routes planned since it spawned
        bool pathFailed = false;     // the last route came back empty

        bool Lame() const { return crippled || variant.lame; }
        float Speed() const;
    };

    struct FPlayerDamageRequest
    {
        FZombieHandle source{};
        float amount = 0.0f;
        glm::vec3 sourcePosition{};
    };

    class ZombieSystem
    {
    public:
        using FPathResolver = std::function<std::vector<glm::vec3>(const glm::vec3&, const glm::vec3&)>;

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
        FZombieDamageResult ApplyDamage(FZombieHandle handle, const FZombieDamage& damage);

        void Update(float deltaSeconds, const glm::vec3& playerPosition, const glm::vec3& playerForward,
                    bool playerAlive, const std::function<bool(const glm::vec3&, const glm::vec3&)>& hasLineOfSight,
                    std::span<const FNoiseEvent> noises, const FPathResolver& resolvePath = {});

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
        bool CanSeePlayer(const FZombieRuntime& zombie, const glm::vec3& playerPosition,
                          const std::function<bool(const glm::vec3&, const glm::vec3&)>& hasLineOfSight) const;
        void UpdateAttack(FZombieRuntime& zombie, const glm::vec3& playerPosition, bool playerAlive,
                          float deltaSeconds);
        void Separate(float deltaSeconds, const glm::vec3& playerPosition);
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
