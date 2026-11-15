#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include <spdlog/spdlog.h>

#include "Brotato3DAudio.hpp"

using namespace Brotato3DUtil;

namespace
{
    constexpr float CorpseLingerMs = 3600.0f;
    constexpr float BossCorpseLingerMs = 6000.0f;
    constexpr float CorpseSinkMs = 700.0f;
    constexpr float CrowdedCorpseLingerMs = 1300.0f;
    constexpr int CrowdedCorpseCount = 70;
    constexpr float CrawlSpeedScale = 0.42f;
    constexpr float EnemyTurnRate = 9.0f;
    constexpr float AttackAnimIntervalMs = 950.0f;
    constexpr float SeparationRadiusScale = 0.9f;   // bodies may overlap a little: a press, not a grid
    constexpr float SeparationMaxSpeed = 6.0f;      // m/s a body can be shouldered aside
    constexpr float ContactStandOff = 0.92f;        // stop at this share of the contact distance and claw

    glm::vec3 ResolveEnemyDebrisDir(const Brotato3D::FEnemyRuntime& enemy, const glm::vec3& fallbackDir)
    {
        glm::vec3 dir(enemy.lastHitDebrisDir.x, 0.0f, enemy.lastHitDebrisDir.z);
        if (glm::length(dir) < 0.001f)
        {
            dir = glm::vec3(fallbackDir.x, 0.0f, fallbackDir.z);
        }
        if (glm::length(dir) < 0.001f)
        {
            dir = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return glm::normalize(dir);
    }

    glm::vec3 EnemyFeet(const Brotato3D::FEnemyRuntime& enemy)
    {
        return glm::vec3(enemy.worldPos.x, 0.0f, enemy.worldPos.z);
    }
}

void Brotato3DGameInstance::LoadZombieRigs()
{
    zombieRigs_.clear();
    // The walker rig owns the clips every other variant adopts; load it once as the donor.
    auto donor = std::make_unique<Brotato3D::FZombieRigResource>();
    const bool donorLoaded = donor->Load("assets/scad/characters/brotato_zombie_walker.scad", nullptr);
    for (const auto& [enemyId, def] : enemyDefs_)
    {
        if (donorLoaded && def.rig == "walker")
        {
            // Solving the walker's clips is the expensive part of a rig load: reuse the donor.
            zombieRigs_[enemyId] = std::make_unique<Brotato3D::FZombieRigResource>(*donor);
            continue;
        }
        auto resource = std::make_unique<Brotato3D::FZombieRigResource>();
        const std::string path = fmt::format("assets/scad/characters/brotato_zombie_{}.scad", def.rig);
        if (!resource->Load(path, donorLoaded ? &donor->Asset() : nullptr))
        {
            spdlog::warn("[Brotato3D] enemy '{}' has no zombie rig; it will be invisible", enemyId);
        }
        zombieRigs_[enemyId] = std::move(resource);
    }
}

std::shared_ptr<Brotato3D::FZombieRigVisual> Brotato3DGameInstance::CreateZombieVisual(const std::string& enemyId, size_t serial)
{
    const auto it = zombieRigs_.find(enemyId);
    if (it == zombieRigs_.end() || !it->second || !it->second->IsReady())
    {
        return nullptr;
    }
    auto visual = std::make_shared<Brotato3D::FZombieRigVisual>();
    if (!visual->Instantiate(GetEngine().GetScene(), *it->second, fmt::format("Brotato3D_Zombie_{}_{}", enemyId, serial)))
    {
        return nullptr;
    }
    return visual;
}

void Brotato3DGameInstance::HideEnemyVisual(Brotato3D::FEnemyRuntime& enemy)
{
    if (enemy.fading)
    {
        corpseCount_ = std::max(0, corpseCount_ - 1);
    }
    enemy.fading = false;
    enemy.corpseMs = 0.0f;
    if (enemy.visual)
    {
        enemy.visual->Hide();
    }
}

void Brotato3DGameInstance::PlayEnemyAction(Brotato3D::FEnemyRuntime& enemy, Brotato3D::EZombieAction action, float playRate)
{
    if (enemy.visual && !enemy.crawling)
    {
        enemy.visual->PlayAction(action, playRate);
    }
}

void Brotato3DGameInstance::SpawnEnemy(const std::string& enemyId, const glm::vec3& worldPos)
{
    const auto defIt = enemyDefs_.find(enemyId);
    const auto visualIt = enemyVisuals_.find(enemyId);
    if (defIt == enemyDefs_.end() || visualIt == enemyVisuals_.end())
    {
        spdlog::warn("[Brotato3D] Unknown enemy id '{}'", enemyId);
        return;
    }

    const Brotato3D::FEnemyDef& def = defIt->second;
    const FEnemyVisualResource& visual = visualIt->second;
    // Never inside a building: the wave director picks points blind.
    const glm::vec3 open = arenaNav_.FindNearestFree(worldPos, std::max(def.size.x, def.size.z) * 0.5f, 24.0f);
    const glm::vec3 spawnPos(open.x, def.size.y * 0.5f, open.z);
    auto reusableEnemy = std::find_if(enemies_.begin(), enemies_.end(),
                                      [&def](const Brotato3D::FEnemyRuntime& enemy)
                                      {
                                          return !enemy.alive && !enemy.fading && enemy.def == &def;
                                      });

    Brotato3D::FEnemyRuntime enemy{};
    enemy.def = &def;
    enemy.worldPos = spawnPos;
    enemy.radius = std::max(def.size.x, def.size.z) * 0.5f;
    enemy.currentHp = def.hp;
    enemy.maxHp = def.hp;
    enemy.alive = true;
    enemy.modelId = visual.modelId;
    enemy.materialId = visual.materialId;
    enemy.darkMaterialId = visual.darkMaterialId;
    enemy.hitFlashMaterialId = visual.hitFlashMaterialId;
    enemy.warningMaterialId = visual.warningMaterialId;
    enemy.phase2MaterialId = visual.phase2MaterialId;
    const glm::vec3 toPlayer = player_.worldPos - spawnPos;
    enemy.yaw = std::atan2(toPlayer.x, toPlayer.z);

    Brotato3D::FEnemyRuntime* slot = nullptr;
    if (reusableEnemy != enemies_.end())
    {
        enemy.visual = std::move(reusableEnemy->visual);
        enemy.kinematicBodyId = reusableEnemy->kinematicBodyId.IsInvalid() ? AcquireEnemyKinematicBody(enemyId) :
                                                                        reusableEnemy->kinematicBodyId;
        enemy.runtimeTag = static_cast<uint32_t>(std::distance(enemies_.begin(), reusableEnemy)) + 1U;
        *reusableEnemy = std::move(enemy);
        slot = &*reusableEnemy;
    }
    else
    {
        enemy.kinematicBodyId = AcquireEnemyKinematicBody(enemyId);
        enemies_.push_back(std::move(enemy));
        enemies_.back().runtimeTag = static_cast<uint32_t>(enemies_.size());
        slot = &enemies_.back();
    }

    if (!slot->visual)
    {
        slot->visual = CreateZombieVisual(enemyId, zombieVisualSerial_++);
    }
    if (slot->visual)
    {
        const auto rigIt = zombieRigs_.find(enemyId);
        const size_t tintCount = rigIt != zombieRigs_.end() && rigIt->second ? std::max<size_t>(1, rigIt->second->TintCount()) : 1;
        const size_t tint = std::uniform_int_distribution<size_t>(0, tintCount - 1)(rng_);
        // A few percent of size variation keeps a crowd from reading as clones.
        const float scale = def.rigScale * std::uniform_real_distribution<float>(0.94f, 1.07f)(rng_);
        slot->visual->Reset(tint, scale, std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_));
        slot->visual->SetPlacement(EnemyFeet(*slot), slot->yaw);
        slot->visual->Update(0.0f);
    }
    // Spawn activation is a positional snap; use a stable fallback step for the kinematic body.
    SyncEnemyKinematicBody(*slot, 1.0 / 60.0);
}

// A horde is bodies, not a stack: zombies shoulder each other apart and pile up around the
// player instead of all standing on the same spot. Positions only; the gait follows the
// measured speed, so a zombie being squeezed sideways shuffles rather than slides.
void Brotato3DGameInstance::SeparateEnemies(double deltaSeconds)
{
    const float maxPush = SeparationMaxSpeed * static_cast<float>(deltaSeconds);
    const size_t count = enemies_.size();
    for (size_t a = 0; a < count; ++a)
    {
        Brotato3D::FEnemyRuntime& first = enemies_[a];
        if (!first.alive || !first.def)
        {
            continue;
        }
        const bool firstRushing = first.def->lance.enabled && first.lanceState == Brotato3D::ELanceState::Dashing;
        for (size_t b = a + 1; b < count; ++b)
        {
            Brotato3D::FEnemyRuntime& second = enemies_[b];
            if (!second.alive || !second.def)
            {
                continue;
            }
            const glm::vec2 delta(second.worldPos.x - first.worldPos.x, second.worldPos.z - first.worldPos.z);
            const float minDistance = (first.radius + second.radius) * SeparationRadiusScale;
            const float distanceSq = glm::dot(delta, delta);
            if (distanceSq >= minDistance * minDistance)
            {
                continue;
            }
            const float distance = std::sqrt(distanceSq);
            // Two bodies on the same point: split them along a stable per-pair axis.
            const glm::vec2 axis = distance > 1.0e-4f ? delta / distance :
                                                         glm::vec2(std::cos(static_cast<float>(a + b)), std::sin(static_cast<float>(a + b)));
            const float push = std::min((minDistance - distance) * 0.5f, maxPush);
            // Heavier bodies give way less; a linebacker mid-rush ploughs through.
            const bool secondRushing = second.def->lance.enabled && second.lanceState == Brotato3D::ELanceState::Dashing;
            const float firstMass = firstRushing ? 50.0f : first.radius * first.radius;
            const float secondMass = secondRushing ? 50.0f : second.radius * second.radius;
            const float firstShare = secondMass / (firstMass + secondMass);
            first.worldPos = ResolveEnemyGroundedPosition(
                first, first.worldPos - glm::vec3(axis.x, 0.0f, axis.y) * (push * 2.0f * firstShare));
            second.worldPos = ResolveEnemyGroundedPosition(
                second, second.worldPos + glm::vec3(axis.x, 0.0f, axis.y) * (push * 2.0f * (1.0f - firstShare)));
        }
    }
}

void Brotato3DGameInstance::UpdateEnemies(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    // Presentation needs where each body was before both behaviour and separation moved it.
    std::vector<glm::vec3> previousPositions(enemies_.size());
    for (size_t index = 0; index < enemies_.size(); ++index)
    {
        previousPositions[index] = enemies_[index].worldPos;
    }
    for (auto& enemy : enemies_)
    {
        if (!enemy.alive)
        {
            continue;
        }
        enemy.lastHitExplosive = false;
        if (!validation_.freeze)
        {
            UpdateEnemyBehaviour(enemy, deltaSeconds, deltaMs);
        }
        else
        {
            enemy.hitFlashRemainingMs = std::max(0.0f, enemy.hitFlashRemainingMs - deltaMs);
        }
    }
    if (!validation_.freeze)
    {
        SeparateEnemies(deltaSeconds);
    }
    for (size_t index = 0; index < enemies_.size() && index < previousPositions.size(); ++index)
    {
        Brotato3D::FEnemyRuntime& enemy = enemies_[index];
        if (enemy.alive)
        {
            UpdateEnemyPresentation(enemy, previousPositions[index], deltaSeconds);
        }
        else if (enemy.fading)
        {
            UpdateEnemyCorpse(enemy, deltaSeconds);
        }
    }
}

void Brotato3DGameInstance::UpdateEnemyCorpse(Brotato3D::FEnemyRuntime& enemy, double deltaSeconds)
{
    enemy.corpseMs -= static_cast<float>(deltaSeconds * 1000.0);
    if (enemy.corpseMs <= 0.0f || !enemy.visual)
    {
        HideEnemyVisual(enemy);
        return;
    }
    // The last moments sink the body into the ground instead of popping it.
    const float sink = 1.0f - std::clamp(enemy.corpseMs / CorpseSinkMs, 0.0f, 1.0f);
    glm::vec3 feet = EnemyFeet(enemy);
    feet.y = -sink * sink * 0.9f * (enemy.def ? enemy.def->rigScale : 1.0f);
    enemy.visual->SetPlacement(feet, enemy.yaw);
    enemy.visual->Update(static_cast<float>(deltaSeconds));
}

void Brotato3DGameInstance::UpdateEnemyPresentation(Brotato3D::FEnemyRuntime& enemy,
                                                    const glm::vec3& previousPos,
                                                    double deltaSeconds)
{
    if (!enemy.visual || !enemy.def)
    {
        return;
    }
    const float dt = static_cast<float>(deltaSeconds);
    const glm::vec2 step(enemy.worldPos.x - previousPos.x, enemy.worldPos.z - previousPos.z);
    const float speed = dt > 1.0e-5f ? glm::length(step) / dt : 0.0f;
    // Knockback and dashes are impulses, not strides: ease the gait toward the measured speed.
    enemy.moveSpeedNow = glm::mix(enemy.moveSpeedNow, std::min(speed, enemy.def->moveSpeed * 3.0f),
                                  1.0f - std::exp(-10.0f * dt));
    enemy.attackAnimCooldownMs = std::max(0.0f, enemy.attackAnimCooldownMs - dt * 1000.0f);

    // Face the prey; a committed rush faces where it is going.
    glm::vec3 facing = player_.worldPos - enemy.worldPos;
    if (enemy.def->lance.enabled && enemy.lanceState == Brotato3D::ELanceState::Dashing)
    {
        facing = enemy.lanceDashDir;
    }
    else if (!enemy.seesPlayer && enemy.moveSpeedNow > 0.2f && glm::length(enemy.navDir) > 0.5f)
    {
        // Rounding a corner: look where the feet go, not through the wall.
        facing = enemy.navDir;
    }
    if (glm::length(glm::vec2(facing.x, facing.z)) > 0.05f)
    {
        const float targetYaw = std::atan2(facing.x, facing.z);
        const float delta = std::remainder(targetYaw - enemy.yaw, glm::two_pi<float>());
        const float maxTurn = EnemyTurnRate * dt;
        enemy.yaw += std::clamp(delta, -maxTurn, maxTurn);
    }

    Brotato3D::EZombieTone tone = enemy.bossPhase2Active ? Brotato3D::EZombieTone::Enraged : Brotato3D::EZombieTone::Normal;
    const bool telegraphing =
        (enemy.def->mortar.enabled && enemy.mortarTelegraphRemainingMs > 0.0f) ||
        (enemy.def->lance.enabled && enemy.lanceState == Brotato3D::ELanceState::Telegraph) ||
        (enemy.def->charge.enabled && enemy.charging) ||
        (enemy.def->bomb.enabled && enemy.bombFuseMs >= 0.0f);
    if (enemy.hitFlashRemainingMs > 0.0f ||
        (enemy.def->lance.enabled && enemy.lanceState == Brotato3D::ELanceState::Dashing))
    {
        tone = Brotato3D::EZombieTone::Flash;
    }
    else if (telegraphing)
    {
        // Blink rather than hold solid red so the body stays readable.
        tone = std::fmod(runElapsedSec_ * 9.0f, 1.0f) < 0.55f ? Brotato3D::EZombieTone::Warning : tone;
    }
    enemy.visual->SetTone(tone);

    const float reach = enemy.radius + player_.radius + 0.9f;
    if (enemy.attackAnimCooldownMs <= 0.0f && DistanceXZ(enemy.worldPos, player_.worldPos) < reach)
    {
        PlayEnemyAction(enemy,
                        std::uniform_int_distribution<int>(0, 2)(rng_) == 0 ? Brotato3D::EZombieAction::Lunge :
                                                                               Brotato3D::EZombieAction::Swipe,
                        1.5f);
        enemy.attackAnimCooldownMs = AttackAnimIntervalMs;
    }

    enemy.visual->SetPlacement(EnemyFeet(enemy), enemy.yaw, enemy.pulseScale);
    enemy.visual->SetGait(enemy.crawling ? Brotato3D::EZombieGait::Crawl :
                              (enemy.moveSpeedNow > 0.08f ? Brotato3D::EZombieGait::Walk : Brotato3D::EZombieGait::Idle),
                          enemy.moveSpeedNow);
    enemy.visual->Update(dt);
}

void Brotato3DGameInstance::BeginEnemyCorpse(Brotato3D::FEnemyRuntime& enemy, bool gib)
{
    if (!enemy.visual || !enemy.def)
    {
        return;
    }
    const glm::vec3 hitDir = ResolveEnemyDebrisDir(enemy, enemy.worldPos - player_.worldPos);
    const glm::vec3 forward(std::sin(enemy.yaw), 0.0f, std::cos(enemy.yaw));
    const glm::vec3 chest = EnemyFeet(enemy) + glm::vec3(0.0f, 1.15f * enemy.def->rigScale, 0.0f);

    if (gib)
    {
        // Blown apart: every limb leaves in its own direction.
        for (size_t limb = 0; limb < Brotato3D::zombieLimbCount; ++limb)
        {
            const float angle = std::uniform_real_distribution<float>(0.0f, glm::two_pi<float>())(rng_);
            const glm::vec3 scatter = glm::normalize(hitDir * 1.2f + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)));
            SeverEnemyLimb(enemy, static_cast<Brotato3D::EZombieLimb>(limb), scatter,
                           std::uniform_real_distribution<float>(5.0f, 9.0f)(rng_));
        }
        SpawnBloodBurst(chest, hitDir, 14, 6.5f);
        SpawnBloodDecal(EnemyFeet(enemy), 1.3f * enemy.def->rigScale);
    }
    else
    {
        if (enemy.lastHitCrit)
        {
            // A critical kill takes the head clean off.
            SeverEnemyLimb(enemy, Brotato3D::EZombieLimb::Head, glm::normalize(hitDir + glm::vec3(0.0f, 0.4f, 0.0f)), 7.0f);
        }
        else if (enemy.def->canLoseLimbs && std::uniform_int_distribution<int>(0, 2)(rng_) == 0)
        {
            SeverRandomEnemyLimb(enemy, hitDir, false);
        }
        SpawnBloodBurst(chest, hitDir, 6, 5.0f);
        SpawnBloodDecal(EnemyFeet(enemy) + hitDir * 0.5f, 0.8f * enemy.def->rigScale);
    }

    Brotato3D::EZombieDeath death = glm::dot(hitDir, forward) < 0.0f ? Brotato3D::EZombieDeath::Back :
                                                                        Brotato3D::EZombieDeath::Forward;
    if (enemy.crawling || gib)
    {
        death = Brotato3D::EZombieDeath::Crumple;
    }
    enemy.visual->PlayDeath(death);
    enemy.visual->SetPlacement(EnemyFeet(enemy), enemy.yaw);
    enemy.visual->Update(0.0f);

    if (!enemy.fading)
    {
        ++corpseCount_;
    }
    enemy.fading = true;
    enemy.corpseMs = enemy.def->boss.enabled ? BossCorpseLingerMs :
                     (corpseCount_ > CrowdedCorpseCount ? CrowdedCorpseLingerMs : CorpseLingerMs);
}

void Brotato3DGameInstance::UpdateEnemyBehaviour(Brotato3D::FEnemyRuntime& enemy, double deltaSeconds, float deltaMs)
{
    enemy.contactCooldownMs = std::max(0.0f, enemy.contactCooldownMs - deltaMs);
    enemy.rangedFireCooldownMs = std::max(0.0f, enemy.rangedFireCooldownMs - deltaMs);
    enemy.chargeCooldownMs = std::max(0.0f, enemy.chargeCooldownMs - deltaMs);
    enemy.healIntervalMs = std::max(0.0f, enemy.healIntervalMs - deltaMs);
    enemy.hitFlashRemainingMs = std::max(0.0f, enemy.hitFlashRemainingMs - deltaMs);
    enemy.mortarFireCooldownMs = std::max(0.0f, enemy.mortarFireCooldownMs - deltaMs);
    enemy.lanceCooldownMs = std::max(0.0f, enemy.lanceCooldownMs - deltaMs);

    if (enemy.def->boss.enabled && !enemy.bossPhase2Active &&
        enemy.currentHp <= static_cast<int>(std::round(enemy.maxHp * enemy.def->boss.phase2HpRatio)))
    {
        enemy.bossPhase2Active = true;
        PlayEnemyAction(enemy, Brotato3D::EZombieAction::Alert, 1.0f);
        StartScreenShake(250.0f, 3.0f);
    }

    glm::vec3 toPlayer = player_.worldPos - enemy.worldPos;
    toPlayer.y = 0.0f;
    const float playerDistance = glm::length(toPlayer);

    if (enemy.def->bomb.enabled && enemy.bombFuseMs >= 0.0f)
    {
        enemy.bombFuseMs -= deltaMs;
        // The gut swells faster and faster until it goes.
        const float fuse01 = 1.0f - std::clamp(enemy.bombFuseMs / std::max(1.0f, enemy.def->bomb.fuseMs), 0.0f, 1.0f);
        enemy.pulseScale = 1.0f + 0.18f * fuse01 + std::sin(runElapsedSec_ * 28.0f) * 0.05f;
        if (enemy.bombFuseMs <= 0.0f)
        {
            PushExplosionRing(enemy.worldPos, glm::vec4(1.0f, 0.18f, 0.10f, 1.0f), enemy.def->bomb.explosionRadius);
            SpawnTempLight(enemy.worldPos, glm::vec3(1.0f, 0.25f, 0.08f), 5.0f, 250.0f);
            StartScreenShake(220.0f, 3.5f);
            if (DistanceXZ(enemy.worldPos, player_.worldPos) <= enemy.def->bomb.explosionRadius)
            {
                DamagePlayer(enemy.def->bomb.explosionDamage, 180.0f, 250.0f);
            }
            enemy.alive = false;
            enemy.pulseScale = 1.0f;
            DeactivateEnemyKinematicBody(enemy);
            BeginEnemyCorpse(enemy, true);
            return;
        }
        SyncEnemyKinematicBody(enemy, deltaSeconds);
        return;
    }
    enemy.pulseScale = 1.0f;

    if (enemy.def->heal.enabled && enemy.healIntervalMs <= 0.0f)
    {
        Brotato3D::FEnemyRuntime* healTarget = nullptr;
        float healTargetDistance = FLT_MAX;
        for (auto& candidate : enemies_)
        {
            if (&candidate == &enemy || !candidate.alive || candidate.currentHp >= candidate.maxHp)
            {
                continue;
            }
            const float distance = DistanceXZ(candidate.worldPos, enemy.worldPos);
            if (distance <= enemy.def->heal.radiusMeters && distance < healTargetDistance)
            {
                healTargetDistance = distance;
                healTarget = &candidate;
            }
        }
        if (healTarget)
        {
            healTarget->currentHp = std::min(healTarget->maxHp, healTarget->currentHp + enemy.def->heal.healAmount);
            PlayEnemyAction(enemy, Brotato3D::EZombieAction::Alert, 1.6f);
            PushFloatingText(healTarget->worldPos + glm::vec3(0.0f, 0.8f, 0.0f),
                             fmt::format("+{}", enemy.def->heal.healAmount),
                             glm::vec4(0.75f, 0.35f, 1.0f, 1.0f), 600.0f);
            PushLaserBeam(enemy.worldPos + glm::vec3(0.0f, enemy.def->size.y, 0.0f),
                          healTarget->worldPos + glm::vec3(0.0f, healTarget->def->size.y, 0.0f),
                          glm::vec4(0.75f, 0.25f, 1.0f, 1.0f), 150.0f, 0.12f);
        }
        enemy.healIntervalMs = enemy.def->heal.intervalMs;
    }

    bool customEnemyHandled = false;
    if (playerDistance > 0.001f)
    {
        const glm::vec3 toPlayerDir = toPlayer / playerDistance;
        if (enemy.def->mortar.enabled)
        {
            customEnemyHandled = UpdateMortarTank(enemy, deltaSeconds, deltaMs, toPlayerDir, playerDistance);
        }
        else if (enemy.def->lance.enabled)
        {
            customEnemyHandled = UpdateLanceCharger(enemy, deltaSeconds, deltaMs, toPlayerDir, playerDistance);
        }

        if (!customEnemyHandled)
        {
            glm::vec3 moveDir = ChaseDirection(enemy, toPlayerDir, deltaSeconds);
            float moveSpeed = enemy.def->moveSpeed;
            if (enemy.bossPhase2Active)
            {
                moveSpeed *= enemy.def->boss.phase2MoveSpeedMult;
            }
            if (enemy.def->bomb.enabled && playerDistance <= enemy.def->bomb.triggerDistance)
            {
                enemy.bombFuseMs = enemy.def->bomb.fuseMs;
                SyncEnemyKinematicBody(enemy, deltaSeconds);
                return;
            }
            if (enemy.def->charge.enabled && !enemy.crawling)
            {
                if (!enemy.charging && enemy.chargeCooldownMs <= 0.0f && playerDistance <= enemy.def->charge.triggerDistance &&
                    enemy.seesPlayer)
                {
                    enemy.charging = true;
                    enemy.chargeRampMs = enemy.def->charge.chargeRampSec * 1000.0f;
                    enemy.chargeCooldownMs = enemy.def->charge.cooldownMs;
                }
                if (enemy.charging)
                {
                    const float rampTotal = std::max(1.0f, enemy.def->charge.chargeRampSec * 1000.0f);
                    enemy.chargeRampMs -= deltaMs;
                    const float rampProgress = 1.0f - std::clamp(enemy.chargeRampMs / rampTotal, 0.0f, 1.0f);
                    moveSpeed *= glm::mix(1.0f, enemy.def->charge.chargeSpeedMult, rampProgress);
                    if (enemy.chargeRampMs < -800.0f)
                    {
                        enemy.charging = false;
                    }
                }
                else if (playerDistance > enemy.def->charge.triggerDistance)
                {
                    // Runners stalk at a walk until they are close enough to commit.
                    moveSpeed *= 0.55f;
                }
            }
            if (enemy.def->ranged.enabled)
            {
                const float preferredDistance = enemy.def->ranged.preferredDistance;
                if (playerDistance < preferredDistance - 0.5f)
                {
                    moveDir = -toPlayerDir;
                    moveSpeed = enemy.def->moveSpeed * 0.7f;
                }
                else if (playerDistance <= preferredDistance + 0.5f)
                {
                    moveSpeed = 0.0f;
                }

                if (enemy.rangedFireCooldownMs <= 0.0f &&
                    !IsSegmentBlocked(enemy.worldPos, player_.worldPos, enemy.def->ranged.size))
                {
                    SpawnEnemyProjectile(enemy, toPlayerDir);
                    PlayEnemyAction(enemy, Brotato3D::EZombieAction::Alert, 2.2f);
                    enemy.rangedFireCooldownMs = enemy.def->ranged.intervalMs;
                }
            }
            if (enemy.crawling)
            {
                moveSpeed *= CrawlSpeedScale;
            }
            // Arrived: claw at the player instead of walking into them. Without this the whole
            // horde compresses onto one point faster than it can shoulder itself apart.
            if (enemy.seesPlayer && moveDir == enemy.navDir &&
                playerDistance < (enemy.radius + player_.radius) * ContactStandOff && !enemy.charging)
            {
                moveSpeed = 0.0f;
            }

            if (moveSpeed > 0.0f)
            {
                enemy.worldPos = ResolveEnemyGroundedPosition(enemy,
                                                              enemy.worldPos + moveDir * moveSpeed * static_cast<float>(deltaSeconds));
            }
        }
    }

    if (DistanceXZ(enemy.worldPos, player_.worldPos) < enemy.radius + player_.radius && enemy.contactCooldownMs <= 0.0f)
    {
        int contactDamage = enemy.def->contactDamage;
        if (enemy.def->lance.enabled && enemy.lanceState == Brotato3D::ELanceState::Dashing)
        {
            contactDamage = static_cast<int>(std::round(contactDamage * enemy.def->lance.dashContactDamageMult));
            enemy.lanceState = Brotato3D::ELanceState::Recovering;
            enemy.lanceStateMs = enemy.def->lance.recoverMs;
            enemy.lanceDashRemainingDist = 0.0f;
            StartScreenShake(180.0f, 3.0f);
        }
        if (enemy.def->charge.enabled && enemy.charging)
        {
            contactDamage = static_cast<int>(std::round(contactDamage * enemy.def->charge.contactDamageMult));
            enemy.charging = false;
        }
        if (enemy.bossPhase2Active)
        {
            contactDamage = static_cast<int>(std::round(contactDamage * enemy.def->boss.phase2ContactDamageMult));
        }
        contactDamage = static_cast<int>(std::round(static_cast<float>(contactDamage) * Brotato3D::MasterDifficulty));
        enemy.contactCooldownMs = 600.0f;
        PlayEnemyAction(enemy, Brotato3D::EZombieAction::Swipe, 1.8f);
        enemy.attackAnimCooldownMs = AttackAnimIntervalMs;
        if (enemy.def->name == "Brute")
        {
            hitStopMs_ = 80.0f;
            appState_ = Brotato3D::EAppState::Hitstop;
            SetWorldPhysicsPaused(true);
        }
        DamagePlayer(contactDamage, 150.0f, 180.0f);
    }
    SyncEnemyKinematicBody(enemy, deltaSeconds);
}

bool Brotato3DGameInstance::UpdateMortarTank(Brotato3D::FEnemyRuntime& enemy,
                                             double deltaSeconds,
                                             float deltaMs,
                                             const glm::vec3& toPlayerDir,
                                             float playerDistance)
{
    if (!enemy.def || !enemy.def->mortar.enabled)
    {
        return false;
    }

    const Brotato3D::FMortarDef& mortar = enemy.def->mortar;
    if (enemy.mortarTelegraphRemainingMs > 0.0f)
    {
        enemy.mortarTelegraphRemainingMs -= deltaMs;
        if (enemy.mortarTelegraphRemainingMs <= 0.0f)
        {
            CancelGroundIndicatorsForEnemy(enemy.runtimeTag);
            PushExplosionRing(enemy.mortarTargetPos, glm::vec4(1.0f, 0.30f, 0.10f, 1.0f), mortar.explosionRadius);
            SpawnTempLight(enemy.mortarTargetPos, glm::vec3(1.0f, 0.45f, 0.10f), 5.0f, 250.0f);
            StartScreenShake(180.0f, 3.0f);
            if (DistanceXZ(enemy.mortarTargetPos, player_.worldPos) <= mortar.explosionRadius)
            {
                DamagePlayer(static_cast<int>(std::round(static_cast<float>(mortar.explosionDamage) * Brotato3D::MasterDifficulty)),
                             180.0f,
                             250.0f);
            }
            enemy.mortarFireCooldownMs = mortar.fireIntervalMs;
            enemy.mortarTelegraphRemainingMs = 0.0f;
        }
        SyncEnemyKinematicBody(enemy, deltaSeconds);
        return true;
    }

    if (enemy.mortarFireCooldownMs <= 0.0f &&
        playerDistance >= mortar.throwRangeMin &&
        playerDistance <= mortar.throwRangeMax)
    {
        glm::vec3 target = player_.worldPos + playerVelocity_ * mortar.leadFactor;
        target = ClampToArena(target, 0.0f, arenaHalfExtent_);
        target.y = 0.05f;
        enemy.mortarTargetPos = target;
        enemy.mortarTelegraphRemainingMs = std::max(1.0f, mortar.telegraphMs);
        PlayEnemyAction(enemy, Brotato3D::EZombieAction::Alert, 0.8f / std::max(0.3f, mortar.telegraphMs / 1000.0f));

        Brotato3D::FGroundIndicator indicator{};
        indicator.shape = Brotato3D::EGroundIndicatorShape::Circle;
        indicator.worldPos = target;
        indicator.radius = mortar.explosionRadius;
        indicator.color = glm::vec4(1.0f, 0.20f, 0.08f, 0.9f);
        indicator.totalMs = enemy.mortarTelegraphRemainingMs;
        indicator.remainingMs = enemy.mortarTelegraphRemainingMs;
        indicator.enemyTag = enemy.runtimeTag;
        PushGroundIndicator(indicator);
        SyncEnemyKinematicBody(enemy, deltaSeconds);
        return true;
    }

    glm::vec3 moveDir = ChaseDirection(enemy, toPlayerDir, deltaSeconds);
    float moveSpeed = 0.0f;
    if (playerDistance < mortar.throwRangeMin)
    {
        moveDir = -toPlayerDir;
        moveSpeed = enemy.def->moveSpeed;
    }
    else if (playerDistance > mortar.throwRangeMax)
    {
        moveSpeed = enemy.def->moveSpeed;
    }

    if (moveSpeed > 0.0f)
    {
        enemy.worldPos = ResolveEnemyGroundedPosition(enemy, enemy.worldPos + moveDir * moveSpeed * static_cast<float>(deltaSeconds));
    }
    return true;
}

bool Brotato3DGameInstance::UpdateLanceCharger(Brotato3D::FEnemyRuntime& enemy,
                                               double deltaSeconds,
                                               float deltaMs,
                                               const glm::vec3& toPlayerDir,
                                               float playerDistance)
{
    if (!enemy.def || !enemy.def->lance.enabled)
    {
        return false;
    }

    const Brotato3D::FLanceDef& lance = enemy.def->lance;
    switch (enemy.lanceState)
    {
    case Brotato3D::ELanceState::Idle:
        if (enemy.lanceCooldownMs <= 0.0f && playerDistance <= lance.windupRangeMin &&
            !IsSegmentBlocked(enemy.worldPos, player_.worldPos, enemy.radius))
        {
            enemy.lanceState = Brotato3D::ELanceState::Telegraph;
            enemy.lanceStateMs = std::max(1.0f, lance.telegraphMs);
            enemy.lanceDashDir = toPlayerDir;
            PlayEnemyAction(enemy, Brotato3D::EZombieAction::Alert, 0.8f / std::max(0.3f, lance.telegraphMs / 1000.0f));
            enemy.lanceDashRemainingDist = lance.dashDistanceMax;

            Brotato3D::FGroundIndicator indicator{};
            indicator.shape = Brotato3D::EGroundIndicatorShape::Strip;
            indicator.worldPos = enemy.worldPos + glm::vec3(0.0f, 0.05f, 0.0f);
            indicator.endPos = ClampToArena(enemy.worldPos + enemy.lanceDashDir * lance.dashDistanceMax,
                                            enemy.radius,
                                            arenaHalfExtent_);
            indicator.endPos = ResolveExtractionVehicleCollision(indicator.endPos, enemy.radius);
            indicator.endPos = ClampToArena(indicator.endPos, enemy.radius, arenaHalfExtent_);
            indicator.endPos.y = 0.05f;
            indicator.width = 0.6f;
            indicator.color = glm::vec4(1.0f, 0.18f, 0.08f, 0.85f);
            indicator.totalMs = enemy.lanceStateMs;
            indicator.remainingMs = enemy.lanceStateMs;
            indicator.enemyTag = enemy.runtimeTag;
            PushGroundIndicator(indicator);
            return true;
        }
        enemy.worldPos = ResolveEnemyGroundedPosition(
            enemy, enemy.worldPos + ChaseDirection(enemy, toPlayerDir, deltaSeconds) * enemy.def->moveSpeed *
                                        static_cast<float>(deltaSeconds));
        return true;

    case Brotato3D::ELanceState::Telegraph:
        enemy.lanceStateMs -= deltaMs;
        if (enemy.lanceStateMs <= 0.0f)
        {
            CancelGroundIndicatorsForEnemy(enemy.runtimeTag);
            enemy.lanceState = Brotato3D::ELanceState::Dashing;
            enemy.lanceDashStartPos = enemy.worldPos;
            enemy.lanceDashRemainingDist = lance.dashDistanceMax;
            Brotato3D::PlayWeaponFireSfx("shotgun");
        }
        return true;

    case Brotato3D::ELanceState::Dashing:
    {
        const float dashStep = std::min(lance.dashSpeed * static_cast<float>(deltaSeconds), enemy.lanceDashRemainingDist);
        const glm::vec3 before = enemy.worldPos;
        const glm::vec3 intendedPos = enemy.worldPos + enemy.lanceDashDir * dashStep;
        enemy.worldPos = ResolveEnemyGroundedPosition(enemy, intendedPos);
        enemy.lanceDashRemainingDist -= glm::length(glm::vec2(enemy.worldPos.x - before.x, enemy.worldPos.z - before.z));
        const bool hitWall = DistanceXZ(intendedPos, enemy.worldPos) > 0.001f;
        if (enemy.lanceDashRemainingDist <= 0.001f || hitWall)
        {
            enemy.lanceState = Brotato3D::ELanceState::Recovering;
            enemy.lanceStateMs = lance.recoverMs;
            enemy.lanceDashRemainingDist = 0.0f;
        }
        return true;
    }

    case Brotato3D::ELanceState::Recovering:
        enemy.lanceStateMs -= deltaMs;
        if (enemy.lanceStateMs <= 0.0f)
        {
            enemy.lanceState = Brotato3D::ELanceState::Idle;
            enemy.lanceCooldownMs = lance.cooldownMs;
            enemy.lanceStateMs = 0.0f;
        }
        return true;
    }

    return true;
}

void Brotato3DGameInstance::KillEnemy(Brotato3D::FEnemyRuntime& enemy, bool dropLoot)
{
    if (!enemy.alive)
    {
        return;
    }
    CancelGroundIndicatorsForEnemy(enemy.runtimeTag);
    enemy.mortarTelegraphRemainingMs = 0.0f;
    enemy.lanceState = Brotato3D::ELanceState::Idle;
    enemy.alive = false;
    DeactivateEnemyKinematicBody(enemy);
    enemy.pulseScale = 1.0f;
    if (dropLoot)
    {
        ++killCount_;
        Brotato3D::PlayEnemyDeathSfx(enemy.def ? enemy.def->name : std::string{});
        if (!enemy.def || !enemy.def->boss.enabled)
        {
            SpawnKillMaterialDebris(enemy);
        }
        ProcessOnKillTriggers(enemy.worldPos);
    }
    if (enemy.def && enemy.def->boss.enabled)
    {
        bossVictoryDelayMs_ = std::max(bossVictoryDelayMs_, 1200.0f);
        spdlog::info("[Brotato3D] [boss defeated]");
    }
    if (dropLoot && enemy.def && enemy.def->boss.enabled)
    {
        const glm::vec3 deathSprayDir = ResolveEnemyDebrisDir(enemy, enemy.worldPos - player_.worldPos);
        std::uniform_real_distribution<float> unitDist(-1.0f, 1.0f);
        std::uniform_real_distribution<float> chunkSpeedDist(5.0f, 9.0f);
        std::uniform_real_distribution<float> bossSpeedDist(6.0f, 10.0f);
        for (int index = 0; index < 50; ++index)
        {
            glm::vec3 dir = deathSprayDir * 2.2f +
                            glm::vec3(unitDist(rng_) * 0.45f,
                                      std::uniform_real_distribution<float>(0.35f, 0.95f)(rng_),
                                      unitDist(rng_) * 0.45f);
            if (glm::length(dir) < 0.001f)
            {
                dir = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            dir = glm::normalize(dir);
            const uint32_t matId = (index % 4 == 0) ? enemy.darkMaterialId : enemy.materialId;
            SpawnDebris(Brotato3D::EDebrisKind::Chunk, enemy.worldPos + dir * 0.2f, dir, chunkSpeedDist(rng_), matId, 1, 0.0f);
        }
        for (int index = 0; index < 8; ++index)
        {
            glm::vec3 dir = deathSprayDir * 2.5f +
                            glm::vec3(unitDist(rng_) * 0.4f,
                                      std::uniform_real_distribution<float>(0.4f, 1.0f)(rng_),
                                      unitDist(rng_) * 0.4f);
            if (glm::length(dir) < 0.001f)
            {
                dir = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            dir = glm::normalize(dir);
            SpawnDebris(Brotato3D::EDebrisKind::BossChunk, enemy.worldPos + dir * 0.4f, dir, bossSpeedDist(rng_),
                        enemy.materialId, 1, 0.0f);
        }
        SpawnKillMaterialDebris(enemy, 3, 1.0f, 12);
        StartScreenShake(800.0f, 5.0f);
        explosionRings_.push_back({enemy.worldPos, glm::vec4(1.0f, 0.72f, 0.18f, 1.0f), 800.0f, 800.0f, 4.0f});
        explosionRings_.push_back({enemy.worldPos, glm::vec4(1.0f, 0.92f, 0.40f, 1.0f), 1200.0f, 1200.0f, 8.0f});
        SpawnTempLight(enemy.worldPos, glm::vec3(1.0f, 0.85f, 0.40f), 12.0f, 800.0f);
        bossKillFlashMs_ = 100.0f;
        timeScaleRecoveryMs_ = 1200.0f;
    }
    // One-shot kills, blasts and bosses come apart; everything else falls where it stood.
    const bool boss = enemy.def && enemy.def->boss.enabled;
    const bool overkill = enemy.lastHitDamage >= std::max(14, static_cast<int>(static_cast<float>(enemy.maxHp) * 0.75f));
    BeginEnemyCorpse(enemy, boss || enemy.lastHitExplosive || overkill);
}

void Brotato3DGameInstance::ClearAliveEnemies(bool dropLoot)
{
    for (auto& enemy : enemies_)
    {
        if (enemy.alive)
        {
            KillEnemy(enemy, dropLoot);
        }
    }
}

void Brotato3DGameInstance::SpawnKillMaterialDebris(const Brotato3D::FEnemyRuntime& enemy,
                                                    int countMultiplier,
                                                    float spawnRadius,
                                                    int minCount)
{
    if (!enemy.def)
    {
        return;
    }

    const int count = std::max(minCount, enemy.def->materialDrop * std::max(1, countMultiplier));
    if (count <= 0)
    {
        return;
    }

    const glm::vec3 deathSprayDir = ResolveEnemyDebrisDir(enemy, enemy.worldPos - player_.worldPos);
    std::uniform_real_distribution<float> speedDist(4.5f, 7.0f);
    for (int index = 0; index < count; ++index)
    {
        const float angle = static_cast<float>(index) / static_cast<float>(count) * glm::two_pi<float>();
        const glm::vec3 radial(std::cos(angle), 0.0f, std::sin(angle));
        const glm::vec3 spawnPos = enemy.worldPos + radial * spawnRadius + glm::vec3(0.0f, enemy.def->size.y * 0.45f, 0.0f);
        glm::vec3 dir = deathSprayDir * 1.8f + radial * 0.45f + glm::vec3(0.0f, 0.6f, 0.0f);
        if (glm::length(dir) < 0.001f)
        {
            dir = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        dir = glm::normalize(dir);
        SpawnDebris(Brotato3D::EDebrisKind::Chunk,
                    spawnPos,
                    dir,
                    speedDist(rng_),
                    materialDebrisMatId_,
                    1,
                    0.0f,
                    Brotato3D::EDebrisPayload::Material,
                    1);
    }
}


