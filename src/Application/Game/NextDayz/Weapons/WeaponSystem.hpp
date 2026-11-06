#pragma once

// ============================================================================
// WeaponSystem.hpp - Two back slots plus a waist sidearm: equip / switch /
// hitscan fire / melee swings / reload / ADS. Fire is an engine RayCast for
// the world plus a hitbox trace for the infected; hits leave as
// FWeaponHitEvent for the CombatSystem. Melee owns a wind-up/contact/recovery
// timeline, with one strike per click and a short recovery input buffer.
// The FPS view-model tracks the camera with separate ADS and melee guard poses. Each slot holds an
// inventory item instance: the magazine belongs to that gun (it survives
// switching, dropping and picking up again) and ammo reserves live in the
// Inventory.
// ============================================================================

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"
#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Player/PlayerState.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

class NextEngine;

namespace Assets
{
    class Node;
}

namespace NextDayz
{
    struct FShotEvent
    {
        uint64_t sequence = 0;
        std::string weaponId;
        glm::vec2 cameraImpulseRadians{0.0f}; // x=yaw, y=pitch
        float viewModelImpulse = 0.0f;
        float rigRecoilScale = 1.0f;
        bool melee = false;
    };

    class PlayerController;

    class WeaponSystem
    {
    public:
        void Configure(const FWeaponFeelConfig& config) { config_ = config; }

        // View-model proc assets injected by the GameInstance (BeforeSceneRebuild).
        void SetViewModelAssets(const std::array<uint32_t, kWeapons.size()>& modelIds,
                                const std::array<FWeaponMaterialSlots, kWeapons.size()>& materialIds);

        void OnSceneLoaded(NextEngine& engine);
        void OnSceneUnloaded();
        void ResetRuntime();

        // Loads a weapon into a slot with a fresh magazine, not bound to any
        // inventory item (validation loadouts). Returns false if id unknown.
        bool Equip(int slot, const std::string& weaponId);
        // Puts an inventory weapon (firearm or melee) in a slot; its magazine
        // is whatever that gun holds. Taking it from the other slot empties that.
        bool EquipInstance(int slot, const Inventory& inventory, FItemInstanceId instanceId);
        void Unequip(int slot);
        FItemInstanceId SlotInstance(int slot) const;
        void CopyMagazinesTo(Inventory& inventory) const;
        void SwitchSlot(int slot);
        void SwitchPrevious();
        static bool CanEquipInSlot(int slot, std::string_view weaponId);
        void RestoreSelection(int activeSlot, int previousSlot);

        // Rounds are traced against the infected hitboxes up to the first
        // world surface; the world raycast alone never sees an infected.
        using FTargetTracer = std::function<std::optional<FHitboxHit>(const glm::vec3& origin,
                                                                      const glm::vec3& direction, float maxDistance)>;
        void SetTargetTracer(FTargetTracer tracer) { targetTracer_ = std::move(tracer); }

        void SetTriggerDown(bool down)
        {
            triggerPressed_ = triggerPressed_ || (down && !triggerDown_);
            triggerDown_ = down;
        }
        void CancelMelee();
        bool TriggerDown() const { return triggerDown_; }
        bool MeleeInputPending() const { return ActiveIsMelee() && (triggerPressed_ || meleeInputTimer_ > 0.0f); }
        void SetPresentationSuppressed(bool suppressed) { presentationSuppressed_ = suppressed; }
        void RequestReload(Inventory& inventory);

        void Update(float deltaSeconds, PlayerController& player, Inventory& inventory, NextEngine& engine);

        // --- queries (HUD / agent) ---
        bool HasActiveWeapon() const { return ActiveWeapon() != nullptr; }
        const FWeaponDef* ActiveWeapon() const;
        std::string ActiveWeaponId() const;
        std::string ActiveDisplayName() const;
        int AmmoInMag() const;
        int AmmoReserve(const Inventory& inventory) const;
        bool IsReloading() const { return reloading_; }
        bool IsSwitching() const { return switching_; }
        bool IsSwinging() const { return swingTimer_ > 0.0f; }
        bool MeleeStrikeResolved() const { return swingStrikeResolved_; }
        bool ActiveIsMelee() const { return ActiveWeapon() && ActiveWeapon()->IsMelee(); }
        EWeaponPresentationAction PresentationAction() const;
        float PresentationActionTime() const;
        int ActiveSlot() const { return activeSlot_; }
        int PreviousSlot() const { return previousSlot_; }
        int SwitchSourceSlot() const { return switchSourceSlot_; }
        int SwitchTargetSlot() const { return switchTargetSlot_; }
        bool SwitchCommitted() const { return switchCommitted_; }
        const std::string& SlotWeaponId(int slot) const;

        std::vector<FShotEvent> ConsumeShotEvents();
        std::vector<FWeaponHitEvent> ConsumeHitEvents();
        std::vector<FWeaponTraceEvent> ConsumeTraceEvents();
        uint64_t LastShotSequence() const { return shotSequence_; }
        // True once after a trigger pull on an empty magazine.
        bool ConsumeDryFire()
        {
            const bool fired = dryFire_;
            dryFire_ = false;
            return fired;
        }
        bool ViewModelRecoilActive() const;
        bool IsViewModelVisible() const { return viewModelVisible_; }
        bool IsScopeView() const { return scopeView_; }
        // World-space view-model landmarks. The scoped optical view keeps its
        // weapon pose for effects and validation even while the mesh is hidden.
        bool ViewModelMuzzle(glm::vec3& outPosition, glm::vec3& outForward) const;
        bool ViewModelSight(glm::vec3& outPosition, glm::vec3& outForward) const;

    private:
        struct FSlot
        {
            std::string weaponId;   // empty = empty slot
            int ammoInMag = 0;
            FItemInstanceId instanceId = 0;   // 0 = not bound to an inventory item
        };

        void FireOneShot(PlayerController& player, NextEngine& engine);
        void BeginSwing();
        void ResolveMeleeStrike(PlayerController& player, NextEngine& engine);
        // Keeps slots in step with the inventory: a dropped gun leaves its slot,
        // the magazine is written back to the gun.
        void SyncInventory(Inventory& inventory);
        void RefreshViewModelVisibility();
        // Camera-space offset (x right, y up, z forward) and local rotation
        // (pitch, yaw, roll in radians) the view model carries this frame.
        void UpdateViewModelMotion(const PlayerController& player, float deltaSeconds, glm::vec3& outOffset,
                                   glm::vec3& outEuler);

        FWeaponFeelConfig config_{};
        FTargetTracer targetTracer_;
        std::array<FSlot, kWeaponSlotCount> slots_{};
        int activeSlot_ = 0;
        int previousSlot_ = -1;

        bool triggerDown_ = false;
        bool triggerPressed_ = false;
        bool presentationSuppressed_ = false;
        bool triggerConsumed_ = false; // semi-auto: needs a release between shots
        bool dryFire_ = false;
        float fireCooldown_ = 0.0f;
        bool reloading_ = false;
        float reloadTimer_ = 0.0f;
        bool switching_ = false;
        bool switchCommitted_ = false;
        int switchTargetSlot_ = -1;
        int switchSourceSlot_ = -1;
        float switchTimer_ = 0.0f;
        float swingTimer_ = 0.0f;
        float swingDuration_ = 0.0f;
        bool swingStrikeResolved_ = false;
        uint64_t swingSequence_ = 0;
        float meleeInputTimer_ = 0.0f;
        uint64_t shotSequence_ = 0;
        std::vector<FShotEvent> shotEvents_;
        std::vector<FWeaponHitEvent> hitEvents_;
        std::vector<FWeaponTraceEvent> traceEvents_;

        // view model
        std::array<uint32_t, kWeapons.size()> viewModelModelIds_{};
        std::array<FWeaponMaterialSlots, kWeapons.size()> viewModelMaterialIds_{};
        bool viewModelAssetsSet_ = false;
        int viewModelWeaponIndex_ = -1;
        std::shared_ptr<Assets::Node> viewModelNode_;
        bool viewModelVisible_ = false;
        bool scopeView_ = false;
        bool viewModelInScene_ = false;
        glm::vec3 viewModelOffset_{0.16f, -0.19f, 0.55f};
        glm::vec3 viewModelMeleeEuler_{0.0f};
        float viewModelRecoil_ = 0.0f;
        float viewModelRecoilVelocity_ = 0.0f;
        glm::vec2 swayAngle_{0.0f};          // radians: x yaw (+left), y pitch (+muzzle down)
        glm::vec2 swayVelocity_{0.0f};
        glm::vec2 moveVelocity_{0.0f};       // smoothed camera-relative velocity
        float landingOffset_ = 0.0f;
        float landingVelocity_ = 0.0f;
        float breathTime_ = 0.0f;
        float aimAlpha_ = 0.0f;

        std::mt19937 rng_{0xD00Du};
    };
}
