#include "Engine/Common/CoreMinimal.hpp"
#include "WeaponSystem.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Player/PlayerController.hpp"

namespace NextDayz
{
    namespace
    {
        constexpr float kHitscanRange = 400.0f;
        const std::string kEmptyId;

        struct FMeleeViewPose
        {
            glm::vec3 offset{0.0f};
            glm::vec3 euler{0.0f}; // degrees
        };

        FMeleeViewPose MeleeReadyPose(const FWeaponDef& def)
        {
            return {{0.26f, -0.22f, 0.48f},
                    {def.id == "knife" ? -15.0f : -52.0f, -12.0f, -12.0f}};
        }

        // The same contact times drive first-person motion, TPS clips and damage.
        FMeleeViewPose SampleMeleeSwing(const FWeaponDef& def, float time)
        {
            const FMeleeViewPose ready = MeleeReadyPose(def);
            const bool knife = def.id == "knife";
            const bool axe = def.id == "axe";
            const std::array<FMeleeViewPose, 5> poses = {{
                ready,
                {{knife ? 0.34f : 0.40f, axe ? -0.02f : -0.16f, 0.38f},
                 {knife ? -10.0f : -105.0f, knife ? 12.0f : 45.0f, -28.0f}},
                {{knife ? 0.03f : -0.03f, axe ? -0.24f : -0.18f, knife ? 0.78f : 0.60f},
                 {axe ? 18.0f : -8.0f, knife ? -5.0f : -38.0f, -12.0f}},
                {{knife ? 0.01f : -0.20f, axe ? -0.40f : -0.25f, 0.52f},
                 {axe ? 58.0f : 10.0f, knife ? -8.0f : -65.0f, axe ? 18.0f : 22.0f}},
                ready,
            }};
            const std::array<float, 5> times = {0.0f, def.meleeStrikeTime01 * 0.60f,
                                                def.meleeStrikeTime01, def.meleeStrikeTime01 + 0.16f, 1.0f};
            for (size_t key = 1; key < poses.size(); ++key)
            {
                if (time <= times[key])
                {
                    const float t = glm::smoothstep(times[key - 1], times[key], time);
                    return {glm::mix(poses[key - 1].offset, poses[key].offset, t),
                            glm::mix(poses[key - 1].euler, poses[key].euler, t)};
                }
            }
            return ready;
        }
    }

    void WeaponSystem::SetViewModelAssets(const std::array<uint32_t, kWeapons.size()>& modelIds,
                                          const std::array<FWeaponMaterialSlots, kWeapons.size()>& materialIds)
    {
        viewModelModelIds_ = modelIds;
        viewModelMaterialIds_ = materialIds;
        viewModelAssetsSet_ = true;
    }

    void WeaponSystem::OnSceneLoaded(NextEngine& engine)
    {
        slots_ = {};
        activeSlot_ = 0;
        previousSlot_ = 1;
        reloading_ = false;
        switching_ = false;
        shotSequence_ = 0;
        CancelMelee();
        viewModelOffset_ = config_.ViewModelHipOffset;
        viewModelMeleeEuler_ = glm::vec3(0.0f);
        viewModelRecoil_ = 0.0f;
        viewModelRecoilVelocity_ = 0.0f;
        swayAngle_ = glm::vec2(0.0f);
        swayVelocity_ = glm::vec2(0.0f);
        moveVelocity_ = glm::vec2(0.0f);
        landingOffset_ = 0.0f;
        landingVelocity_ = 0.0f;
        aimAlpha_ = 0.0f;
        viewModelVisible_ = false;
        viewModelInScene_ = false;
        viewModelWeaponIndex_ = -1;
        shotEvents_.clear();
        hitEvents_.clear();
        traceEvents_.clear();
        if (!viewModelAssetsSet_)
        {
            return;
        }
        Assets::Scene& scene = engine.GetScene();
        // rayCastVisible = false so the player's own shots never hit the view model.
        viewModelNode_ = Assets::SceneBuilder::CreateRenderNode(
            "nd_viewmodel", glm::vec3(0.0f, -1000.0f, 0.0f), glm::vec3(1.0f), scene.GenerateInstanceId(),
            viewModelModelIds_[0], viewModelMaterialIds_[0], false,
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
        auto physics = std::make_shared<Runtime::PhysicsComponent>();
        physics->SetMobility(Runtime::ENodeMobility::Dynamic);
        viewModelNode_->AddComponent(physics);
        scene.AddNode(viewModelNode_);
        viewModelInScene_ = true;
        scene.MarkDirty();
    }

    void WeaponSystem::OnSceneUnloaded()
    {
        CancelMelee();
        viewModelNode_.reset();
        viewModelVisible_ = false;
        viewModelInScene_ = false;
        viewModelWeaponIndex_ = -1;
        shotEvents_.clear();
        hitEvents_.clear();
        traceEvents_.clear();
    }

    void WeaponSystem::ResetRuntime()
    {
        slots_ = {};
        activeSlot_ = 0;
        previousSlot_ = 1;
        triggerDown_ = false;
        triggerPressed_ = false;
        presentationSuppressed_ = false;
        triggerConsumed_ = false;
        fireCooldown_ = 0.0f;
        reloading_ = false;
        reloadTimer_ = 0.0f;
        switching_ = false;
        switchCommitted_ = false;
        switchTargetSlot_ = -1;
        switchTimer_ = 0.0f;
        CancelMelee();
        shotSequence_ = 0;
        shotEvents_.clear();
        hitEvents_.clear();
        traceEvents_.clear();
        RefreshViewModelVisibility();
    }

    bool WeaponSystem::EquipInstance(int slot, const Inventory& inventory, FItemInstanceId instanceId)
    {
        const FItemInstance* instance = inventory.FindInstance(instanceId);
        const FItemDef* item = instance ? FindItemDef(instance->defId) : nullptr;
        if (!item || !IsWieldable(*item) || !Equip(slot, std::string(item->weaponDefId)))
        {
            return false;
        }
        for (size_t other = 0; other < slots_.size(); ++other)
        {
            if (static_cast<int>(other) != slot && slots_[other].instanceId == instanceId)
            {
                slots_[other] = {};
            }
        }
        slots_[slot].instanceId = instanceId;
        const FWeaponDef* def = FindWeaponDef(item->weaponDefId);
        slots_[slot].ammoInMag = def ? std::clamp(instance->loadedAmmo, 0, def->magSize) : 0;
        RefreshViewModelVisibility();
        return true;
    }

    void WeaponSystem::Unequip(int slot)
    {
        if (slot < 0 || slot >= static_cast<int>(slots_.size()))
        {
            return;
        }
        slots_[slot] = {};
        if (slot == activeSlot_)
        {
            reloading_ = false;
            CancelMelee();
        }
        RefreshViewModelVisibility();
    }

    FItemInstanceId WeaponSystem::SlotInstance(int slot) const
    {
        return slot >= 0 && slot < static_cast<int>(slots_.size()) ? slots_[slot].instanceId : 0;
    }

    void WeaponSystem::SyncInventory(Inventory& inventory)
    {
        for (size_t slot = 0; slot < slots_.size(); ++slot)
        {
            FSlot& current = slots_[slot];
            if (current.instanceId == 0)
            {
                continue;
            }
            if (!inventory.FindInstance(current.instanceId))
            {
                Unequip(static_cast<int>(slot));
                continue;
            }
            inventory.SetLoadedAmmo(current.instanceId, current.ammoInMag);
        }
    }

    void WeaponSystem::CopyMagazinesTo(Inventory& inventory) const
    {
        for (const auto& slot : slots_)
            if (slot.instanceId) inventory.SetLoadedAmmo(slot.instanceId, slot.ammoInMag);
    }

    bool WeaponSystem::Equip(int slot, const std::string& weaponId)
    {
        if (slot < 0 || slot >= static_cast<int>(slots_.size()))
        {
            return false;
        }
        const FWeaponDef* def = FindWeaponDef(weaponId);
        if (!def)
        {
            return false;
        }
        previousSlot_ = activeSlot_;
        slots_[slot].weaponId = weaponId;
        slots_[slot].ammoInMag = def->magSize; // fresh magazine on equip
        slots_[slot].instanceId = 0;
        CancelMelee();
        activeSlot_ = slot;
        reloading_ = false;
        reloadTimer_ = 0.0f;
        switching_ = false;
        switchCommitted_ = false;
        switchTargetSlot_ = -1;
        switchTimer_ = 0.0f;
        viewModelMeleeEuler_ = glm::vec3(0.0f);
        if (def->IsMelee())
        {
            viewModelRecoil_ = 0.0f;
            viewModelRecoilVelocity_ = 0.0f;
        }
        RefreshViewModelVisibility();
        return true;
    }

    void WeaponSystem::SwitchSlot(int slot)
    {
        if (slot < 0 || slot >= static_cast<int>(slots_.size()) || slot == activeSlot_ || switching_)
        {
            return;
        }
        reloading_ = false;
        reloadTimer_ = 0.0f;
        CancelMelee();
        switching_ = true;
        switchCommitted_ = false;
        switchTargetSlot_ = slot;
        switchTimer_ = std::max(config_.SwitchSeconds, 0.01f);
    }

    void WeaponSystem::SwitchPrevious()
    {
        SwitchSlot(previousSlot_);
    }

    void WeaponSystem::RequestReload(Inventory& inventory)
    {
        const FWeaponDef* def = ActiveWeapon();
        if (!def || def->IsMelee() || reloading_ || switching_)
        {
            return;
        }
        if (slots_[activeSlot_].ammoInMag >= def->magSize)
        {
            return;
        }
        if (inventory.CountOf(std::string(AmmoItemId(def->ammo))) <= 0)
        {
            return;
        }
        reloading_ = true;
        reloadTimer_ = std::max(config_.ReloadSeconds, 0.01f);
    }

    const FWeaponDef* WeaponSystem::ActiveWeapon() const
    {
        return FindWeaponDef(slots_[activeSlot_].weaponId);
    }

    std::string WeaponSystem::ActiveWeaponId() const
    {
        return slots_[activeSlot_].weaponId;
    }

    std::string WeaponSystem::ActiveDisplayName() const
    {
        const FWeaponDef* def = ActiveWeapon();
        return def ? std::string(def->displayName) : std::string("Unarmed");
    }

    int WeaponSystem::AmmoInMag() const
    {
        return slots_[activeSlot_].ammoInMag;
    }

    int WeaponSystem::AmmoReserve(const Inventory& inventory) const
    {
        const FWeaponDef* def = ActiveWeapon();
        return def ? inventory.CountOf(std::string(AmmoItemId(def->ammo))) : 0;
    }

    const std::string& WeaponSystem::SlotWeaponId(int slot) const
    {
        if (slot < 0 || slot >= static_cast<int>(slots_.size()))
        {
            return kEmptyId;
        }
        return slots_[slot].weaponId;
    }

    std::vector<FShotEvent> WeaponSystem::ConsumeShotEvents()
    {
        std::vector<FShotEvent> events;
        events.swap(shotEvents_);
        return events;
    }

    std::vector<FWeaponHitEvent> WeaponSystem::ConsumeHitEvents()
    {
        std::vector<FWeaponHitEvent> events;
        events.swap(hitEvents_);
        return events;
    }

    std::vector<FWeaponTraceEvent> WeaponSystem::ConsumeTraceEvents()
    {
        std::vector<FWeaponTraceEvent> events;
        events.swap(traceEvents_);
        return events;
    }

    bool WeaponSystem::ViewModelRecoilActive() const
    {
        return std::abs(viewModelRecoil_) > 0.0001f || std::abs(viewModelRecoilVelocity_) > 0.001f;
    }

    bool WeaponSystem::ViewModelMuzzle(glm::vec3& outPosition, glm::vec3& outForward) const
    {
        if (!viewModelNode_ || !viewModelVisible_ || !HasActiveWeapon())
        {
            return false;
        }
        const glm::mat4& world = viewModelNode_->WorldTransform();
        const FWeaponModelPoints points = WeaponModelPoints(ActiveWeaponId());
        outPosition = glm::vec3(world * glm::vec4(0.0f, points.boreHeight, points.muzzleForward, 1.0f));
        outForward = glm::normalize(glm::vec3(world[2]));
        return outPosition.y > -900.0f;
    }

    EWeaponPresentationAction WeaponSystem::PresentationAction() const
    {
        if (switching_)
        {
            return EWeaponPresentationAction::Switch;
        }
        if (reloading_)
        {
            return EWeaponPresentationAction::Reload;
        }
        if (IsSwinging())
        {
            return EWeaponPresentationAction::Melee;
        }
        return EWeaponPresentationAction::None;
    }

    float WeaponSystem::PresentationActionTime() const
    {
        if (switching_)
        {
            return glm::clamp(1.0f - switchTimer_ / std::max(config_.SwitchSeconds, 0.01f), 0.0f, 1.0f);
        }
        if (reloading_)
        {
            return glm::clamp(1.0f - reloadTimer_ / std::max(config_.ReloadSeconds, 0.01f), 0.0f, 1.0f);
        }
        if (IsSwinging())
        {
            return glm::clamp(1.0f - swingTimer_ / swingDuration_, 0.0f, 1.0f);
        }
        return 0.0f;
    }

    void WeaponSystem::RefreshViewModelVisibility()
    {
        if (viewModelNode_)
        {
            if (auto render = viewModelNode_->GetComponent<Runtime::RenderComponent>())
            {
                render->SetVisible(HasActiveWeapon());
            }
        }
    }

    void WeaponSystem::FireOneShot(PlayerController& player, NextEngine& engine)
    {
        const FWeaponDef* def = ActiveWeapon();
        if (!def)
        {
            return;
        }

        const glm::vec3 cameraOrigin = player.CameraPosition();
        const glm::vec3 cameraDirection = player.Forward();

        // Resolve the point under the centre-screen crosshair from the real
        // render-camera origin. In TPS this is the offset shoulder camera, not
        // the character eye position.
        glm::vec3 aimPoint = cameraOrigin + cameraDirection * kHitscanRange;
        float aimDistance = kHitscanRange;
        engine.RayCast(cameraOrigin, cameraDirection, [&](Assets::RayCastResult result) {
            if (result.Hit && result.T <= kHitscanRange)
            {
                aimPoint = glm::vec3(result.HitPoint);
                aimDistance = result.T;
            }
            return true;
        });
        if (targetTracer_)
        {
            if (const std::optional<FHitboxHit> target = targetTracer_(cameraOrigin, cameraDirection, aimDistance))
            {
                aimPoint = target->point;
            }
        }

        const glm::vec3 playerUp =
            glm::normalize(glm::cross(player.Right(), cameraDirection));
        const glm::vec3 muzzle = player.EyePosition() + cameraDirection * 0.6f +
                                 player.Right() * 0.12f - playerUp * 0.08f;
        glm::vec3 dir = glm::normalize(aimPoint - muzzle);

        // Random cone spread around the muzzle-to-crosshair direction.
        const float spread = player.IsAiming() ? def->spreadAds : def->spreadHip;
        if (spread > 0.0f)
        {
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            const glm::vec3 right = player.Right();
            const glm::vec3 up = glm::normalize(glm::cross(right, dir));
            const float ax = dist(rng_) * spread;
            const float ay = dist(rng_) * spread;
            dir = glm::normalize(dir + right * std::tan(ax) + up * std::tan(ay));
        }

        const uint64_t sequence = ++shotSequence_;
        std::uniform_real_distribution<float> pelletSpread(-1.0f, 1.0f);
        for (int pellet = 0; pellet < std::max(def->pellets, 1); ++pellet)
        {
            glm::vec3 pelletDirection = dir;
            if (pellet > 0)
            {
                const glm::vec3 right = player.Right();
                const glm::vec3 up = glm::normalize(glm::cross(right, dir));
                pelletDirection = glm::normalize(dir + right * std::tan(pelletSpread(rng_) * spread) +
                                                 up * std::tan(pelletSpread(rng_) * spread));
            }
            glm::vec3 hitPoint = muzzle + pelletDirection * kHitscanRange;
            glm::vec3 hitNormal = -pelletDirection;
            float hitDistance = kHitscanRange;
            uint32_t hitInstanceId = 0;
            bool hit = false;
            engine.RayCast(muzzle, pelletDirection, [&](Assets::RayCastResult result) {
                if (result.Hit && result.T <= kHitscanRange)
                {
                    hitPoint = glm::vec3(result.HitPoint);
                    hitNormal = glm::vec3(result.Normal);
                    hitDistance = result.T;
                    hitInstanceId = result.InstanceId;
                    hit = true;
                }
                return true;
            });
            FWeaponHitEvent hitEvent{sequence, std::string(def->id), muzzle, pelletDirection, hitInstanceId,
                                     hitPoint, def->baseDamage, def->impactScale};
            if (targetTracer_)
            {
                if (const std::optional<FHitboxHit> target = targetTracer_(muzzle, pelletDirection, hitDistance))
                {
                    hitPoint = target->point;
                    hitNormal = target->normal;
                    hitInstanceId = 0;
                    hit = true;
                    hitEvent.hitInstanceId = 0;
                    hitEvent.hitPoint = hitPoint;
                    hitEvent.zombie = target->zombie;
                    hitEvent.zone = target->zone;
                }
            }
            hitEvent.normal = hitNormal;
            if (hit)
            {
                hitEvents_.push_back(hitEvent);
            }
            traceEvents_.push_back({sequence, pellet, muzzle, pelletDirection, hitPoint, hitInstanceId, hit,
                                    hitEvent.zombie.IsValid(), hitNormal});
        }

        std::uniform_real_distribution<float> yawDist(-def->cameraYawDegrees, def->cameraYawDegrees);
        FShotEvent event;
        event.sequence = sequence;
        event.weaponId = std::string(def->id);
        event.cameraImpulseRadians =
            glm::radians(glm::vec2(yawDist(rng_), -def->cameraKickDegrees));
        event.viewModelImpulse = def->viewModelKick;
        event.rigRecoilScale = def->rigRecoilScale;
        shotEvents_.push_back(event);
        viewModelRecoilVelocity_ += event.viewModelImpulse;
    }

    void WeaponSystem::CancelMelee()
    {
        swingTimer_ = 0.0f;
        swingDuration_ = 0.0f;
        swingStrikeResolved_ = false;
        meleeInputTimer_ = 0.0f;
        triggerPressed_ = false;
    }

    void WeaponSystem::BeginSwing()
    {
        const FWeaponDef* def = ActiveWeapon();
        if (!def || !def->IsMelee())
        {
            return;
        }
        swingDuration_ = def->fireInterval;
        swingTimer_ = swingDuration_;
        swingStrikeResolved_ = false;
        swingSequence_ = ++shotSequence_;
        meleeInputTimer_ = 0.0f;
        FShotEvent event;
        event.sequence = swingSequence_;
        event.weaponId = std::string(def->id);
        event.melee = true;
        shotEvents_.push_back(event);
    }

    void WeaponSystem::ResolveMeleeStrike(PlayerController& player, NextEngine& engine)
    {
        const FWeaponDef* def = ActiveWeapon();
        if (!def)
        {
            return;
        }
        // From the eye even in third person: the blade is where the body is.
        const glm::vec3 origin = player.EyePosition();
        const glm::vec3 forward = player.Forward();
        const glm::vec3 right = player.Right();
        const uint64_t sequence = swingSequence_;
        FWeaponHitEvent hitEvent{sequence, std::string(def->id), origin, forward, 0, origin + forward * def->meleeRange,
                                 def->baseDamage, def->impactScale};
        bool hit = false;
        glm::vec3 hitNormal = -forward;
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));
        // The swing sweeps a fan in front of the chest: centre first, then
        // lower (the infected stoop) and out to either side.
        constexpr std::array<glm::vec2, 12> kSweep = {{
            {0.0f, 0.0f}, {0.0f, -0.2f}, {0.0f, -0.4f}, {0.14f, -0.2f}, {-0.14f, -0.2f}, {0.14f, 0.0f},
            {-0.14f, 0.0f}, {0.26f, -0.2f}, {-0.26f, -0.2f}, {0.14f, -0.4f}, {-0.14f, -0.4f}, {0.0f, 0.15f},
        }};
        for (const glm::vec2& sweep : kSweep)
        {
            const float sweepWidth = def->id == "knife" ? 0.5f : def->id == "crowbar" ? 1.6f : 1.0f;
            const glm::vec3 direction = glm::normalize(forward + right * sweep.x * sweepWidth + up * sweep.y);
            float worldDistance = def->meleeRange;
            uint32_t worldInstance = 0;
            glm::vec3 worldPoint{};
            glm::vec3 worldNormal{};
            engine.RayCast(origin, direction, [&](Assets::RayCastResult result) {
                if (result.Hit && result.T <= worldDistance)
                {
                    worldDistance = result.T;
                    worldInstance = result.InstanceId;
                    worldPoint = glm::vec3(result.HitPoint);
                    worldNormal = glm::vec3(result.Normal);
                }
                return true;
            });
            if (targetTracer_)
            {
                if (const std::optional<FHitboxHit> target = targetTracer_(origin, direction, worldDistance))
                {
                    hitEvent.direction = direction;
                    hitEvent.hitInstanceId = 0;
                    hitEvent.zombie = target->zombie;
                    hitEvent.zone = target->zone;
                    hitEvent.hitPoint = target->point;
                    hitNormal = target->normal;
                    hit = true;
                    break;
                }
            }
            if (sweep == glm::vec2(0.0f) && worldInstance != 0)
            {
                hitEvent.hitInstanceId = worldInstance;
                hitEvent.hitPoint = worldPoint;
                hitNormal = worldNormal;
                hit = true;
            }
        }
        hitEvent.normal = hitNormal;
        if (hit)
        {
            hitEvents_.push_back(hitEvent);
            FWeaponTraceEvent trace{sequence, 0, origin, hitEvent.direction, hitEvent.hitPoint, hitEvent.hitInstanceId,
                                    true, hitEvent.zombie.IsValid(), hitNormal};
            trace.melee = true;
            traceEvents_.push_back(trace);
        }
    }

    void WeaponSystem::Update(float deltaSeconds, PlayerController& player, Inventory& inventory, NextEngine& engine)
    {
        const float dt = std::max(0.0f, deltaSeconds);
        fireCooldown_ -= dt;
        SyncInventory(inventory);
        if (const FWeaponDef* melee = ActiveWeapon(); melee && melee->IsMelee())
        {
            if (IsSwinging())
            {
                swingTimer_ = std::max(0.0f, swingTimer_ - dt);
                // Crossing the contact frame resolves once, even on a long frame.
                if (!swingStrikeResolved_ && 1.0f - swingTimer_ / swingDuration_ >= melee->meleeStrikeTime01)
                {
                    swingStrikeResolved_ = true;
                    ResolveMeleeStrike(player, engine);
                }
            }
            // A quick click also brings a sprinting character into the attack.
            // Only recovery clicks expire; lowering the sprint carry must not eat input.
            if (IsSwinging() || player.WeaponReady())
            {
                meleeInputTimer_ = std::max(0.0f, meleeInputTimer_ - dt);
            }
            if (triggerPressed_ && !switching_)
            {
                meleeInputTimer_ = 0.20f;
            }
            if (!IsSwinging() && !switching_ && player.WeaponReady() && meleeInputTimer_ > 0.0f)
            {
                BeginSwing();
            }
        }
        triggerPressed_ = false;

        if (reloading_)
        {
            reloadTimer_ -= dt;
            if (reloadTimer_ <= 0.0f)
            {
                reloading_ = false;
                if (const FWeaponDef* def = ActiveWeapon())
                {
                    const std::string ammoId(AmmoItemId(def->ammo));
                    const int need = def->magSize - slots_[activeSlot_].ammoInMag;
                    const int got = inventory.Consume(ammoId, std::max(0, need));
                    slots_[activeSlot_].ammoInMag += got;
                }
            }
        }

        if (switching_)
        {
            switchTimer_ -= dt;
            if (!switchCommitted_ && PresentationActionTime() >= 0.5f)
            {
                previousSlot_ = activeSlot_;
                activeSlot_ = switchTargetSlot_;
                switchCommitted_ = true;
                RefreshViewModelVisibility();
            }
            if (switchTimer_ <= 0.0f)
            {
                switching_ = false;
                switchCommitted_ = false;
                switchTargetSlot_ = -1;
                switchTimer_ = 0.0f;
            }
        }

        const FWeaponDef* def = ActiveWeapon();

        // Feed ADS target FOV to the camera controller.
        player.SetAimFov(def ? def->adsFov : player.CurrentFov());

        // Coming out of a sprint the weapon has to be up before it fires; a
        // held trigger fires the moment it is.
        if (def && !def->IsMelee() && !reloading_ && !switching_ && triggerDown_ && player.WeaponReady())
        {
            constexpr int kMaxShotsPerFrame = 8;
            int shotsThisFrame = 0;
            const bool newPull = !triggerConsumed_;
            if (newPull && fireCooldown_ <= 0.0f)
            {
                if (slots_[activeSlot_].ammoInMag > 0)
                {
                    FireOneShot(player, engine);
                    slots_[activeSlot_].ammoInMag -= 1;
                    fireCooldown_ = def->fireInterval;
                    ++shotsThisFrame;
                }
                else
                {
                    dryFire_ = true;
                }
                triggerConsumed_ = true;
            }
            while (def->fullAuto && !def->IsMelee() && triggerConsumed_ && fireCooldown_ <= 0.0f &&
                   shotsThisFrame < kMaxShotsPerFrame && slots_[activeSlot_].ammoInMag > 0)
            {
                FireOneShot(player, engine);
                slots_[activeSlot_].ammoInMag -= 1;
                fireCooldown_ += def->fireInterval;
                ++shotsThisFrame;
            }
        }
        if (!triggerDown_)
        {
            triggerConsumed_ = false;
            fireCooldown_ = std::max(0.0f, fireCooldown_);
        }

        // View-model follows the camera; lerp offset toward hip/ADS pose.
        if (viewModelNode_)
        {
            const int weaponIndex = WeaponIndex(ActiveWeaponId());
            if (weaponIndex >= 0 && weaponIndex != viewModelWeaponIndex_)
            {
                viewModelWeaponIndex_ = weaponIndex;
                if (auto render = viewModelNode_->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetModelId(viewModelModelIds_[weaponIndex]);
                    render->SetMaterials(viewModelMaterialIds_[weaponIndex]);
                }
                engine.GetScene().MarkDirty();
            }

            const float recoilDt = std::max(0.0f, deltaSeconds);
            viewModelRecoilVelocity_ +=
                (-config_.ViewModelRecoilSpring * viewModelRecoil_ -
                 config_.ViewModelRecoilDamping * viewModelRecoilVelocity_) *
                recoilDt;
            viewModelRecoil_ += viewModelRecoilVelocity_ * recoilDt;

            glm::vec3 targetOffset = player.IsAiming() ? config_.ViewModelAdsOffset : config_.ViewModelHipOffset;
            if (player.IsAiming() && def && !def->IsMelee())
            {
                // Each model carries its own sight line (irons, bead, PSO-1 axis):
                // drop the view model until that line sits where the tuned one did.
                targetOffset.y += (kAdsReferenceSightHeight - WeaponModelPoints(def->id).sightHeight) *
                                  config_.ViewModelScale;
            }
            glm::vec3 meleeEuler(0.0f);
            if (def && def->IsMelee())
            {
                const FMeleeViewPose ready = MeleeReadyPose(*def);
                targetOffset = player.IsAiming() || IsSwinging() ? ready.offset : glm::vec3(0.28f, -0.36f, 0.48f);
                meleeEuler = glm::radians(player.IsAiming() || IsSwinging() ? ready.euler : glm::vec3(38.0f, -8.0f, -12.0f));
            }
            const float actionTime = PresentationActionTime();
            const float actionArc = std::sin(glm::pi<float>() * actionTime);
            if (reloading_)
            {
                targetOffset.y -= actionArc * 0.18f;
                targetOffset.x += actionArc * 0.08f;
            }
            else if (switching_)
            {
                targetOffset.y -= actionArc * 0.42f;
                targetOffset.z -= actionArc * 0.18f;
            }
            // Traversal: the weapon swings down and out to the firing side while
            // the hands are on the obstacle, and comes back up as the body stands.
            if (player.IsTraversing() && !IsSwinging())
            {
                targetOffset += glm::vec3(0.06f, -0.11f, -0.08f);
                meleeEuler += glm::radians(glm::vec3(28.0f, -16.0f, 20.0f));
            }
            const float t = 1.0f - std::exp(-config_.ViewModelLerpSpeed * std::max(0.0f, deltaSeconds));
            viewModelOffset_ = glm::mix(viewModelOffset_, targetOffset, t);
            if (def && def->IsMelee() && IsSwinging())
            {
                const FMeleeViewPose pose = SampleMeleeSwing(*def, PresentationActionTime());
                viewModelOffset_ = pose.offset;
                meleeEuler = glm::radians(pose.euler);
            }

            const bool show = HasActiveWeapon() && player.IsFirstPerson() && !presentationSuppressed_;
            if (show != viewModelVisible_)
            {
                viewModelVisible_ = show;
                if (auto render = viewModelNode_->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetVisible(show);
                }
                viewModelNode_->Scale() =
                    show ? glm::vec3(config_.ViewModelScale) : glm::vec3(0.0f);
                if (!show && viewModelInScene_)
                {
                    engine.GetScene().RemoveNodeByInstanceId(viewModelNode_->GetInstanceId());
                    viewModelInScene_ = false;
                }
                else if (show && !viewModelInScene_)
                {
                    engine.GetScene().AddNode(viewModelNode_);
                    viewModelInScene_ = true;
                }
                engine.GetScene().MarkDirty();
            }
            glm::vec3 motionOffset(0.0f);
            glm::vec3 motionEuler(0.0f);
            UpdateViewModelMotion(player, deltaSeconds, motionOffset, motionEuler);
            viewModelMeleeEuler_ = IsSwinging() ? meleeEuler : glm::mix(viewModelMeleeEuler_, meleeEuler, t);
            motionEuler += viewModelMeleeEuler_;
            if (show)
            {
                const glm::vec3 eye = player.CameraPosition();
                const glm::vec3 forward = player.CameraForward();
                const glm::vec3 right = player.Right();
                const glm::vec3 up = glm::normalize(glm::cross(right, forward));
                // Proper (det +1) rotation basis: object +Z -> forward, +Y -> up,
                // +X = up x forward (right() is left-handed w.r.t. forward/up).
                const glm::vec3 xAxis = glm::normalize(glm::cross(up, forward));
                const glm::vec3 offset = viewModelOffset_ + motionOffset;
                const glm::vec3 pos =
                    eye + right * offset.x + up * offset.y + forward * (offset.z - viewModelRecoil_ * 2.5f);
                viewModelNode_->Translation() = pos;
                viewModelNode_->Rotation() =
                    glm::quat_cast(glm::mat3(xAxis, up, forward)) *
                    glm::angleAxis(motionEuler.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
                    glm::angleAxis(motionEuler.x, glm::vec3(1.0f, 0.0f, 0.0f)) *
                    glm::angleAxis(motionEuler.z, glm::vec3(0.0f, 0.0f, 1.0f)) *
                    glm::angleAxis(reloading_ ? actionArc * 0.45f : 0.0f, glm::vec3(0.0f, 0.0f, 1.0f)) *
                    glm::angleAxis(-viewModelRecoil_ * 100.0f * glm::radians(config_.ViewModelRecoilPitchDegrees),
                                   glm::vec3(1.0f, 0.0f, 0.0f));
                viewModelNode_->RecalcTransform(true);
            }
            else
            {
                viewModelNode_->Translation() = glm::vec3(0.0f, -1000.0f, 0.0f);
                viewModelNode_->RecalcTransform(true);
            }
            engine.GetScene().MarkTransformDirty();
        }
    }

    void WeaponSystem::UpdateViewModelMotion(const PlayerController& player, float deltaSeconds, glm::vec3& outOffset,
                                             glm::vec3& outEuler)
    {
        const float dt = std::max(deltaSeconds, 0.0f);
        const FWeaponFeelConfig& feel = config_;
        aimAlpha_ = glm::mix(aimAlpha_, player.IsAiming() ? 1.0f : 0.0f, 1.0f - std::exp(-14.0f * dt));
        const float hipAlpha = 1.0f - aimAlpha_;
        const float sprint = glm::smoothstep(0.0f, 1.0f, player.SprintAlpha());
        const FPlayerLocomotionState& locomotion = player.LocomotionState();

        // Look inertia: the weapon lags the camera's angular velocity and
        // springs back past centre when the mouse stops.
        const glm::vec2 angularVelocity = dt > 0.0f ? player.LookDelta() / dt : glm::vec2(0.0f);
        const float swayLimit = glm::radians(feel.ViewModelSwayMaxDegrees);
        const glm::vec2 swayTarget =
            glm::clamp(-angularVelocity * feel.ViewModelSwayLagSeconds, glm::vec2(-swayLimit), glm::vec2(swayLimit)) *
            glm::mix(1.0f, 0.3f, aimAlpha_);
        const float springDt = std::min(dt, 0.05f);
        swayVelocity_ += (feel.ViewModelSwaySpring * (swayTarget - swayAngle_) -
                          feel.ViewModelSwayDamping * swayVelocity_) *
                         springDt;
        swayAngle_ += swayVelocity_ * springDt;

        // Movement: tilt into a strafe, drift against the motion.
        const glm::vec2 velocity = player.LocalVelocity();
        moveVelocity_ = glm::mix(moveVelocity_, velocity,
                                 1.0f - std::exp(-dt / std::max(feel.ViewModelMoveSmoothingSeconds, 1.0e-3f)));

        // Stride bob, a figure eight: one sway and two dips per stride.
        const float speedScale =
            locomotion.onGround ? glm::clamp(locomotion.horizontalSpeed / 4.2f, 0.0f, 1.6f) : 0.0f;
        const float bobScale = speedScale * glm::mix(1.0f, feel.ViewModelBobSprintScale, sprint) *
                               glm::mix(1.0f, feel.ViewModelBobAimScale, aimAlpha_);
        const float theta = glm::two_pi<float>() * player.StridePhase();
        const float dip = 0.5f - 0.5f * std::cos(2.0f * theta);

        // Idle breathing.
        breathTime_ += dt;
        const float breath = std::sin(glm::two_pi<float>() * breathTime_ / std::max(feel.ViewModelBreathSeconds, 0.1f)) *
                             (1.0f - glm::clamp(speedScale * 2.0f, 0.0f, 1.0f)) * glm::mix(1.0f, 0.35f, aimAlpha_);

        // Landing: a kick into a spring.
        if (player.LandingSpeed() > 0.0f)
        {
            landingVelocity_ -= player.LandingSpeed() * feel.ViewModelLandingKick * 10.0f;
        }
        landingVelocity_ += (-160.0f * landingOffset_ - 16.0f * landingVelocity_) * springDt;
        landingOffset_ = glm::clamp(landingOffset_ + landingVelocity_ * springDt, -0.08f, 0.04f);

        outOffset = glm::vec3(feel.ViewModelBob.x * std::sin(theta) * bobScale - swayAngle_.x * 0.05f * hipAlpha -
                                  moveVelocity_.x * feel.ViewModelMoveOffsetPerMps * hipAlpha,
                              -feel.ViewModelBob.y * dip * bobScale + swayAngle_.y * 0.03f * hipAlpha +
                                  feel.ViewModelBreathHeight * breath + landingOffset_,
                              -std::max(moveVelocity_.y, 0.0f) * feel.ViewModelMoveOffsetPerMps * 0.6f * hipAlpha) +
                    feel.ViewModelSprintOffset * sprint;
        outEuler = glm::vec3(swayAngle_.y + glm::radians(feel.ViewModelSprintEulerDegrees.x) * sprint,
                             swayAngle_.x + glm::radians(feel.ViewModelSprintEulerDegrees.y) * sprint,
                             glm::radians(feel.ViewModelBobRollDegrees) * std::sin(theta) * bobScale +
                                 glm::radians(feel.ViewModelMoveRollDegreesPerMps) * moveVelocity_.x *
                                     glm::mix(1.0f, 0.25f, aimAlpha_) +
                                 glm::radians(feel.ViewModelSprintEulerDegrees.z) * sprint);
    }
}
