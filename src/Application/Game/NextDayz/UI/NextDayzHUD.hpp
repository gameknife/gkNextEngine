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

namespace NextDayz
{
    class WeaponSystem;

    struct FDebugHudState
    {
        glm::vec3 position{0.0f};
        glm::vec3 eyePosition{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec2 localMove{0.0f};
        glm::vec2 cameraRecoilRadians{0.0f};
        float yawRadians = 0.0f;
        float pitchRadians = 0.0f;
        float fovDegrees = 0.0f;
        float horizontalSpeed = 0.0f;
        float controllerHeight = 0.0f;
        float aimWeight = 0.0f;
        float actionTime = 0.0f;
        std::string stance;
        std::string desiredStance;
        std::string gait;
        std::string jumpPhase;
        std::string traversalAction;
        std::string traversalProbeResult;
        std::string baseAnimation;
        std::string action;
        std::string weaponAction;
        std::string weaponActionClip;
        float weaponActionTime = 0.0f;
        float weaponActionWeight = 0.0f;
        float jumpPhaseTime = 0.0f;
        float traversalTime = 0.0f;
        float traversalHeight = 0.0f;
        int switchTargetSlot = -1;
        bool onGround = false;
        bool standBlocked = false;
        bool sprinting = false;
        bool actionCommitted = false;
        bool cameraRecoilActive = false;
        bool rigRecoilActive = false;
        bool viewModelRecoilActive = false;
        bool switchingWeapon = false;
        bool switchCommitted = false;
        uint64_t shotSequence = 0;
        int activeZombies = 0;
        int alertedZombies = 0;
        int zombieKills = 0;
        int zombieCap = 0;
        int zombiePathSegments = 0;
        int hitBodies = 0;          // infected with live skeleton hitboxes
        int hitCapsules = 0;
        int headshots = 0;
        int zombieStaggers = 0;
        int lootAvailable = 0;
        int lootReserved = 0;
        int lootCooldown = 0;
        int lootSpots = 0;
        int lootPiles = 0;
        int lootZones = 0;
        std::array<int, 6> lootAvailableByCategory{};
        std::array<int, 6> lootTotalByCategory{};
        int criticalFood = 0;
        int criticalMedical = 0;
        int criticalBackpack = 0;
        int criticalWeapons = 0;
        int criticalAmmo = 0;
        int criticalWaterSources = 0;
        int recentWeaponTraces = 0;
        uint32_t lastTraceInstanceId = 0;
        std::string lastTraceResult;
        bool zombieOverlay = true;
        bool lootOverlay = true;
        bool hitProxyOverlay = true;
    };

    // Loot within reach, for the inventory's vicinity column.
    struct FHudNearbyLoot
    {
        int index = -1;
        std::string name;
        float distance = 0.0f;
    };

    struct FHudNotification
    {
        std::string text;
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
        bool firstPerson = true;

        bool hasWeapon = false;
        bool melee = false;
        std::string weaponName;
        int ammoInMag = 0;
        int ammoReserve = 0;
        bool reloading = false;
        int activeSlot = 0;

        std::string interactionPrompt; // empty = nothing hovered
        int hour = 8;
        int minute = 0;
        bool overcast = false;
        std::string weather;

        bool showInventory = false;
        bool showDebugPanel = false;
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
        std::array<std::string, 4> quickSlots;   // item ids bound to keys 3..6
        std::vector<FHudNotification> notifications;
        FDebugHudState debug;

        std::function<void(FItemInstanceId instanceId, int slot)> equipWeapon;
        std::function<void(FItemInstanceId instanceId)> wear;
        std::function<void(FItemInstanceId instanceId)> takeOff;
        std::function<void(FItemInstanceId instanceId)> useItem;
        std::function<void(FItemInstanceId instanceId, int count)> dropItem;
        std::function<void(FItemInstanceId instanceId, FContainerId containerId)> moveItem;
        std::function<void(FItemInstanceId instanceId)> splitItem;
        std::function<void(int lootIndex)> takeLoot;
        std::function<void(int slot, const std::string& itemId)> bindQuick;
        std::function<void(int slot)> unequipWeapon;
        std::function<void()> restartSession;
    };

    namespace NextDayzHUD
    {
        void Draw(const FHudContext& context);
    }
}
