#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

// ============================================================================
// Dismemberment and blood.
//
// Limbs come off for three reasons: HP thresholds (a third of the health bar
// costs a limb, so a tough zombie is visibly worn down), critical kills (the
// head), and overkill / blasts (everything). A lost leg changes the fight: the
// zombie drops to a crawl - slower, lower, still dangerous - instead of dying.
// The limb itself is thrown by FZombieRigVisual; this file decides which one
// and paints the ground.
// ============================================================================

using namespace Brotato3DUtil;

namespace
{
    constexpr int BloodDecalCount = 192;
    constexpr float BloodDecalBaseY = 0.045f;
    constexpr float BloodLifetimeMs = 650.0f;
    constexpr int MaxBloodChunksPerBurst = 16;

    using Brotato3D::EZombieLimb;

    bool IsLeg(EZombieLimb limb)
    {
        return limb == EZombieLimb::LegL || limb == EZombieLimb::LegR || limb == EZombieLimb::CalfL ||
               limb == EZombieLimb::CalfR;
    }
}

void Brotato3DGameInstance::BuildGorePool(std::vector<Assets::Model>& models,
                                          std::vector<Assets::FMaterial>& materials,
                                          std::vector<std::shared_ptr<Assets::Node>>& nodes)
{
    // Saturated enough to stay red under the dusk sky; a near-black tone reads as an oil stain.
    bloodMatId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.55f, 0.035f, 0.035f));
    bloodDarkMatId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.34f, 0.02f, 0.025f));

    // A flat eight-sided puddle; each decal gets its own yaw and stretch when placed.
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.5f, 0.0f, -0.5f), glm::vec3(0.5f, 0.006f, 0.5f)));
    const uint32_t decalModelId = static_cast<uint32_t>(models.size() - 1);

    bloodDecals_.clear();
    bloodDecals_.reserve(BloodDecalCount);
    nextBloodDecal_ = 0;
    for (int index = 0; index < BloodDecalCount; ++index)
    {
        auto node = Assets::SceneBuilder::CreateRenderNode(fmt::format("Brotato3D_Blood_{}", index),
                                                           HiddenPosition,
                                                           glm::vec3(1.0f),
                                                           static_cast<uint32_t>(nodes.size()),
                                                           decalModelId,
                                                           index % 3 == 0 ? bloodMatId_ : bloodDarkMatId_,
                                                           false);
        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetCastShadows(false);
            render->SetRayCastVisible(false);
        }
        nodes.push_back(node);
        bloodDecals_.push_back({node});
    }
}

void Brotato3DGameInstance::ClearGore()
{
    for (FBloodDecal& decal : bloodDecals_)
    {
        if (decal.node)
        {
            decal.node->SetTranslation(HiddenPosition);
            Assets::NodeUtils::SetVisible(decal.node, false);
        }
    }
    nextBloodDecal_ = 0;
}

void Brotato3DGameInstance::SpawnBloodDecal(const glm::vec3& worldPos, float radius)
{
    if (bloodDecals_.empty() || !Brotato3D::GoreEnabled)
    {
        return;
    }
    // A puddle plus two smaller splashes thrown off it; the ring buffer recycles the oldest.
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    for (int splash = 0; splash < 3; ++splash)
    {
        const size_t slot = nextBloodDecal_;
        nextBloodDecal_ = (nextBloodDecal_ + 1) % bloodDecals_.size();
        FBloodDecal& decal = bloodDecals_[slot];
        if (!decal.node)
        {
            continue;
        }
        const float size = splash == 0 ? radius : radius * (0.25f + 0.25f * std::abs(unit(rng_)));
        glm::vec3 pos = worldPos;
        if (splash > 0)
        {
            pos += glm::vec3(unit(rng_), 0.0f, unit(rng_)) * radius * 0.9f;
        }
        pos = ClampToArena(pos, 0.0f, arenaHalfExtent_);
        // Stagger the heights so overlapping puddles never z-fight.
        pos.y = BloodDecalBaseY + static_cast<float>(slot) * 0.00012f;
        decal.node->SetTransform(pos,
                                 glm::angleAxis(unit(rng_) * glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)),
                                 glm::vec3(size * (0.8f + 0.5f * std::abs(unit(rng_))), 1.0f,
                                           size * (0.8f + 0.5f * std::abs(unit(rng_)))));
        Assets::NodeUtils::SetVisible(decal.node, true);
    }
}

void Brotato3DGameInstance::SpawnBloodBurst(const glm::vec3& worldPos, const glm::vec3& direction, int count, float speed)
{
    if (count <= 0 || bloodMatId_ == 0 || !Brotato3D::GoreEnabled)
    {
        return;
    }
    glm::vec3 dir(direction.x, 0.35f, direction.z);
    dir = glm::length(dir) > 0.001f ? glm::normalize(dir) : glm::vec3(0.0f, 1.0f, 0.0f);
    SpawnDebris(Brotato3D::EDebrisKind::Tiny,
                glm::vec3(worldPos.x, std::max(0.25f, worldPos.y), worldPos.z),
                dir,
                speed,
                bloodMatId_,
                std::min(count, MaxBloodChunksPerBurst),
                0.75f,
                Brotato3D::EDebrisPayload::None,
                0,
                BloodLifetimeMs);
}

bool Brotato3DGameInstance::SeverEnemyLimb(Brotato3D::FEnemyRuntime& enemy,
                                           EZombieLimb limb,
                                           const glm::vec3& direction,
                                           float force)
{
    if (!enemy.visual || !enemy.def || !Brotato3D::GoreEnabled)
    {
        return false;
    }
    glm::vec3 flat(direction.x, 0.0f, direction.z);
    flat = glm::length(flat) > 0.001f ? glm::normalize(flat) : glm::vec3(0.0f, 0.0f, 1.0f);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    const glm::vec3 side(flat.z, 0.0f, -flat.x);
    const glm::vec3 velocity = flat * force + side * (unit(rng_) * force * 0.35f) +
                               glm::vec3(0.0f, 3.2f + 2.4f * std::abs(unit(rng_)), 0.0f);
    glm::vec3 wound(0.0f);
    if (!enemy.visual->Sever(limb, velocity, rng_, &wound))
    {
        return false;
    }
    SpawnBloodBurst(wound, flat, 5, 4.5f);
    SpawnBloodDecal(glm::vec3(wound.x, 0.0f, wound.z) + flat * 0.6f, 0.45f * enemy.def->rigScale);
    if (IsLeg(limb) && enemy.alive && enemy.def->canCrawl && !enemy.crawling)
    {
        enemy.crawling = true;
        enemy.charging = false;
    }
    return true;
}

bool Brotato3DGameInstance::SeverRandomEnemyLimb(Brotato3D::FEnemyRuntime& enemy, const glm::vec3& direction, bool preferLeg)
{
    if (!enemy.visual || !enemy.def)
    {
        return false;
    }
    static constexpr std::array<EZombieLimb, 4> arms = {EZombieLimb::ForearmL, EZombieLimb::ForearmR, EZombieLimb::ArmL,
                                                        EZombieLimb::ArmR};
    static constexpr std::array<EZombieLimb, 4> legs = {EZombieLimb::CalfL, EZombieLimb::CalfR, EZombieLimb::LegL,
                                                        EZombieLimb::LegR};
    std::vector<EZombieLimb> candidates;
    const auto collect = [&](const std::array<EZombieLimb, 4>& group)
    {
        for (const EZombieLimb limb : group)
        {
            if (enemy.visual->HasLimb(limb))
            {
                candidates.push_back(limb);
            }
        }
    };
    // A zombie that cannot crawl keeps its legs while it lives: a biped on one leg reads as a bug.
    const bool legsAllowed = !enemy.alive || enemy.def->canCrawl;
    if (preferLeg && legsAllowed && !enemy.crawling)
    {
        collect(legs);
    }
    if (candidates.empty())
    {
        collect(arms);
    }
    if (candidates.empty() && legsAllowed)
    {
        collect(legs);
    }
    if (candidates.empty())
    {
        return false;
    }
    const EZombieLimb limb = candidates[std::uniform_int_distribution<size_t>(0, candidates.size() - 1)(rng_)];
    return SeverEnemyLimb(enemy, limb, direction, std::uniform_real_distribution<float>(3.0f, 5.5f)(rng_));
}

void Brotato3DGameInstance::ReactEnemyToDamage(Brotato3D::FEnemyRuntime& enemy, int damage, bool isCrit)
{
    enemy.lastHitDamage = damage;
    enemy.lastHitCrit = isCrit;
    if (!enemy.visual || !enemy.def || damage <= 0)
    {
        return;
    }

    glm::vec3 dir(enemy.lastHitDebrisDir.x, 0.0f, enemy.lastHitDebrisDir.z);
    if (glm::length(dir) < 0.001f)
    {
        dir = enemy.worldPos - player_.worldPos;
        dir.y = 0.0f;
    }
    dir = glm::length(dir) > 0.001f ? glm::normalize(dir) : glm::vec3(0.0f, 0.0f, 1.0f);

    // Heavier bodies rock less; every hit still lands visibly.
    const float kick = std::clamp(5.0f + static_cast<float>(damage) * 0.45f, 5.0f, 20.0f) / std::max(1.0f, enemy.def->rigScale);
    enemy.visual->AddHit(dir, kick);
    const glm::vec3 chest(enemy.worldPos.x, 1.1f * enemy.def->rigScale, enemy.worldPos.z);
    SpawnBloodBurst(chest, dir, isCrit ? 4 : 2, 4.0f);

    if (enemy.currentHp <= 0 || enemy.maxHp <= 0)
    {
        return;
    }
    // A heavy hit (a big share of the health bar, or a crit) knocks it off its stride.
    if ((isCrit || damage * 4 >= enemy.maxHp) && !enemy.def->boss.enabled)
    {
        PlayEnemyAction(enemy, Brotato3D::EZombieAction::Stagger, 1.5f);
    }
    if (!enemy.def->canLoseLimbs)
    {
        return;
    }

    // Each third of the health bar costs a limb: first an arm, then (if it can crawl) a leg.
    const float hpRatio = static_cast<float>(enemy.currentHp) / static_cast<float>(enemy.maxHp);
    const int targetStage = hpRatio < 0.34f ? 2 : (hpRatio < 0.67f ? 1 : 0);
    while (enemy.limbStage < targetStage)
    {
        ++enemy.limbStage;
        SeverRandomEnemyLimb(enemy, dir, enemy.limbStage >= 2);
    }
}
