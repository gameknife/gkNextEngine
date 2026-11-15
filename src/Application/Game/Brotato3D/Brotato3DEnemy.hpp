#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Brotato3DDataLoader.hpp"
#include "Engine/Runtime/Subsystems/NextPhysicsTypes.hpp"

namespace Assets
{
    class Node;
}

namespace Brotato3D
{
    class FZombieRigVisual;

    enum class ELanceState : uint8_t
    {
        Idle,
        Telegraph,
        Dashing,
        Recovering,
    };

    struct FEnemyRuntime
    {
        const FEnemyDef* def = nullptr;
        glm::vec3 worldPos = glm::vec3(0.0f);
        float radius = 0.3f;
        int currentHp = 0;
        int maxHp = 0;
        bool alive = false;
        bool fading = false;
        float hitFlashRemainingMs = 0.0f;
        float deathFadeMs = 0.0f;
        float contactCooldownMs = 0.0f;
        float rangedFireCooldownMs = 0.0f;
        float chargeRampMs = 0.0f;
        float chargeCooldownMs = 0.0f;
        bool charging = false;
        float bombFuseMs = -1.0f;
        float healIntervalMs = 0.0f;
        bool bossPhase2Active = false;
        float mortarFireCooldownMs = 0.0f;
        float mortarTelegraphRemainingMs = 0.0f;
        glm::vec3 mortarTargetPos = glm::vec3(0.0f);
        ELanceState lanceState = ELanceState::Idle;
        float lanceStateMs = 0.0f;
        float lanceCooldownMs = 0.0f;
        glm::vec3 lanceDashDir = glm::vec3(0.0f);
        glm::vec3 lanceDashStartPos = glm::vec3(0.0f);
        float lanceDashRemainingDist = 0.0f;
        glm::vec3 lastHitDebrisDir = glm::vec3(0.0f, 0.0f, 1.0f);
        uint32_t runtimeTag = 0;
        uint32_t modelId = 0;
        uint32_t materialId = 0;
        uint32_t darkMaterialId = 0;
        uint32_t hitFlashMaterialId = 0;
        uint32_t warningMaterialId = 0;
        uint32_t phase2MaterialId = 0;
        NextBodyID kinematicBodyId{};
        bool kinematicBodyActive = false;

        // Presentation. The body is a pooled ScadRig (Brotato3DZombieVisual.hpp); a dead
        // enemy keeps it as a corpse while `fading`, then the slot is free again.
        std::shared_ptr<FZombieRigVisual> visual;
        float yaw = 0.0f;               // facing, radians around +Y (atan2(x, z))
        float moveSpeedNow = 0.0f;      // ground speed this frame, drives the gait
        float pulseScale = 1.0f;        // bloater swelling before it bursts
        float attackAnimCooldownMs = 0.0f;
        float corpseMs = 0.0f;          // time left as a corpse (counts down while fading)
        bool crawling = false;          // a leg is gone: prone, slow, still biting
        int limbStage = 0;              // how many HP thresholds have already cost a limb
        bool lastHitCrit = false;
        bool lastHitExplosive = false;
        // Navigation: the steered walking direction, and whether the player was in plain sight
        // this frame (if not, the zombie follows the arena flow field around the buildings).
        glm::vec3 navDir = glm::vec3(0.0f);
        bool seesPlayer = true;
        int lastHitDamage = 0;
    };
}
