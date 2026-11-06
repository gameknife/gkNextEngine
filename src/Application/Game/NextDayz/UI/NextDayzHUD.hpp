#pragma once

// ============================================================================
// NextDayzHUD.hpp - ImGui HUD: crosshair and hit markers, damage direction and
// vignette, survival status (bleeding, cold, weak, healing), a message feed,
// the compass strip and map screen (only with those items), weapon/ammo
// readout, quickbar, and the inventory screen (vicinity / equipment / cargo
// with drag and drop). Pure presentation: the GameInstance packs a context
// each frame and receives requests back through callbacks.
// ============================================================================

#include <functional>
#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Player/SurvivalSystem.hpp"
#include "Application/Game/NextDayz/UI/Localization.hpp"

namespace NextDayz
{
    class WeaponSystem;

    // Loot within reach, for the inventory's vicinity column.
    struct FHudNearbyLoot
    {
        int index = -1;
        std::string name;
        float distance = 0.0f;
        EEquipSlot equipSlot = EEquipSlot::None;
        std::string itemId;
        int count = 1;
    };

    struct FHudNotification
    {
        L10n::FText text;
        float age = 0.0f;
        int severity = 0;   // 0 info, 1 warning, 2 danger, 3 good news
    };

    struct FHudPoi
    {
        std::string name;
        std::string kind;
        glm::vec2 xz{0.0f};
    };

    struct FHudContext
    {
        bool aiming = false;
        bool freeLooking = false;
        bool firstPerson = true;
        bool scopeView = false;
        EWeaponAttachment optic = EWeaponAttachment::None;
        bool sightReady = false;
        glm::vec2 reflexOffsetPx{0.0f};

        bool hasWeapon = false;
        bool melee = false;
        bool swinging = false;
        float swingProgress = 0.0f;
        std::string weaponName;
        int ammoInMag = 0;
        int ammoReserve = 0;
        bool reloading = false;
        int activeSlot = 0;

        std::string interactionPrompt; // empty = nothing hovered
        bool actionActive = false;
        std::string actionLabel;
        float actionProgress = 0.0f;
        int hour = 8;
        int minute = 0;
        bool overcast = false;
        std::string weather;

        bool showInventory = false;
        bool showMap = false;
        const Inventory* inventory = nullptr;
        const WeaponSystem* weapons = nullptr;
        FSurvivalSnapshot survival;
        float armor = 0.0f;
        float warmth = 0.0f;
        bool weak = false;
        bool lanternLit = false;
        float lanternMinutes = 0.0f;
        bool paused = false;
        double survivalSeconds = 0.0;
        int kills = 0;
        std::string objective;
        // Combat feedback, each 1 on the event and fading to 0.
        float hitMarker = 0.0f;
        bool hitMarkerKill = false;
        bool hitMarkerHead = false;
        float hurt = 0.0f;
        float hurtYawRadians = 0.0f;   // attacker bearing relative to the view, + = left
        // Navigation (the compass and map only show with those items).
        bool hasCompass = false;
        bool hasMap = false;
        float yawRadians = 0.0f;       // camera yaw: 0 looks along +Z (south on the map)
        glm::vec3 playerPosition{0.0f};
        std::vector<FHudPoi> pois;
        std::optional<glm::vec3> cacheMarker;
        // Inventory screen and quickbar.
        std::vector<FHudNearbyLoot> nearby;
        std::array<std::string, 4> quickSlots;   // item ids bound to keys 4..7
        std::vector<FHudNotification> notifications;
        // Request an offscreen character image; zero means the first frame is still pending.
        std::function<uint64_t(glm::vec2 size, float yaw, float distance)> characterPreview;
        std::function<uint64_t(std::string_view itemId)> itemIcon;
        std::function<void()> closeInventory;

        std::function<void(FItemInstanceId instanceId, int slot)> equipWeapon;
        std::function<void(FItemInstanceId weapon, FItemInstanceId attachment)> installAttachment;
        std::function<void(FItemInstanceId weapon, EAttachmentSlot slot)> removeAttachment;
        std::function<void(FItemInstanceId instanceId)> wear;
        std::function<void(FItemInstanceId instanceId)> takeOff;
        std::function<void(FItemInstanceId instanceId)> useItem;
        std::function<void(FItemInstanceId instanceId, int count)> dropItem;
        std::function<void(FItemInstanceId instanceId, FContainerId containerId)> moveItem;
        std::function<void(FItemInstanceId instanceId, FContainerId containerId, FGridPlacement placement)> placeItem;
        std::function<void(FItemInstanceId source, FItemInstanceId target)> mergeItems;
        std::function<void(int lootIndex, FContainerId destination, FGridPlacement placement)> takeLootAt;
        std::function<void(FItemInstanceId instanceId)> splitItem;
        std::function<void(int lootIndex, FContainerId destination, EEquipSlot equipSlot)> takeLoot;
        std::function<void(int slot, const std::string& itemId)> bindQuick;
        std::function<void(int slot)> unequipWeapon;
        std::function<void()> restartSession;
    };

    namespace NextDayzHUD
    {
        void Draw(const FHudContext& context);
    }
}
