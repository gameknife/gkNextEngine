#include "Engine/Common/CoreMinimal.hpp"

#include "SurvivalSystem.hpp"

namespace NextDayz
{
    void SurvivalSystem::Reset()
    {
        state_ = {};
        state_.health = config_.StartingHealth;
        state_.hunger = config_.StartingHunger;
        state_.hydration = config_.StartingHydration;
        state_.lifeState = EPlayerLifeState::Alive;
        damageProtectionRemaining_ = 0.0f;
        ClampState();
    }

    bool SurvivalSystem::IsWeak() const
    {
        return state_.hunger < config_.WeakThreshold || state_.hydration < config_.WeakThreshold;
    }

    void SurvivalSystem::Update(float simulationDeltaSeconds, bool sprinting)
    {
        const float deltaSeconds = std::max(simulationDeltaSeconds, 0.0f);
        damageProtectionRemaining_ = std::max(0.0f, damageProtectionRemaining_ - deltaSeconds);
        state_.healing = false;
        if (!IsAlive() || deltaSeconds <= 0.0f)
        {
            return;
        }
        const float hungerMultiplier = (sprinting ? config_.SprintHungerMultiplier : 1.0f) *
                                       (1.0f + state_.cold * config_.ColdHungerMultiplier);
        const float hydrationMultiplier = sprinting ? config_.SprintHydrationMultiplier : 1.0f;
        state_.hunger -= config_.HungerDrainPerSecond * hungerMultiplier * deltaSeconds;
        state_.hydration -= config_.HydrationDrainPerSecond * hydrationMultiplier * deltaSeconds;
        ClampState();

        // Healing: painkillers always work; a full stomach only heals a warm,
        // closed-up body.
        float heal = 0.0f;
        if (state_.painkillerSeconds > 0.0f)
        {
            const float step = std::min(deltaSeconds, state_.painkillerSeconds);
            heal += config_.PainkillerHeal / std::max(config_.PainkillerSeconds, 0.01f) * step;
            state_.painkillerSeconds -= step;
        }
        if (state_.hunger >= config_.WellFedThreshold && state_.hydration >= config_.WellFedThreshold &&
            state_.bleedingWounds == 0 && state_.cold < 0.3f)
        {
            heal += config_.WellFedRegenPerSecond * deltaSeconds;
        }
        if (heal > 0.0f && state_.health < 100.0f)
        {
            state_.health += heal;
            state_.healing = true;
        }

        float damage = 0.0f;
        std::string_view source;
        if (state_.bleedingWounds > 0)
        {
            damage += config_.BleedDamagePerSecondPerWound * static_cast<float>(state_.bleedingWounds) * deltaSeconds;
            source = "blood loss";
        }
        if (state_.hunger <= 0.0f)
        {
            damage += config_.StarvationDamagePerSecond * deltaSeconds;
            source = "starvation";
        }
        if (state_.hydration <= 0.0f)
        {
            damage += config_.DehydrationDamagePerSecond * deltaSeconds;
            source = "dehydration";
        }
        if (damage > 0.0f)
        {
            ApplyDamage(damage, source, false);
        }
        ClampState();
    }

    bool SurvivalSystem::ApplyDamage(float amount, std::string_view source, bool respectProtection)
    {
        if (!IsAlive() || amount <= 0.0f || (respectProtection && damageProtectionRemaining_ > 0.0f))
        {
            return false;
        }
        state_.health -= amount;
        state_.lastDamageSource = source;
        ++state_.damageSequence;
        if (respectProtection)
        {
            damageProtectionRemaining_ = config_.DamageProtectionSeconds;
        }
        ClampState();
        if (state_.health <= 0.0f)
        {
            state_.lifeState = EPlayerLifeState::Dead;
        }
        return true;
    }

    bool SurvivalSystem::ApplyInfectedHit(float amount, float armor, float roll)
    {
        const float absorbed = glm::clamp(armor, 0.0f, 0.9f);
        if (!ApplyDamage(amount * (1.0f - absorbed), "infected"))
        {
            return false;
        }
        // Armour also turns claws away from the skin.
        if (IsAlive() && roll < config_.BleedChance * (1.0f - absorbed * 1.5f) &&
            state_.bleedingWounds < config_.MaxWounds)
        {
            ++state_.bleedingWounds;
            ++state_.woundSequence;
        }
        return true;
    }

    bool SurvivalSystem::TryUseItem(Inventory& inventory, FItemInstanceId instanceId, std::string* outMessage)
    {
        if (!IsAlive())
        {
            return false;
        }
        const FItemInstance* instance = inventory.FindInstance(instanceId);
        const FItemDef* definition = instance ? FindItemDef(instance->defId) : nullptr;
        if (!definition || definition->kind != EItemKind::Consumable)
        {
            return false;
        }
        float share = 1.0f;
        if (definition->needsOpener && !inventory.CanOpenCans())
        {
            share = config_.UnopenedCanShare;
            if (outMessage)
            {
                *outMessage = "No can opener: you smash it open and spill half";
            }
        }
        Inventory transaction = inventory;
        if (!transaction.TryConsumeInstance(instanceId))
        {
            return false;
        }
        if (!definition->leftover.empty())
        {
            // Frees as much as it takes (same item volume), so it always fits.
            const FItemDef* leftover = FindItemDef(definition->leftover);
            transaction.TryAdd(std::string(definition->leftover),
                               leftover ? std::string(leftover->displayName) : std::string(definition->leftover),
                               leftover ? leftover->kind : EItemKind::Misc, 1);
        }
        inventory = std::move(transaction);

        state_.hunger += definition->hungerDelta * (definition->hungerDelta > 0.0f ? share : 1.0f);
        state_.hydration += definition->hydrationDelta * (definition->hydrationDelta > 0.0f ? share : 1.0f);
        state_.health += definition->healthDelta * share;
        switch (definition->use)
        {
        case EItemUse::Bandage:
        case EItemUse::FirstAid:
            if (state_.bleedingWounds > 0 && outMessage)
            {
                *outMessage = "The bleeding has stopped";
            }
            state_.bleedingWounds = 0;
            break;
        case EItemUse::Painkillers:
            state_.painkillerSeconds = config_.PainkillerSeconds;
            break;
        default:
            break;
        }
        ClampState();
        return true;
    }

    void SurvivalSystem::DrinkFromWell()
    {
        if (IsAlive())
        {
            state_.hydration += 55.0f;
            ClampState();
        }
    }

    bool SurvivalSystem::FillBottle(Inventory& inventory, FItemInstanceId emptyBottleId)
    {
        const FItemInstance* instance = inventory.FindInstance(emptyBottleId);
        if (!instance || instance->defId != "water_bottle_empty")
        {
            return false;
        }
        const FInventoryAddRequest bottle{"water_bottle", "Water Bottle", EItemKind::Consumable, 1};
        Inventory transaction = inventory;
        if (!transaction.TryConsumeInstance(emptyBottleId) || !transaction.TryAddBatch(std::span(&bottle, 1)))
        {
            return false;
        }
        inventory = std::move(transaction);
        return true;
    }

    bool SurvivalSystem::Rest()
    {
        if (!IsAlive() || state_.bleedingWounds > 0)
        {
            return false;
        }
        state_.health += config_.RestHeal;
        state_.hunger -= config_.RestHungerCost;
        state_.hydration -= config_.RestHydrationCost;
        ClampState();
        return true;
    }

    void SurvivalSystem::SetNeeds(float health, float hunger, float hydration)
    {
        state_.health = health;
        state_.hunger = hunger;
        state_.hydration = hydration;
        state_.lifeState = health > 0.0f ? EPlayerLifeState::Alive : EPlayerLifeState::Dead;
        ClampState();
    }

    void SurvivalSystem::ClampState()
    {
        state_.health = glm::clamp(state_.health, 0.0f, 100.0f);
        state_.hunger = glm::clamp(state_.hunger, 0.0f, 100.0f);
        state_.hydration = glm::clamp(state_.hydration, 0.0f, 100.0f);
    }
}
