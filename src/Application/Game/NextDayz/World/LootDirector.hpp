#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

#include "Application/Game/NextDayz/Data/DeterministicRng.hpp"
#include "Application/Game/NextDayz/Data/WorldProfiles.hpp"

namespace NextDayz
{
    enum class ELootSlotState : uint8_t
    {
        Available,
        Reserved,
        Cooldown,
    };

    struct FLootSlotHandle
    {
        uint32_t index = 0;
        uint32_t generation = 0;
        bool IsValid() const { return generation != 0; }
    };

    struct FLootSlot
    {
        glm::vec3 worldPos{};
        ELootProfile profile = ELootProfile::Residential;
        ELootCategory category = ELootCategory::Misc;
        ELootSlotState state = ELootSlotState::Available;
        double committedAt = 0.0;
        double cooldownUntil = 0.0;
        double offscreenSince = -1.0;
        uint32_t roll = 0;
    };

    class LootDirector
    {
    public:
        void Configure(const FLootRespawnTuning& tuning) { tuning_ = tuning; }
        const FLootRespawnTuning& Tuning() const { return tuning_; }
        // Shortens (or stretches) the cooldown of every spot of a category
        // already waiting, e.g. food comes back sooner while the player starves.
        void SetCooldownScale(ELootCategory category, float scale);
        // Starts a spot in cooldown (a spot that rolled empty at world start).
        bool StartCooldown(FLootSlotHandle handle, double worldSeconds, double seconds);
        // Restocks a spot right now, whatever its cooldown (radio supply cache).
        bool ForceAvailable(FLootSlotHandle handle);
        // Re-labels a spot after it rolled different contents.
        void SetCategory(FLootSlotHandle handle, ELootCategory category);
        void Reset(uint32_t generation, uint64_t seed);
        FLootSlotHandle AddSlot(const glm::vec3& position, ELootProfile profile, ELootCategory category);

        bool Reserve(FLootSlotHandle handle);
        bool Commit(FLootSlotHandle handle, double worldSeconds);
        bool Cancel(FLootSlotHandle handle);
        bool EvaluateRespawn(FLootSlotHandle handle, double worldSeconds, float playerDistance, bool visible,
                             int categoryAvailable, int categoryCap);

        const FLootSlot* Resolve(FLootSlotHandle handle) const;
        int Count(ELootSlotState state) const;
        uint32_t Generation() const { return generation_; }

    private:
        FLootSlot* ResolveMutable(FLootSlotHandle handle);

        FLootRespawnTuning tuning_{};
        std::array<float, static_cast<size_t>(ELootCategory::Count)> cooldownScale_{1, 1, 1, 1, 1, 1};
        std::vector<FLootSlot> slots_;
        FDeterministicRng rng_;
        uint32_t generation_ = 0;
    };
}
