#pragma once

#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"
#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Player/PlayerController.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

#include <array>
#include <random>
#include <vector>

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
        bool suppressed = false;
    };

    class PlayerController;

    // Shared weapon timers, inventory magazines and trace events. A client
    // supplies the world and character queries and consumes the results.
    class WeaponRuntime
    {
    public:
        virtual ~WeaponRuntime() = default;
        void Configure(const FWeaponFeelConfig& config) { config_ = config; }

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
        void RefreshInventory(Inventory& inventory) { SyncInventory(inventory); }
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

        void Update(float deltaSeconds, PlayerController& player, Inventory& inventory, const FWorldRayQuery& raycast);

        // --- queries (HUD / agent) ---
        bool HasActiveWeapon() const { return ActiveWeapon() != nullptr; }
        const FWeaponDef* ActiveWeapon() const;
        std::string ActiveWeaponId() const;
        std::string ActiveDisplayName() const;
        int AmmoInMag() const;
        int AmmoReserve(const Inventory& inventory) const;
        int MagazineCapacity() const { return WeaponMagazineCapacity(ActiveWeaponId(), SlotAttachments(activeSlot_)); }
        float ReloadDuration() const { return config_.ReloadSeconds * WeaponReloadScale(SlotAttachments(activeSlot_)); }
        const FWeaponAttachments& SlotAttachments(int slot) const;
        EWeaponAttachment ActiveOptic() const { return AttachmentAt(SlotAttachments(activeSlot_), EAttachmentSlot::Optic); }
        bool IsSuppressed() const { return WeaponSuppressed(SlotAttachments(activeSlot_)); }
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

    protected:
        struct FSlot
        {
            std::string weaponId;   // empty = empty slot
            int ammoInMag = 0;
            FItemInstanceId instanceId = 0;   // 0 = not bound to an inventory item
            FWeaponAttachments attachments{};
        };

        void FireOneShot(PlayerController& player, const FWorldRayQuery& raycast);
        void BeginSwing();
        void ResolveMeleeStrike(PlayerController& player, const FWorldRayQuery& raycast);
        // Keeps slots in step with the inventory: a dropped gun leaves its slot,
        // the magazine is written back to the gun.
        void SyncInventory(Inventory& inventory);
        virtual void RefreshViewModelVisibility() {}
        virtual void ResetEquipPresentation() {}

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
        float reloadDuration_ = 0.0f;
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

        float viewModelRecoil_ = 0.0f;
        float viewModelRecoilVelocity_ = 0.0f;
        std::mt19937 rng_{0xD00Du};
    };
}
