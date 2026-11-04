#pragma once

// ============================================================================
// DebugStyle.hpp - Colours shared by the debugger window and world overlay so
// a state reads the same in a table row, a label and a 3D line. Internal to
// the Debug/ translation units.
// ============================================================================

#include <imgui.h>
#include <glm/vec4.hpp>

#include "Application/Game/NextDayz/Data/WorldProfiles.hpp"
#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"
#include "Application/Game/NextDayz/Debugger/DebugEventLog.hpp"
#include "Application/Game/NextDayz/World/LootDirector.hpp"
#include "Application/Game/NextDayz/World/ZombieSpawnDirector.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieSystem.hpp"

namespace NextDayz::Debug::Style
{
    inline ImU32 U32(const glm::vec4& color)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
    }
    inline ImVec4 V4(const glm::vec4& color) { return ImVec4(color.r, color.g, color.b, color.a); }
    inline glm::vec4 Alpha(glm::vec4 color, float alpha)
    {
        color.a *= alpha;
        return color;
    }

    inline constexpr glm::vec4 kGood{0.35f, 0.92f, 0.45f, 1.0f};
    inline constexpr glm::vec4 kWarn{1.0f, 0.78f, 0.25f, 1.0f};
    inline constexpr glm::vec4 kBad{1.0f, 0.32f, 0.24f, 1.0f};
    inline constexpr glm::vec4 kMuted{0.58f, 0.62f, 0.66f, 1.0f};
    inline constexpr glm::vec4 kInfo{0.45f, 0.78f, 1.0f, 1.0f};
    inline constexpr glm::vec4 kPath{0.25f, 0.88f, 1.0f, 1.0f};
    inline constexpr glm::vec4 kSelect{1.0f, 1.0f, 1.0f, 1.0f};
    inline constexpr glm::vec4 kHover{1.0f, 0.3f, 0.95f, 1.0f};

    inline glm::vec4 ZombieState(EZombieState state)
    {
        switch (state)
        {
        case EZombieState::Wander: return {0.36f, 0.92f, 0.47f, 1.0f};
        case EZombieState::Investigate: return {1.0f, 0.86f, 0.27f, 1.0f};
        case EZombieState::Alert: return {1.0f, 0.70f, 0.24f, 1.0f};
        case EZombieState::Chase: return {1.0f, 0.57f, 0.18f, 1.0f};
        case EZombieState::Attack: return {1.0f, 0.22f, 0.18f, 1.0f};
        case EZombieState::Stagger: return {0.90f, 0.31f, 1.0f, 1.0f};
        case EZombieState::Dead: return {0.45f, 0.45f, 0.45f, 0.85f};
        case EZombieState::Dormant: break;
        }
        return {0.63f, 0.72f, 0.76f, 0.9f};
    }

    inline glm::vec4 HitZone(EHitZone zone)
    {
        switch (zone)
        {
        case EHitZone::Head: return {1.0f, 0.35f, 0.24f, 1.0f};
        case EHitZone::Torso: return {0.31f, 1.0f, 0.53f, 1.0f};
        case EHitZone::Limb: return {0.35f, 0.78f, 1.0f, 1.0f};
        case EHitZone::Leg: return {1.0f, 0.82f, 0.27f, 1.0f};
        }
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }

    inline glm::vec4 LootCategory(ELootCategory category)
    {
        switch (category)
        {
        case ELootCategory::FoodWater: return {0.31f, 0.86f, 1.0f, 0.95f};
        case ELootCategory::Medical: return {1.0f, 0.33f, 0.43f, 0.95f};
        case ELootCategory::Ammo: return {1.0f, 0.75f, 0.22f, 0.95f};
        case ELootCategory::Weapon: return {1.0f, 0.35f, 0.16f, 0.96f};
        case ELootCategory::Clothing: return {0.67f, 0.41f, 1.0f, 0.95f};
        case ELootCategory::Misc:
        case ELootCategory::Count: break;
        }
        return {0.80f, 0.84f, 0.86f, 0.9f};
    }

    inline glm::vec4 SpawnVerdict(ESpawnVerdict verdict)
    {
        switch (verdict)
        {
        case ESpawnVerdict::Spawned: return {0.35f, 1.0f, 0.45f, 1.0f};
        case ESpawnVerdict::Populated: return {0.36f, 0.80f, 0.40f, 0.95f};
        case ESpawnVerdict::Cooldown: return {0.45f, 0.62f, 1.0f, 0.95f};
        case ESpawnVerdict::Visible: return {1.0f, 0.86f, 0.27f, 1.0f};
        case ESpawnVerdict::NotNavigable: return {1.0f, 0.25f, 0.70f, 1.0f};
        case ESpawnVerdict::CapReached: return {1.0f, 0.45f, 0.20f, 1.0f};
        case ESpawnVerdict::TooClose: return {1.0f, 0.62f, 0.45f, 0.9f};
        case ESpawnVerdict::Recycled: return {0.70f, 0.70f, 0.85f, 0.9f};
        case ESpawnVerdict::TooFar:
        case ESpawnVerdict::Unevaluated: break;
        }
        return {0.55f, 0.58f, 0.62f, 0.75f};
    }

    inline glm::vec4 LootBlock(ELootRespawnBlock block)
    {
        switch (block)
        {
        case ELootRespawnBlock::Watched: return {1.0f, 0.86f, 0.27f, 1.0f};
        case ELootRespawnBlock::PlayerNear: return {1.0f, 0.62f, 0.35f, 1.0f};
        case ELootRespawnBlock::Cooling: return {0.55f, 0.65f, 0.95f, 1.0f};
        case ELootRespawnBlock::Offscreen: return {0.55f, 0.90f, 0.95f, 1.0f};
        case ELootRespawnBlock::CategoryFull: return {1.0f, 0.35f, 0.75f, 1.0f};
        case ELootRespawnBlock::None: break;
        }
        return kMuted;
    }

    inline glm::vec4 Severity(int severity)
    {
        switch (severity)
        {
        case 0: return {0.80f, 0.82f, 0.84f, 1.0f};
        case 1: return {0.55f, 0.85f, 1.0f, 1.0f};
        case 2: return kWarn;
        default: return kBad;
        }
    }

    inline glm::vec4 Channel(EChannel channel)
    {
        switch (channel)
        {
        case EChannel::System: return {0.70f, 0.70f, 0.75f, 1.0f};
        case EChannel::Hud: return {0.95f, 0.93f, 0.80f, 1.0f};
        case EChannel::Player: return {0.55f, 0.85f, 1.0f, 1.0f};
        case EChannel::Survival: return {1.0f, 0.50f, 0.50f, 1.0f};
        case EChannel::Weapon: return {1.0f, 0.75f, 0.40f, 1.0f};
        case EChannel::Combat: return {1.0f, 0.40f, 0.30f, 1.0f};
        case EChannel::Infected: return {1.0f, 0.60f, 0.20f, 1.0f};
        case EChannel::Spawn: return {0.45f, 0.95f, 0.55f, 1.0f};
        case EChannel::Loot: return {0.40f, 0.85f, 1.0f, 1.0f};
        case EChannel::Noise: return {1.0f, 0.90f, 0.35f, 1.0f};
        case EChannel::World: return {0.75f, 0.65f, 1.0f, 1.0f};
        case EChannel::Count: break;
        }
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }
}
