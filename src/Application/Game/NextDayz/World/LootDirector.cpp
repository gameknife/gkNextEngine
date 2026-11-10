#include "Engine/Common/CoreMinimal.hpp"

#include "LootDirector.hpp"

namespace NextDayz
{
    void LootDirector::Reset(uint32_t generation, uint64_t seed)
    {
        generation_ = generation == 0 ? 1 : generation;
        rng_.Reset(seed);
        slots_.clear();
    }

    FLootSlotHandle LootDirector::AddSlot(const glm::vec3& position, ELootProfile profile, ELootCategory category)
    {
        FLootSlot slot;
        slot.worldPos = position;
        slot.profile = profile;
        slot.category = category;
        slot.roll = rng_.NextU32();
        slots_.push_back(slot);
        return {static_cast<uint32_t>(slots_.size() - 1), generation_};
    }

    FLootSlot* LootDirector::ResolveMutable(FLootSlotHandle handle)
    {
        if (!handle.IsValid() || handle.generation != generation_ || handle.index >= slots_.size())
        {
            return nullptr;
        }
        return &slots_[handle.index];
    }

    const FLootSlot* LootDirector::Resolve(FLootSlotHandle handle) const
    {
        return const_cast<LootDirector*>(this)->ResolveMutable(handle);
    }

    bool LootDirector::Reserve(FLootSlotHandle handle)
    {
        FLootSlot* slot = ResolveMutable(handle);
        if (!slot || slot->state != ELootSlotState::Available)
        {
            return false;
        }
        slot->state = ELootSlotState::Reserved;
        return true;
    }

    bool LootDirector::RestoreSlot(FLootSlotHandle handle, const FLootSlot& saved)
    {
        FLootSlot* slot = ResolveMutable(handle);
        if (!slot) return false;
        const glm::vec3 position = slot->worldPos;
        *slot = saved;
        slot->worldPos = position;
        if (slot->state == ELootSlotState::Reserved) slot->state = ELootSlotState::Available;
        return true;
    }

    bool LootDirector::Commit(FLootSlotHandle handle, double worldSeconds)
    {
        FLootSlot* slot = ResolveMutable(handle);
        if (!slot || slot->state != ELootSlotState::Reserved)
        {
            return false;
        }
        slot->state = ELootSlotState::Cooldown;
        slot->committedAt = worldSeconds;
        slot->cooldownUntil = worldSeconds + RespawnCooldown(slot->category, tuning_);
        slot->offscreenSince = -1.0;
        return true;
    }

    bool LootDirector::StartCooldown(FLootSlotHandle handle, double worldSeconds, double seconds)
    {
        FLootSlot* slot = ResolveMutable(handle);
        if (!slot || slot->state == ELootSlotState::Reserved)
        {
            return false;
        }
        slot->state = ELootSlotState::Cooldown;
        slot->committedAt = worldSeconds;
        slot->cooldownUntil = worldSeconds + seconds;
        slot->offscreenSince = -1.0;
        return true;
    }

    void LootDirector::SetCooldownScale(ELootCategory category, float scale)
    {
        if (category < ELootCategory::Count)
        {
            cooldownScale_[static_cast<size_t>(category)] = std::max(scale, 0.0f);
        }
    }

    bool LootDirector::ForceAvailable(FLootSlotHandle handle)
    {
        FLootSlot* slot = ResolveMutable(handle);
        if (!slot || slot->state == ELootSlotState::Reserved)
        {
            return false;
        }
        slot->state = ELootSlotState::Available;
        slot->cooldownUntil = 0.0;
        slot->offscreenSince = -1.0;
        return true;
    }

    void LootDirector::SetCategory(FLootSlotHandle handle, ELootCategory category)
    {
        if (FLootSlot* slot = ResolveMutable(handle))
        {
            slot->category = category;
        }
    }

    bool LootDirector::Cancel(FLootSlotHandle handle)
    {
        FLootSlot* slot = ResolveMutable(handle);
        if (!slot || slot->state != ELootSlotState::Reserved)
        {
            return false;
        }
        slot->state = ELootSlotState::Available;
        return true;
    }

    bool LootDirector::EvaluateRespawn(FLootSlotHandle handle, double worldSeconds, float playerDistance,
                                       bool visible, int categoryAvailable, int categoryCap)
    {
        FLootSlot* slot = ResolveMutable(handle);
        if (!slot || slot->state != ELootSlotState::Cooldown)
        {
            return false;
        }
        if (visible || playerDistance < tuning_.minimumPlayerDistance)
        {
            slot->offscreenSince = -1.0;
            slot->block = visible ? ELootRespawnBlock::Watched : ELootRespawnBlock::PlayerNear;
            return false;
        }
        if (slot->offscreenSince < 0.0)
        {
            slot->offscreenSince = worldSeconds;
        }
        const double readyAt = ReadyAt(handle);
        if (worldSeconds < readyAt)
        {
            slot->block = ELootRespawnBlock::Cooling;
            return false;
        }
        if (worldSeconds - slot->offscreenSince < tuning_.minimumOffscreenSeconds)
        {
            slot->block = ELootRespawnBlock::Offscreen;
            return false;
        }
        if (categoryAvailable >= categoryCap)
        {
            slot->block = ELootRespawnBlock::CategoryFull;
            return false;
        }
        slot->block = ELootRespawnBlock::None;
        slot->state = ELootSlotState::Available;
        slot->roll = rng_.NextU32();
        slot->cooldownUntil = 0.0;
        slot->offscreenSince = -1.0;
        return true;
    }

    double LootDirector::ReadyAt(FLootSlotHandle handle) const
    {
        const FLootSlot* slot = Resolve(handle);
        if (!slot || slot->state != ELootSlotState::Cooldown)
        {
            return 0.0;
        }
        const double scale = static_cast<double>(CooldownScale(slot->category));
        return slot->committedAt + (slot->cooldownUntil - slot->committedAt) * scale;
    }

    int LootDirector::Count(ELootSlotState state) const
    {
        return static_cast<int>(std::count_if(slots_.begin(), slots_.end(),
            [state](const FLootSlot& slot) { return slot.state == state; }));
    }
}
