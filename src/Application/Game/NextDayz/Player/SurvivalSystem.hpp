#pragma once

// ============================================================================
// SurvivalSystem.hpp - The body: health, hunger, hydration, bleeding wounds,
// cold, painkiller regeneration and the well-fed heal. Pure rules driven by
// simulation seconds; the game feeds it exposure (night/weather vs worn
// warmth) and infected hits, and asks it to apply consumables.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Player/PlayerState.hpp"

namespace NextDayz
{
    struct FSurvivalSnapshot
    {
        float health = 100.0f;
        float hunger = 100.0f;
        float hydration = 100.0f;
        int bleedingWounds = 0;
        float cold = 0.0f;               // 0 warm .. 1 freezing
        float painkillerSeconds = 0.0f;  // regeneration left
        bool healing = false;            // well-fed or painkiller regeneration this tick
        EPlayerLifeState lifeState = EPlayerLifeState::Alive;
        uint64_t damageSequence = 0;
        uint64_t woundSequence = 0;      // bumps each time a new wound opens
        std::string lastDamageSource;
    };

    class SurvivalSystem
    {
    public:
        void Configure(const FSurvivalConfig& config) { config_ = config; }
        void Reset();
        void Update(float simulationDeltaSeconds, bool sprinting);

        // How cold the surroundings are for the clothes worn (0..1), set by the
        // game from time of day and weather each frame.
        void SetCold(float cold) { state_.cold = glm::clamp(cold, 0.0f, 1.0f); }

        bool ApplyDamage(float amount, std::string_view source, bool respectProtection = true);
        // An infected strike: armour absorbs part of it; `roll` (0..1) decides
        // whether it opens a bleeding wound. Returns true if it landed.
        bool ApplyInfectedHit(float amount, float armor, float roll);
        // Uses a consumable (eat, drink, bandage, first aid, painkillers).
        // `outMessage` gets what the player should be told, if anything.
        bool TryUseItem(Inventory& inventory, FItemInstanceId instanceId, std::string* outMessage = nullptr);
        void DrinkFromWell();
        bool FillBottle(Inventory& inventory, FItemInstanceId emptyBottleId);
        // A long rest on a bedroll. Fails while bleeding.
        bool Rest();

        const FSurvivalSnapshot& Snapshot() const { return state_; }
        bool IsAlive() const { return state_.lifeState == EPlayerLifeState::Alive; }
        bool IsBleeding() const { return state_.bleedingWounds > 0; }
        // Too hungry or thirsty to sprint.
        bool IsWeak() const;

        // Deterministic validation hook used by pure tests and agent-only callers.
        void SetNeeds(float health, float hunger, float hydration);
        void SetBleeding(int wounds) { state_.bleedingWounds = std::clamp(wounds, 0, config_.MaxWounds); }

    private:
        void ClampState();

        FSurvivalConfig config_{};
        FSurvivalSnapshot state_{};
        float damageProtectionRemaining_ = 0.0f;
    };
}
