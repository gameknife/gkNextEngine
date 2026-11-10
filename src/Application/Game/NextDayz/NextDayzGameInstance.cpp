#include "Engine/Common/CoreMinimal.hpp"
#include "NextDayzGameInstance.hpp"
#include "Application/Game/NextDayz/UI/Localization.hpp"
#include "Application/Game/NextDayz/UI/InventoryPreview.hpp"
#include "Application/Game/NextDayz/UI/ItemIconCache.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <fmt/format.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"
#include "Modules/ScadLoader/FScadRig.h"
#include "Modules/ScadLoader/FScadShared.h"
#include "Zombies/ZombieMovement.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#include "Modules/NextUI/FontLoader.hpp"

#include "Application/Game/NextDayz/UI/NextDayzHUD.hpp"

namespace
{
    constexpr const char* kDefaultScene = "assets/scad/proc/coldwar/riverland_1km.scad";
    constexpr const char* kSoldierRig = "assets/scad/characters/nextdayz_survivor.scad";
    constexpr const char* kInfectedRig = "assets/scad/characters/nextdayz_infected.scad";
    constexpr const char* kLootCatalogRig = "assets/scad/characters/nextdayz_loot_items.scad";
    constexpr const char* kWeaponCatalogRig = "assets/scad/characters/nextdayz_weapons.scad";
    constexpr float kNotificationSeconds = 7.0f;
    constexpr float kLanternFuelSeconds = 15.0f * 60.0f;
    constexpr float kCacheMarkerSeconds = 20.0f * 60.0f;

    std::string ItemDisplayName(std::string_view itemId)
    {
        const NextDayz::FItemDef* definition = NextDayz::FindItemDef(itemId);
        return definition ? std::string(definition->displayName) : std::string(itemId);
    }

    // Compass words for a horizontal offset (engine +Z is south).
    std::string CompassWord(const glm::vec3& delta)
    {
        static constexpr std::array<const char*, 8> words = {"north", "north-east", "east", "south-east",
                                                             "south", "south-west", "west", "north-west"};
        float bearing = glm::degrees(std::atan2(delta.x, -delta.z));
        bearing = bearing < 0.0f ? bearing + 360.0f : bearing;
        return words[static_cast<size_t>((bearing + 22.5f) / 45.0f) % 8];
    }

    // Faded shirt colours for the infected; each body slot wears one.
    constexpr std::array<glm::vec3, 8> kInfectedShirts = {{
        {0.42f, 0.12f, 0.10f}, {0.20f, 0.28f, 0.40f}, {0.55f, 0.50f, 0.38f}, {0.30f, 0.36f, 0.24f},
        {0.62f, 0.60f, 0.56f}, {0.17f, 0.17f, 0.19f}, {0.45f, 0.30f, 0.18f}, {0.25f, 0.40f, 0.38f},
    }};

    constexpr float kHitMarkerSeconds = 0.22f;
    constexpr float kKillMarkerSeconds = 0.42f;
    constexpr float kHurtSeconds = 0.9f;

}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Modules::Scad::Register(); // .scad scene + rig loader
    return std::make_unique<NextDayzGameInstance>(config, options, engine);
}

NextDayzGameInstance::NextDayzGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                           NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    NextDayz::L10n::Initialize(options.AgentValidation);
    ConfigureWindow(config, options, "NextDayz", options.WidthSpecified ? static_cast<int>(options.Width) : 1920,
                    options.HeightSpecified ? static_cast<int>(options.Height) : 1080, false);
    // Leave GPU time for other local clients, independent of focus or display refresh rate.
    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "60");
    options.PresentMode = static_cast<uint32_t>(VK_PRESENT_MODE_IMMEDIATE_KHR);
}

void NextDayzGameInstance::OnInitUI()
{
    NextGameInstanceBase::OnInitUI();
    const float density = ImGui::GetIO().FontDefault->Sources[0]->RasterizerDensity;
    constexpr float titleSize = 28.0f;
    inventoryTitleFont_ = NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/Roboto-BoldCondensed.ttf",
        .pixelSize = titleSize,
        .rasterizerDensity = density,
    });
    if (inventoryTitleFont_)
    {
        // Pair the Title face with the localized heading's Chinese glyphs.
        ImFontConfig fallback;
        fallback.MergeMode = true;
        NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
            .filePath = "assets/fonts/DroidSansFallback.ttf",
            .pixelSize = titleSize,
            .extraGlyphsUtf8 = NextDayz::L10n::Translate("Inventory", NextDayz::L10n::ELanguage::Chinese).data(),
            .fontConfig = &fallback,
            .rasterizerDensity = density,
        });
    }
}

void NextDayzGameInstance::OnInit()
{
    // Runtime-added rig/loot nodes trigger scene mesh-buffer rebuilds; keep the
    // CPU mesh copies alive (same requirement as CharacterDemo/AirportSim).
    GOption->KeepCPUMeshData = true;
    sessionRng_.Reset(config_.Seed);
    BindDebugger();

    survivalRng_.Reset(sessionRng_.Derive(0x53555256ULL).Seed());
    weapons_.Configure(config_.Weapon);
    loot_.Configure(config_.Loot);
    loot_.SetSeed(config_.Seed);
    rig_.Configure(config_.Animation, config_.Weapon.TpsWeaponScale);
    actions_.Configure(config_.Action);
    survival_.Configure(config_.Survival);
    world_.Configure(config_.Zombie);
    netMode_ = netModeCvar_ == "server" ? ENetMode::Server
             : netModeCvar_ == "client" ? ENetMode::Client : ENetMode::Offline;
    const bool explicitNetMode = std::any_of(GOption->CVarOverrides.begin(), GOption->CVarOverrides.end(), [](std::string_view command)
    {
        const auto first = command.find_first_not_of(" \t");
        if (first == std::string_view::npos) return false;
        command.remove_prefix(first);
        constexpr std::string_view key = "nextdayz.net.mode";
        return command.starts_with(key) && command.size() > key.size() &&
            (command[key.size()] == '=' || command[key.size()] == ' ' || command[key.size()] == '\t');
    });
    if (!explicitNetMode && netMode_ == ENetMode::Offline && !netMasterCvar_.empty()) netMode_ = ENetMode::Client;
    InitializeSessionMenu();
    world_.SetRayCast([this](const glm::vec3& origin, const glm::vec3& direction)
    {
        return GetEngine().GetScene().GetCPUAccelerationStructure().RayCastInCPU(origin, direction);
    });
    audio_.SetEngine(&GetEngine());
    weapons_.SetTargetTracer([this](const glm::vec3& origin, const glm::vec3& direction, float maxDistance)
    {
        return world_.combat.TraceHitboxes(origin, direction, maxDistance);
    });

    if (!rig_.LoadRig(kSoldierRig))
    {
        SPDLOG_WARN("[NextDayz] soldier rig unavailable; player will have no visual");
    }
    {
        std::string error;
        std::vector<std::string> warnings;
        weaponCatalogLoaded_ =
            Assets::FScadRigLoader::LoadRig(kWeaponCatalogRig, {}, weaponCatalogAsset_, error, &warnings);
        if (!weaponCatalogLoaded_)
        {
            SPDLOG_ERROR("[NextDayz] weapon catalog unavailable; held weapons are placeholder boxes: {}", error);
        }
        for (const std::string& warning : warnings)
        {
            SPDLOG_WARN("[NextDayz] weapon catalog: {}", warning);
        }
    }

    {
        std::string error;
        attachmentCatalogLoaded_ = Assets::FScadRigLoader::LoadRig(
            "assets/scad/characters/nextdayz_attachments.scad", {}, attachmentCatalogAsset_, error);
        if (!attachmentCatalogLoaded_) SPDLOG_ERROR("[NextDayz] attachment catalog unavailable: {}", error);
    }
    std::string scene = kDefaultScene;
    std::string mapError;
    if (!NextDayz::LoadExpeditionMaps(Utilities::FileHelper::ResolvePlatformFilePath(
            "assets/configs/nextdayz_maps.json"), expeditionMaps_, mapError))
    {
        SPDLOG_ERROR("[NextDayz] map registry: {}", mapError);
    }
    if (const auto published = std::find_if(expeditionMaps_.begin(), expeditionMaps_.end(),
            [](const auto& map) { return map.published && !map.training; }); published != expeditionMaps_.end())
        scene = published->scene;
    if (!GOption->SceneName.empty())
    {
        scene = GOption->SceneName;
    }
    campaignMap_ = scene;
    loadingCabin_ = !authoringMode_ && ((CampaignEnabled() && netMode_ == ENetMode::Offline) || netMode_ == ENetMode::Client);
    if (scene == cabinScenePath)
    {
        loadingCabin_ = true;
        if (GOption->AgentValidation) validationCampaign_ = true;
        campaignMap_ = kDefaultScene;
        if (const auto published = std::find_if(expeditionMaps_.begin(), expeditionMaps_.end(),
                [](const auto& map) { return map.published && !map.training; }); published != expeditionMaps_.end())
            campaignMap_ = published->scene;
    }
    cabinLobby_ = loadingCabin_;
    SPDLOG_INFO("[NextDayz] loading scene '{}'", loadingCabin_ ? cabinScenePath : scene);
    GetEngine().RequestLoadScene({.filename = loadingCabin_ ? cabinScenePath : scene});
    StartNetwork();
}

void NextDayzGameInstance::LoadFieldAssets()
{
    if (netMode_ == ENetMode::Client && remoteRigs_.empty())
    {
        // Bodies for the other survivors, built with the scene like the player's own.
        for (int index = 0; index < std::clamp(netMaxPlayersCvar_ - 1, 1, 4); ++index)
        {
            auto remote = std::make_unique<NextDayz::PlayerRigVisual>();
            remote->Configure(config_.Animation, config_.Weapon.TpsWeaponScale);
            if (!remote->LoadRig(kSoldierRig)) break;
            remoteRigs_.push_back(std::move(remote));
        }
    }
    if (fieldAssetsLoaded_) return;
    {
        std::string error;
        std::vector<std::string> warnings;
        zombieRigLoaded_ = Assets::FScadRigLoader::LoadRig(
            kInfectedRig, {}, zombieRigAsset_, error, &warnings);
        if (!zombieRigLoaded_)
        {
            SPDLOG_WARN("[NextDayz] infected rig unavailable; infected are invisible capsules: {}", error);
        }
        for (const std::string& warning : warnings)
        {
            SPDLOG_WARN("[NextDayz] infected rig: {}", warning);
        }
        if (zombieRigLoaded_)
        {
            zombieRigClips_ = NextDayz::ZombieRigVisual::FClips::Resolve(zombieRigAsset_);
        }
    }
    {
        std::string error;
        std::vector<std::string> warnings;
        lootCatalogLoaded_ = Assets::FScadRigLoader::LoadRig(kLootCatalogRig, {}, lootCatalogAsset_, error, &warnings);
        if (!lootCatalogLoaded_)
        {
            SPDLOG_WARN("[NextDayz] loot catalog unavailable; loot spots are invisible: {}", error);
        }
    }
    {
        std::string error;
        fpsHandsLoaded_ = Assets::FScadRigLoader::LoadRig("assets/scad/characters/nextdayz_fps_hands.scad", {},
                                                          fpsHandsAsset_, error);
        if (!fpsHandsLoaded_) SPDLOG_ERROR("[NextDayz] first-person hands unavailable: {}", error);
    }
    fieldAssetsLoaded_ = true;
}

void NextDayzGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    ConfigureCampaignCVars(cvars);
    ConfigureNetCVars(cvars);
    ConfigureCabinCVars(cvars);
    ConfigureSettingsCVars(cvars);
    ConfigureSessionCVars(cvars);
    std::string error;
    // Path tracing with DLSS Ray Reconstruction at Quality by default.
    cvars.SetDefaultFromString("r.rendererType", "0", &error);
    cvars.SetDefaultFromString("r.upscaler.type", "2", &error);
    cvars.SetDefaultFromString("r.upscaler.qualityMode", "0", &error);
    cvars.SetDefaultFromString("r.exposure.auto", "true", &error);
    cvars.SetDefaultFromString("r.exposure.maxEV", "3", &error);

    cvars.RegisterBool("nextdayz.grass.enabled", true, &grass_.settings.enabled, NextCVar::ECVarFlags::None,
                       "Grow runtime grass on grassland terrain biomes");
    cvars.RegisterFloat("nextdayz.grass.radius", 192.0f, &grass_.settings.radius, NextCVar::ECVarFlags::None,
                        "Grass coverage radius in metres; large patches cover the distant rings", nullptr, 8.0, 192.0);
    cvars.RegisterFloat("nextdayz.grass.density", 1.0f, &grass_.settings.density, NextCVar::ECVarFlags::None,
                        "Deterministic grass coverage", nullptr, 0.0, 1.0);
    cvars.RegisterFloat("nextdayz.grass.edgeWidth", 4.0f, &grass_.settings.edgeWidth, NextCVar::ECVarFlags::None,
                        "Metres of density/height falloff beside excluded terrain; zero disables feathering",
                        nullptr, 0.0, 12.0);
    cvars.RegisterFloat("nextdayz.grass.transmission", 1.0f, &grass_.settings.transmission, NextCVar::ECVarFlags::None,
                        "Grass direct-light translucency; zero restores opaque lighting", nullptr, 0.0, 1.0);
    cvars.RegisterFloat("nextdayz.grass.windStrength", 1.0f, &grass_.settings.windStrength, NextCVar::ECVarFlags::None,
                        "Wind bending strength", nullptr, 0.0, 1.0);
    cvars.RegisterFloat("nextdayz.grass.windYaw", 0.7f, &grass_.settings.windYaw, NextCVar::ECVarFlags::None,
                        "Wind heading in radians: +X toward -Z");
    cvars.RegisterFloat("nextdayz.grass.windSpeed", 1.15f, &grass_.settings.windSpeed, NextCVar::ECVarFlags::None,
                        "Wind animation speed; zero freezes the field", nullptr, 0.0, 4.0);
    cvars.RegisterBool("nextdayz.grass.interaction", true, &grass_.settings.interactionEnabled,
                       NextCVar::ECVarFlags::None, "Root-preserving grass contact and recovery");
    cvars.RegisterFloat("nextdayz.grass.interactionStrength", 1.0f, &grass_.settings.interactionStrength,
                        NextCVar::ECVarFlags::None, "Grass contact strength", nullptr, 0.0, 2.0);
    cvars.RegisterFloat("nextdayz.grass.recoverySeconds", 1.25f, &grass_.settings.recoverySeconds,
                        NextCVar::ECVarFlags::None, "Grass recovery time after contact", nullptr, 0.3, 4.0);
    cvars.RegisterBool("nextdayz.grass.playerInteraction", true, &grassPlayerInteraction_,
                       NextCVar::ECVarFlags::None, "Let the player push grass aside");
    cvars.RegisterBool("nextdayz.grass.zombieInteraction", true, &grassZombieInteraction_,
                       NextCVar::ECVarFlags::None, "Let living infected push grass aside");

    cvars.RegisterFloat("nextdayz.player.maxStepHeight", config_.Player.MaxStepHeight,
                        &config_.Player.MaxStepHeight, NextCVar::ECVarFlags::Archive,
                        "Maximum obstacle height the player can step over; applied when the controller is created",
                        nullptr, 0.05, 0.60);
    cvars.RegisterUInt("nextdayz.seed", config_.Seed, &config_.Seed, NextCVar::ECVarFlags::StartupOnly,
                       "Deterministic seed for loot, infected AI, and product-loop replay");
    cvars.RegisterBool("nextdayz.validation.loadout", false, &validationLoadout_, NextCVar::ECVarFlags::None,
                       "Agent-only legacy weapon fixture",
                       [this]()
                       {
                           if (validationLoadout_ && GOption->AgentValidation && sceneReady_)
                           {
                               ApplyValidationLoadout();
                           }
                       });
    const auto validationReady = [this]() { return GOption->AgentValidation && sceneReady_; };
    cvars.RegisterBool("nextdayz.validation.navProfile", false, &validationNavProfile_, NextCVar::ECVarFlags::None,
                       "Agent-only: reset wall-clock frame histograms for navigation builds", [this]()
                       {
                           validationNavProfile_ = validationNavProfile_ && GOption->AgentValidation;
                           navBuildFrames_ = {};
                           navIdleFrames_ = {};
                           world_.ResetNavigationStats();
                           navProfileFrame_ = std::chrono::steady_clock::now();
                       });
    cvars.RegisterBool("nextdayz.validation.addBackpack", false, &validationAddBackpack_, NextCVar::ECVarFlags::None,
                       "Agent-only capacity fixture", [this, validationReady]()
                       {
                           if (!validationAddBackpack_ || !validationReady()) return;
                           NextDayz::FItemInstanceId id = 0;
                           if (inventory_.TryAdd("backpack", "Backpack", NextDayz::EItemKind::Clothing, 1, &id))
                           {
                               inventory_.TryEquip(id);
                               SyncClothingVisuals();
                           }
                       });
    cvars.RegisterBool("nextdayz.validation.addSupplies", false, &validationAddSupplies_, NextCVar::ECVarFlags::None,
                       "Agent-only food and water fixture", [this, validationReady]()
                       {
                           if (!validationAddSupplies_ || !validationReady()) return;
                           inventory_.TryAdd("food_can", "Canned Food", NextDayz::EItemKind::Consumable, 1);
                           inventory_.TryAdd("water_bottle", "Water Bottle", NextDayz::EItemKind::Consumable, 1);
                       });
    cvars.RegisterBool("nextdayz.validation.spawnZombie", false, &validationSpawnZombie_, NextCVar::ECVarFlags::None,
                       "Agent-only infected fixture", [this, validationReady]()
                       {
                           if (!validationSpawnZombie_ || !validationReady()) return;
                           glm::vec3 ahead = player_.Forward();
                           ahead.y = 0.0f;
                           ahead = glm::length(ahead) > 1.0e-3f ? glm::normalize(ahead) : glm::vec3(0, 0, 1);
                           // Faces the player so the fixture starts from a known pose.
                           validationZombie_ = SpawnZombieAt(NextDayz::EZombieProfile::Civilian,
                                                             player_.Position() + ahead * validationZombieDistance_);
                       });
    cvars.RegisterBool("nextdayz.validation.useWater", false, &validationUseWater_, NextCVar::ECVarFlags::None,
                       "Agent-only use-action fixture", [this, validationReady]()
                       {
                           if (!validationUseWater_ || !validationReady() || actions_.IsActive()) return;
                           const auto bottle = std::find_if(inventory_.Instances().begin(), inventory_.Instances().end(),
                               [](const NextDayz::FItemInstance& item) { return item.defId == "water_bottle"; });
                           if (bottle != inventory_.Instances().end())
                           {
                               actions_.BeginUse(NextDayz::EPlayerAction::Drink, bottle->instanceId);
                           }
                       });
    cvars.RegisterBool("nextdayz.validation.finishZombie", false, &validationFinishZombie_, NextCVar::ECVarFlags::None,
                       "Agent-only deterministic combat commit", [this, validationReady]()
                       {
                           if (!validationFinishZombie_ || !validationReady()) return;
                           for (const NextDayz::FZombieRuntime& zombie : world_.zombies.Slots())
                           {
                               if (zombie.active && zombie.state != NextDayz::EZombieState::Dead)
                               {
                                   world_.combat.ProcessMelee(++validationAttackSequence_, zombie.handle, 200.0f,
                                                        NextDayz::EHitZone::Head);
                                   break;
                               }
                           }
                       });
    cvars.RegisterFloat("nextdayz.validation.damagePlayer", 0.0f, &validationDamagePlayer_,
                        NextCVar::ECVarFlags::None, "Agent-only player damage fixture",
                        [this, validationReady]()
                        {
                            if (validationDamagePlayer_ > 0.0f && validationReady())
                            {
                                // Online the body is the server's: ask it (it must allow cheats).
                                if (NetOwnsSurvival()) NetSurvivalAction(3, 0, validationDamagePlayer_);
                                else survival_.ApplyDamage(validationDamagePlayer_, "validation", false);
                            }
                        }, 0.0, 200.0);
    cvars.RegisterFloat("nextdayz.validation.hunger", -1.0f, &validationHunger_, NextCVar::ECVarFlags::None,
                        "Agent-only hunger setter", [this, validationReady]()
                        {
                            if (validationHunger_ >= 0.0f && validationReady())
                            {
                                const auto& state = survival_.Snapshot();
                                survival_.SetNeeds(state.health, validationHunger_, state.hydration);
                            }
                        }, -1.0, 100.0);
    cvars.RegisterFloat("nextdayz.validation.hydration", -1.0f, &validationHydration_, NextCVar::ECVarFlags::None,
                        "Agent-only hydration setter", [this, validationReady]()
                        {
                            if (validationHydration_ >= 0.0f && validationReady())
                            {
                                const auto& state = survival_.Snapshot();
                                survival_.SetNeeds(state.health, state.hunger, validationHydration_);
                            }
                        }, -1.0, 100.0);
    cvars.RegisterFloat("nextdayz.validation.lookYaw", 0.0f, &validationLookYaw_, NextCVar::ECVarFlags::None,
                        "Agent-only: set the camera yaw (degrees)",
                        [this]() { player_.SetYaw(glm::radians(validationLookYaw_)); }, -360.0, 360.0);
    cvars.RegisterFloat("nextdayz.validation.lookPitch", 0.0f, &validationLookPitch_, NextCVar::ECVarFlags::None,
                        "Agent-only: set the camera pitch (degrees, + looks down)",
                        [this]() { player_.SetPitch(glm::radians(validationLookPitch_)); }, -80.0, 80.0);
    cvars.RegisterBool("nextdayz.validation.thirdPerson", false, &validationThirdPerson_,
                       NextCVar::ECVarFlags::None, "Agent-only: force third (true) or first (false) person",
                       [this]() { player_.SetFirstPerson(!validationThirdPerson_); });
    // In-game debugger (F5). The knobs live in the debugger so its window and
    // these cvars edit the same values.
    NextDayz::Debug::FKnobs& knobs = debugger_.Knobs();
    cvars.RegisterBool("nextdayz.debug.orbit", false, &knobs.orbit, NextCVar::ECVarFlags::None,
                       "Orbit camera around the player rig instead of the gameplay camera");
    cvars.RegisterFloat("nextdayz.debug.orbitYaw", knobs.orbitYaw, &knobs.orbitYaw, NextCVar::ECVarFlags::None,
                        "Orbit camera angle in degrees from behind the body (+ toward its right)", nullptr,
                        -360.0, 360.0);
    cvars.RegisterFloat("nextdayz.debug.orbitDistance", knobs.orbitDistance, &knobs.orbitDistance,
                        NextCVar::ECVarFlags::None, "Orbit camera distance (m)", nullptr, 0.5, 20.0);
    cvars.RegisterFloat("nextdayz.debug.orbitHeight", knobs.orbitHeight, &knobs.orbitHeight,
                        NextCVar::ECVarFlags::None, "Orbit camera look-at height above the feet (m)", nullptr, 0.0,
                        3.0);
    cvars.RegisterBool("nextdayz.debug.orbitZombie", false, &knobs.orbitZombie, NextCVar::ECVarFlags::None,
                       "Orbit camera centres on the focus infected: the debugger selection, else the last "
                       "validation spawn, else the first active");
    cvars.RegisterFloat("nextdayz.debug.zombieTimeScale", 1.0f, &knobs.zombieTimeScale, NextCVar::ECVarFlags::None,
                        "Slow-motion for the infected simulation and animation (0 freezes them)", nullptr, 0.0,
                        2.0);
    cvars.RegisterFloat("nextdayz.debug.fxTimeScale", 1.0f, &knobs.fxTimeScale, NextCVar::ECVarFlags::None,
                        "Slow-motion for combat effects (0 freezes flashes, tracers and blood in place)", nullptr,
                        0.0, 2.0);
    cvars.RegisterFloat("nextdayz.debug.timeScale", 1.0f, &knobs.timeScale, NextCVar::ECVarFlags::None,
                        "Scale the whole simulation (0 stops it; the debugger's time slider)", nullptr, 0.0, 8.0);
    cvars.RegisterBool("nextdayz.debug.freeze", false, &knobs.frozen, NextCVar::ECVarFlags::None,
                       "Freeze the simulation (F10); nextdayz.debug.step advances it");
    cvars.RegisterInt("nextdayz.debug.step", 0, &debugStepCvar_, NextCVar::ECVarFlags::None,
                      "Freeze and advance the simulation this many 1/60 s frames", [this]()
                      {
                          if (debugStepCvar_ > 0)
                          {
                              debugger_.Knobs().frozen = true;
                              debugger_.Knobs().stepFrames += debugStepCvar_;
                          }
                      }, 0, 600);
    cvars.RegisterBool("nextdayz.debug.god", false, &knobs.god, NextCVar::ECVarFlags::None,
                       "Infected blows still land (feedback) but do no damage");
    cvars.RegisterBool("nextdayz.debug.freezeNeeds", false, &knobs.freezeNeeds, NextCVar::ECVarFlags::None,
                       "Stop hunger, thirst, bleeding and cold");
    cvars.RegisterBool("nextdayz.debug.panel", false, &debugPanelCvar_, NextCVar::ECVarFlags::None,
                       "Show the in-game debugger (same as F5)",
                       [this]() { debugger_.SetVisible(debugPanelCvar_); });
    cvars.RegisterString("nextdayz.debug.tab", "", &debugTabCvar_, NextCVar::ECVarFlags::None,
                         "Debugger tab: overview, player, survival, inventory, weapons, infected, spawns, loot, "
                         "world, combat, log",
                         [this]()
                         {
                             if (const std::optional<NextDayz::Debug::ETab> tab = NextDayz::Debug::ParseTab(debugTabCvar_))
                             {
                                 debugger_.RequestTab(*tab);
                             }
                         });
    cvars.RegisterString("nextdayz.debug.layers", "", &debugLayersCvar_, NextCVar::ECVarFlags::None,
                         "World overlay layers: infected,perception,hitboxes,traces,noise,spawns,loot,zones,nav,"
                         "anchors,player | all | none | +layer / -layer to edit",
                         [this]() { debugger_.ApplyLayerString(debugLayersCvar_); });
    cvars.RegisterString("nextdayz.debug.select", "", &debugSelectCvar_, NextCVar::ECVarFlags::None,
                         "Debugger selection: nearest | z<slot> | l<loot index> | s<spawn point> | none",
                         [this]() { ApplyDebugSelection(debugSelectCvar_); });
    cvars.RegisterString("nextdayz.debug.expedition", "", &debugExpeditionCvar_, NextCVar::ECVarFlags::None,
                         "Expedition shortcuts: return, save, reload, spawn, alpha, bravo, rescue-kit", [this]()
                         {
                             if (const auto action = NextDayz::Debug::ParseExpeditionAction(
                                     std::exchange(debugExpeditionCvar_, "")))
                                 DebugExpeditionAction(*action);
                         });
    cvars.RegisterBool("nextdayz.debug.dump", false, &debugDumpCvar_, NextCVar::ECVarFlags::None,
                       "Write every system's state to debug/nextdayz_snapshot.json (setting it to true writes once)",
                       [this]()
                       {
                           if (debugDumpCvar_ && sceneReady_)
                           {
                               debugger_.WriteSnapshot();
                           }
                           debugDumpCvar_ = false;
                       });
    cvars.RegisterFloat("nextdayz.debug.hour", -1.0f, &debugHour_, NextCVar::ECVarFlags::None,
                        "Jump the clock to this hour (0..24)", [this]()
                        {
                            if (debugHour_ >= 0.0f) time_.SetHour(debugHour_);
                        }, -1.0, 24.0);
    cvars.RegisterInt("nextdayz.debug.weather", -1, &debugWeather_, NextCVar::ECVarFlags::None,
                      "Force the weather: 0 clear, 1 overcast, 2 fog", [this]()
                      {
                          if (debugWeather_ >= 0) time_.SetWeather(static_cast<NextDayz::EWeather>(debugWeather_));
                      }, -1, 2);
    const auto applyFog = [this]()
    {
        time_.SetFogTuning(debugFogRadiance_ >= 0.0f ? debugFogRadiance_ : config_.Time.FogDayRadiance,
                           debugFogDensityScale_);
    };
    cvars.RegisterFloat("nextdayz.debug.fogRadiance", -1.0f, &debugFogRadiance_, NextCVar::ECVarFlags::None,
                        "Override the daytime fog colour radiance (-1 = tuned default)", applyFog, -1.0, 500.0);
    cvars.RegisterFloat("nextdayz.debug.fogDensityScale", 1.0f, &debugFogDensityScale_, NextCVar::ECVarFlags::None,
                        "Multiply the height fog density", applyFog, 0.0, 10.0);
    cvars.RegisterFloat("nextdayz.validation.zombieDistance", 2.2f, &validationZombieDistance_,
                        NextCVar::ECVarFlags::None, "Agent-only: distance spawnZombie places the infected ahead",
                        nullptr, 0.5, 60.0);
    cvars.RegisterInt("nextdayz.validation.hitZombieZone", 0, &validationHitZombieZone_, NextCVar::ECVarFlags::None,
                      "Agent-only: fire one AK round from the eye at the first infected's 1 head / 2 torso / "
                      "3 arm / 4 leg hitbox",
                      [this, validationReady]()
                      {
                          if (validationHitZombieZone_ >= 1 && validationHitZombieZone_ <= 4 && validationReady())
                          {
                              ValidationHitZombie(static_cast<NextDayz::EHitZone>(validationHitZombieZone_ - 1));
                          }
                      }, 0, 4);
    cvars.RegisterBool("nextdayz.validation.invulnerable", false, &validationInvulnerable_,
                       NextCVar::ECVarFlags::None,
                       "Agent-only: infected blows still land (feedback) but do no damage");
    cvars.RegisterInt("nextdayz.validation.bleed", 0, &validationBleed_, NextCVar::ECVarFlags::None,
                      "Agent-only: open this many bleeding wounds", [this, validationReady]()
                      {
                          if (validationReady()) survival_.SetBleeding(validationBleed_);
                      }, 0, 3);
    cvars.RegisterString("nextdayz.validation.give", "", &validationGive_, NextCVar::ECVarFlags::None,
                         "Agent-only: add an item by id (\"id\" or \"id:count\")", [this, validationReady]()
                         {
                             if (validationGive_.empty() || !validationReady()) return;
                             std::string id = validationGive_;
                             int count = 1;
                             if (const size_t colon = id.find(':'); colon != std::string::npos)
                             {
                                 count = std::max(1, std::atoi(id.c_str() + colon + 1));
                                 id.resize(colon);
                             }
                             if (const NextDayz::FItemDef* definition = NextDayz::FindItemDef(id))
                             {
                                 inventory_.TryAdd(id, std::string(definition->displayName), definition->kind, count);
                             }
                         });
    cvars.RegisterString("nextdayz.validation.use", "", &validationUse_, NextCVar::ECVarFlags::None,
                         "Agent-only: use (eat, wear, equip, tune in...) the first item with this id",
                         [this, validationReady]()
                         {
                             if (validationUse_.empty() || !validationReady()) return;
                             if (const NextDayz::FItemInstance* instance = inventory_.FindFirst(validationUse_))
                             {
                                 UseInventoryItem(instance->instanceId);
                             }
                         });
    cvars.RegisterString("nextdayz.validation.equip", "", &validationEquip_, NextCVar::ECVarFlags::None,
                         "Agent-only: equip an owned weapon in slot 1..3, e.g. 2:mosin; slot alone unequips",
                         [this, validationReady]()
                         {
                             if (!validationReady() || validationEquip_.empty()) return;
                             const int slot = validationEquip_[0] - '1';
                             if (slot < 0 || slot >= NextDayz::kWeaponSlotCount) return;
                             if (validationEquip_.size() == 1) { weapons_.Unequip(slot); return; }
                             if (validationEquip_[1] != ':') return;
                             if (const auto* instance = inventory_.FindFirst(validationEquip_.substr(2)))
                                 EquipWeaponInstance(instance->instanceId, slot);
                         });
    cvars.RegisterString("nextdayz.validation.attachment", "", &validationAttachment_, NextCVar::ECVarFlags::None,
                         "Agent-only: mount e.g. 1:optic_red_dot; detach with 1:-0 (slot index 0..4)",
                         [this, validationReady]()
                         {
                             if (!validationReady() || validationAttachment_.size() < 3 || validationAttachment_[1] != ':') return;
                             const int slot = validationAttachment_[0] - '1';
                             if (slot < 0 || slot >= NextDayz::kWeaponSlotCount) return;
                             const auto weapon = weapons_.SlotInstance(slot);
                             const auto part = validationAttachment_.substr(2);
                             if (part.size() == 2 && part[0] == '-' && part[1] >= '0' && part[1] <= '4')
                                 RemoveAttachment(weapon, static_cast<NextDayz::EAttachmentSlot>(part[1] - '0'));
                             else if (const auto* instance = inventory_.FindFirst(part))
                                 InstallAttachment(weapon, instance->instanceId);
                         });
    cvars.RegisterString("nextdayz.validation.teleport", "", &validationTeleport_, NextCVar::ECVarFlags::None,
                         "Agent-only: move the player to map coordinates \"x,y\" (SCAD space: +y north)",
                         [this, validationReady]()
                         {
                             if (validationTeleport_.empty() || !validationReady()) return;
                             const size_t comma = validationTeleport_.find(',');
                             if (comma == std::string::npos) return;
                             const float x = static_cast<float>(std::atof(validationTeleport_.c_str()));
                             const float z = -static_cast<float>(std::atof(validationTeleport_.c_str() + comma + 1));
                             float groundY = 0.0f;
                             GetEngine().RayCast(glm::vec3(x, config_.Loot.SpawnRayHeight, z), glm::vec3(0.0f, -1.0f, 0.0f),
                                                 [&](Assets::RayCastResult result)
                                                 {
                                                     if (result.Hit) groundY = static_cast<float>(result.HitPoint.y);
                                                     return true;
                                                 });
                             TeleportPlayer(glm::vec3(x, groundY, z));
                         });
    cvars.RegisterString("nextdayz.validation.drop", "", &validationDrop_, NextCVar::ECVarFlags::None,
                         "Agent-only: drop the first item with this id at the player's feet",
                         [this, validationReady]()
                         {
                             if (validationDrop_.empty() || !validationReady()) return;
                             if (const NextDayz::FItemInstance* instance = inventory_.FindFirst(validationDrop_))
                             {
                                 DropItem(instance->instanceId, -1);
                             }
                         });
    cvars.RegisterBool("nextdayz.validation.restart", false, &validationRestart_, NextCVar::ECVarFlags::None,
                       "Agent-only in-process restart", [this, validationReady]()
                       {
                           if (validationRestart_ && validationReady()) ResetSession();
                       });
    cvars.RegisterBool("nextdayz.validation.preloadItemIcons", false, &validationPreloadItemIcons_, NextCVar::ECVarFlags::None,
                       "Agent-only: request every catalog item's runtime thumbnail", [this, validationReady]()
                       {
                           if (!validationPreloadItemIcons_ || !validationReady()) return;
                           auto& icons = NextDayz::GetItemIconCache(GetEngine().GetRenderer());
                           for (const auto& def : NextDayz::AllItemDefs()) icons.Request(def.id, loot_.VisualForItem(def.id));
                       });
}

void NextDayzGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                              std::vector<Assets::Model>& models,
                                              std::vector<Assets::FMaterial>& materials,
                                              std::vector<Assets::LightObject>& /*lights*/,
                                              std::vector<Assets::AnimationTrack>& /*tracks*/)
{
    cabinScene_ = loadingCabin_;
    if (cabinScene_)
    {
        rig_.InjectAssets(models, materials);
        InjectWeaponCatalog(models, materials);
        InjectCabinLights(nodes, models, materials);
        return;
    }
    LoadFieldAssets();
    grass_.Prepare(nodes, models, materials);
    rescueHelicopter_.InjectAssets(models, materials);
    rig_.InjectAssets(models, materials);
    for (auto& remote : remoteRigs_) remote->InjectAssets(models, materials);
    {
        // The survivor's forearms (rig parts) plus first-person hand models.
        NextDayz::FFirstPersonArmAssets arms = rig_.FirstPersonArmAssets();
        arms.handsReady = fpsHandsLoaded_;
        for (size_t side = 0; side < 2 && arms.handsReady; ++side)
        {
            const int32_t bone = fpsHandsAsset_.FindBone(side == 0 ? "bone_fps_hand_grip" : "bone_fps_hand_support");
            const auto part = std::find_if(fpsHandsAsset_.parts.begin(), fpsHandsAsset_.parts.end(),
                                           [bone](const auto& entry) { return entry.bone == bone && entry.modelIndex >= 0; });
            if (bone < 0 || part == fpsHandsAsset_.parts.end())
            {
                SPDLOG_ERROR("[NextDayz] first-person hand model {} missing", side);
                arms.handsReady = false;
                break;
            }
            models.push_back(fpsHandsAsset_.partModels[part->modelIndex]);
            arms.handModels[side] = static_cast<uint32_t>(models.size() - 1);
            arms.handMaterials[side].fill(0);
            for (size_t section = 0; section < part->sectionColors.size() && section < 16; ++section)
                arms.handMaterials[side][section] = Assets::SceneBuilder::AddRigSectionMaterial(materials, *part, section);
        }
        weapons_.SetFirstPersonArmAssets(std::move(arms));
    }
    effects_.InjectAssets(models, materials);
    InjectLootCatalog(models, materials);

    zombieRigPartModelIds_.clear();
    zombieRigPartMaterialIds_.clear();
    if (zombieRigLoaded_)
    {
        for (const Assets::FRigPart& part : zombieRigAsset_.parts)
        {
            models.push_back(zombieRigAsset_.partModels[part.modelIndex]);
            zombieRigPartModelIds_.push_back(static_cast<uint32_t>(models.size() - 1));
            std::array<uint32_t, 16> sectionMaterials{};
            for (size_t section = 0;
                 section < part.sectionColors.size() && section < sectionMaterials.size(); ++section)
            {
                if (!part.sectionTintable[section])
                {
                    sectionMaterials[section] =
                        Assets::SceneBuilder::AddRigSectionMaterial(materials, part, section);
                }
            }
            zombieRigPartMaterialIds_.push_back(sectionMaterials);
        }
        for (size_t shirt = 0; shirt < zombieTintMaterialIds_.size(); ++shirt)
        {
            zombieTintMaterialIds_[shirt] = Assets::SceneBuilder::AddLambertianMaterial(
                materials, kInfectedShirts[shirt % kInfectedShirts.size()]);
        }
    }

    InjectWeaponCatalog(models, materials);
}

void NextDayzGameInstance::InjectWeaponCatalog(std::vector<Assets::Model>& models,
                                               std::vector<Assets::FMaterial>& materials)
{
    for (size_t i = 0; i < NextDayz::kWeapons.size(); ++i)
    {
        const std::string_view weaponId = NextDayz::kWeapons[i].id;
        const Assets::FRigPart* model = nullptr;
        if (weaponCatalogLoaded_)
        {
            const int32_t bone = weaponCatalogAsset_.FindBone(fmt::format("bone_wpn_{}", weaponId));
            int partCount = 0;
            for (const Assets::FRigPart& part : weaponCatalogAsset_.parts)
            {
                if (bone >= 0 && part.bone == bone && part.modelIndex >= 0 &&
                    part.modelIndex < static_cast<int32_t>(weaponCatalogAsset_.partModels.size()))
                {
                    model = model ? model : &part;
                    ++partCount;
                }
            }
            if (partCount > 1)
            {
                // A part holds at most 16 material sections; the render node takes one model.
                SPDLOG_WARN("[NextDayz] weapon '{}' uses more than 16 materials; only its first part is shown",
                            weaponId);
            }
        }

        weaponMaterialIds_[i] = {};
        if (model)
        {
            models.push_back(weaponCatalogAsset_.partModels[model->modelIndex]);
            for (size_t section = 0; section < model->sectionColors.size() && section < weaponMaterialIds_[i].size();
                 ++section)
            {
                weaponMaterialIds_[i][section] = Assets::SceneBuilder::AddRigSectionMaterial(materials, *model, section);
            }
        }
        else
        {
            SPDLOG_WARN("[NextDayz] weapon catalog has no model for '{}'; using a placeholder box", weaponId);
            models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.02f, -0.03f, -0.30f),
                                                           glm::vec3(0.02f, 0.05f, 0.50f)));
            weaponMaterialIds_[i][0] = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.2f));
        }
        weaponModelIds_[i] = static_cast<uint32_t>(models.size() - 1);
    }
    weapons_.SetViewModelAssets(weaponModelIds_, weaponMaterialIds_);
    rig_.SetWeaponAssets(weaponModelIds_, weaponMaterialIds_);
    for (auto& remote : remoteRigs_) remote->SetWeaponAssets(weaponModelIds_, weaponMaterialIds_);
    NextDayz::FAttachmentVisualAssets attachments;
    attachments.ready = attachmentCatalogLoaded_;
    for (size_t i = 0; i < attachments.models.size() && attachments.ready; ++i)
    {
        const std::string_view id = i < NextDayz::kAttachments.size() ? NextDayz::kAttachments[i].id : "svd_scope";
        const int32_t bone = attachmentCatalogAsset_.FindBone(fmt::format("bone_att_{}", id));
        const auto part = std::find_if(attachmentCatalogAsset_.parts.begin(), attachmentCatalogAsset_.parts.end(),
                                     [bone](const auto& entry) { return entry.bone == bone && entry.modelIndex >= 0; });
        if (bone < 0 || part == attachmentCatalogAsset_.parts.end())
        {
            SPDLOG_ERROR("[NextDayz] attachment catalog missing '{}'", id);
            attachments.ready = false;
            break;
        }
        models.push_back(attachmentCatalogAsset_.partModels[part->modelIndex]);
        attachments.models[i] = static_cast<uint32_t>(models.size() - 1);
        for (size_t section = 0; section < part->sectionColors.size() && section < 16; ++section)
            attachments.materials[i][section] = Assets::SceneBuilder::AddRigSectionMaterial(materials, *part, section);
    }
    weapons_.SetAttachmentAssets(attachments);
    rig_.SetAttachmentAssets(attachments);
    for (auto& remote : remoteRigs_) remote->SetAttachmentAssets(attachments);
}

glm::vec3 NextDayzGameInstance::ResolveSpawnPosition() const
{
    // Prefer spawning on the same ground as nearby supplies: a plain downward
    // raycast at the gas station hits the canopy roof, not the pad. Using a loot
    // item's world position gives real ground height and puts the player facing
    // it (loot is placed a few metres ahead along +Z, our default yaw).
    if (netMode_ == ENetMode::Client)
    {
        if (const std::optional<glm::vec3> spawn = NetworkSpawnPosition()) return *spawn;
    }
    glm::vec3 lootPos;
    if (loot_.NearestLootPos(glm::vec2(config_.SpawnXZ.x, config_.SpawnXZ.z), lootPos))
    {
        const glm::vec3 spawn(lootPos.x, lootPos.y + 1.0f, lootPos.z - 3.0f);
        SPDLOG_INFO("[NextDayz] spawn near loot -> ({:.1f}, {:.1f}, {:.1f})", spawn.x, spawn.y, spawn.z);
        return spawn;
    }

    // Fallback: downward raycast (open terrain with no loot nearby).
    const glm::vec3 origin(config_.SpawnXZ.x, config_.Loot.SpawnRayHeight, config_.SpawnXZ.z);
    float groundY = 2.0f;
    GetEngine().RayCast(origin, glm::vec3(0.0f, -1.0f, 0.0f), [&](Assets::RayCastResult result) {
        if (result.Hit)
        {
            groundY = static_cast<float>(result.HitPoint.y);
        }
        return true;
    });
    return glm::vec3(config_.SpawnXZ.x, groundY + 1.0f, config_.SpawnXZ.z);
}

std::optional<glm::vec3> NextDayzGameInstance::NetworkSpawnPosition() const
{
    // Every survivor of a server starts at the map's first safe deployment
    // point, so they meet; each stands on its own spot of a small ring.
    const auto safe = world_.anchors.Find(NextDayz::EWorldAnchorType::PlayerSafe);
    const NextDayz::FWorldAnchor* anchor = safe.empty() ? nullptr : world_.anchors.Resolve(safe.front());
    if (!anchor)
    {
        return std::nullopt;
    }
    const float angle = static_cast<float>(LocalSurvivorId()) * 2.4f;
    glm::vec3 feet = anchor->worldPos + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * 2.5f;
    GetEngine().RayCast(feet + glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
                        [&](Assets::RayCastResult result)
                        {
                            if (result.Hit && result.T < 6.0f) feet.y = static_cast<float>(result.HitPoint.y);
                            return true;
                        });
    return feet + glm::vec3(0.0f, 0.1f, 0.0f);
}

void NextDayzGameInstance::OnSceneLoaded()
{
    if (cabinScene_)
    {
        rig_.OnSceneLoaded(GetEngine().GetScene());
        sceneReady_ = true;
        cabinSeconds_ = 0.0f;
        ApplyCabinEnvironment();
        BindCabinCamera();
        switchingCampaignContext_ = false;
        if (CampaignEnabled()) InitializeCabinCampaign();
        SyncClothingVisuals();
        TickCabin(0.0f);
        SetMouseCaptured(false);
        SPDLOG_INFO("[NextDayz] helicopter lobby ready ({} nodes); expedition assets deferred",
                    GetEngine().GetScene().Nodes().size());
        return;
    }
    if (!loadingMapPath_.empty()) campaignMap_ = std::exchange(loadingMapPath_, "");
    switchingCampaignContext_ = false;
    debugger_.OnSessionStart("scene loaded");
    world_.anchors.Scan(GetEngine());
    world_.ResetPopulation(sessionRng_.Derive(0x5A4F4D42ULL).Seed());
    zombieVisuals_.Reset();
    // Loot scan first so the spawn can be derived from real supply ground height.
    loot_.OnSceneLoaded(GetEngine().GetScene(), CollectLootSpots());

    const glm::vec3 spawn = ResolveSpawnPosition();
    player_.Create(GetEngine().GetPhysicsEngine(), spawn, config_);

    rig_.OnSceneLoaded(GetEngine().GetScene());
    for (auto& remote : remoteRigs_)
    {
        remote->OnSceneLoaded(GetEngine().GetScene());
        remote->SetVisible(false);
    }
    weapons_.OnSceneLoaded(GetEngine());
    effects_.OnSceneLoaded(GetEngine().GetScene());
    CreateZombieVisuals();
    time_.Reset(config_.Time, sessionRng_.Derive(0x57454154ULL).Seed());
    time_.Tick(0.0, GetEngine().GetScene());

    // Product start: pockets, one emergency bandage, and an empty bottle. Weapon
    // regression scripts opt into nextdayz.validation.loadout explicitly.
    inventory_.Clear();
    actions_.Reset();
    survival_.Reset();
    inventory_.TryAdd("bandage", "Bandage", NextDayz::EItemKind::Consumable, 1);
    inventory_.TryAdd("water_bottle_empty", "Empty Bottle", NextDayz::EItemKind::Misc, 1);
    StartNewLife();
    if ((validationLoadout_ && GOption->AgentValidation) || (netMode_ == ENetMode::Client && netKitCvar_ && !NetUsesMaster()))
    {
        ApplyValidationLoadout();
    }

    SyncClothingVisuals();

    showInventory_ = false;
    paused_ = false;
    survivalSeconds_ = 0.0;
    SetMouseCaptured(true);
    sceneReady_ = true;
    rescueHelicopter_.OnSceneLoaded(GetEngine().GetScene());
    InitializeCampaign();
    BuildZombieNavigation();
    ++geographySceneGeneration_;
    grass_.Bind(GetEngine().GetScene());
    grass_.Tick(GetEngine().GetScene(), 0.0f, player_.Position(), time_.Storm());
    SPDLOG_INFO("[NextDayz] scene loaded ({} nodes)", GetEngine().GetScene().Nodes().size());
    NetAfterSceneLoaded();
    switchingCampaignContext_ = false;
}

void NextDayzGameInstance::OnSceneUnloaded()
{
    // The worker samples this scene's immutable geometry and terrain. Join it
    // before either is destroyed or a different scene is installed.
    world_.JoinNavigation();
    if (sceneReady_ && !pendingCampaign_ && !switchingCampaignContext_)
    {
        SaveCampaign();
        if (CampaignEnabled()) pendingCampaign_ = CaptureCampaign();
    }
    rescueHelicopter_.Clear();
    sceneReady_ = false;
    grass_.Clear();
    player_.Destroy();
    rig_.OnSceneUnloaded();
    for (auto& remote : remoteRigs_) remote->OnSceneUnloaded();
    weapons_.OnSceneUnloaded();
    effects_.OnSceneUnloaded();
    loot_.OnSceneUnloaded();
    world_.anchors.Clear();
    inventory_.Clear();
    actions_.Reset();
    survival_.Reset();
    world_.ResetPopulation(sessionRng_.Derive(0x5A4F4D42ULL).Seed());
    world_.ClearSurvivors();
    world_.ClearNavigation();
    zombieVisuals_.Reset();
    zombieVisualOwners_.clear();
    zombieRigVisuals_.clear();
    zombieAudio_.clear();
    notifications_.clear();
    firecrackers_.clear();
}

void NextDayzGameInstance::OnDestroy()
{
    if (settingsDirty_ && !GOption->AgentValidation && !GetEngine().GetCVarSystem().SaveUserFiles())
        SPDLOG_WARN("[NextDayz] failed to save settings on exit");
    ShutdownNetwork();
    world_.JoinNavigation();
    if (sceneReady_) SaveCampaign();
    player_.Destroy();
}

void NextDayzGameInstance::OnTick(double deltaSeconds)
{
    if (validationNavProfile_)
    {
        const auto now = std::chrono::steady_clock::now();
        const double frameMs = std::chrono::duration<double, std::milli>(now - navProfileFrame_).count();
        navProfileFrame_ = now;
        (world_.NavigationBuilding() ? navBuildFrames_ : navIdleFrames_).Record(frameMs);
    }
    debugger_.BeginFrame();
    PumpRetiredNetworkSessions();
    TickMapAuthoring();
    if (netMode_ != ENetMode::Offline)
    {
        NetPoll();
    }
    if (switchingCampaignContext_) return;
    if (netMode_ == ENetMode::Server)
    {
        // The map's world, with every client's survivor in it; nobody plays here.
        if (sceneReady_) TickServer(static_cast<float>(deltaSeconds));
        return;
    }
    if (netMode_ == ENetMode::Client && NetInLobby())
    {
        TickLobby(static_cast<float>(deltaSeconds));
    }
    if (sceneReady_ && cabinScene_)
    {
        TickCabin(static_cast<float>(deltaSeconds));
        return;
    }
    if (!sceneReady_ || !player_.IsValid())
    {
        return;
    }
    float dt = static_cast<float>(deltaSeconds);
    if (netMode_ == ENetMode::Client)
    {
        if (NetInLobby())
        {
            // Keep the expedition frozen until the cabin scene is committed.
            return;
        }
        NetClientSend(dt);
    }
    if (CampaignEnabled())
    {
        if (!campaign_.inField || campaignSaveBlocked_)
        {
            rig_.SetVisible(false);
            weapons_.SetTriggerDown(false);
            weapons_.SetPresentationSuppressed(true);
            return;
        }
        if (!survival_.IsAlive())
        {
            ReturnToSafehouse(ESafehouseReturn::Death);
            return;
        }
        if (campaign_.flight.boarded)
        {
            if (!paused_) AdvanceExtraction(dt);
            return;
        }
    }
    if (paused_ || settingsOpen_ || sessionMenuOpen_)
    {
        weapons_.SetTriggerDown(false);
        return;
    }
    // The debugger can freeze, single-step or rescale the whole simulation.
    if (!debugger_.BeginTick(dt))
    {
        return;
    }
    {
        NextDayz::Debug::ScopedTimer timer(debugger_.Timers(), NextDayz::Debug::ETimer::Tick);
        SimulateFrame(dt);
    }
    debugger_.EndTick(dt);
    if (CampaignEnabled())
    {
        if (!survival_.IsAlive()) ReturnToSafehouse(ESafehouseReturn::Death);
        else AdvanceExtraction(dt);
    }
}

void NextDayzGameInstance::SimulateFrame(float dt)
{
    bool inventoryCommitted = false;
    using NextDayz::Debug::ETimer;
    using NextDayz::Debug::ScopedTimer;
    NextDayz::Debug::Profiler& timers = debugger_.Timers();
    const NextDayz::Debug::FKnobs& knobs = debugger_.Knobs();
    if (survival_.IsAlive())
    {
        survivalSeconds_ += dt;
    }
    audio_.SetListener(player_.EyePosition());
    hitMarkerSeconds_ = std::max(0.0f, hitMarkerSeconds_ - dt);
    hurtSeconds_ = std::max(0.0f, hurtSeconds_ - dt);
    for (NextDayz::FHudNotification& note : notifications_)
    {
        note.age += dt;
    }
    std::erase_if(notifications_, [](const NextDayz::FHudNotification& note) { return note.age > kNotificationSeconds; });

    std::optional<ScopedTimer> section;
    section.emplace(timers, ETimer::Actions);
    actions_.Update(dt);
    if (const std::optional<NextDayz::FLootHandle> cancel = actions_.ConsumeCancelRequest())
    {
        loot_.Cancel(*cancel);
        lootDestination_ = 0;
        lootEquipSlot_ = NextDayz::EEquipSlot::None;
        lootPlacement_.reset();
    }
    if (const std::optional<NextDayz::FLootHandle> commit = actions_.ConsumeCommitRequest();
        commit && netMode_ == ENetMode::Client)
    {
        // The server decides who gets it; check here only that something would fit.
        const auto destination = std::exchange(lootDestination_, 0);
        const auto equipSlot = std::exchange(lootEquipSlot_, NextDayz::EEquipSlot::None);
        const auto placement = std::exchange(lootPlacement_, std::nullopt);
        NextDayz::Inventory trial = inventory_;
        std::vector<NextDayz::FLootStack> left;
        const bool fits = commit->index < loot_.Entries().size() &&
            !NextDayz::LootSystem::AddToInventory(loot_.Entries()[commit->index].contents, trial, destination, equipSlot,
                                                  placement, left).empty();
        if (fits)
        {
            NetRequestTake(*commit, destination, equipSlot, placement);
        }
        else
        {
            loot_.Cancel(*commit);
            Notify("Not enough space - drop something or find a bigger bag", 1);
        }
    }
    else if (commit)
    {
        const NextDayz::FLootCommitResult result = loot_.Commit(*commit, inventory_, GetEngine(),
            std::exchange(lootDestination_, 0), std::exchange(lootEquipSlot_, NextDayz::EEquipSlot::None),
            std::exchange(lootPlacement_, std::nullopt));
        if (result.taken)
        {
            inventoryCommitted = true;
            SyncClothingVisuals();
            audio_.Pickup();
            for (const NextDayz::FLootStack& stack : result.stacks)
            {
                Notify(stack.count > 1 ? NextDayz::L10n::Message("Picked up {} x{}", ItemDisplayName(stack.itemId), stack.count)
                                       : NextDayz::L10n::Message("Picked up {}", ItemDisplayName(stack.itemId)));
            }
            if (result.partial)
            {
                Notify("No room for the rest - it stays here", 1);
            }
        }
        else
        {
            Notify("Not enough space - drop something or find a bigger bag", 1);
        }
    }
    if (const std::optional<NextDayz::FItemActionCommit> commit = actions_.ConsumeItemCommitRequest())
    {
        inventoryCommitted = true;
        if (NetOwnsSurvival())
        {
            // The server applies it to the body it keeps; we show it at once.
            if (commit->action == NextDayz::EPlayerAction::DrinkFromWell) NetSurvivalAction(1);
            else if (commit->action == NextDayz::EPlayerAction::Rest) NetSurvivalAction(2);
            else if (commit->action != NextDayz::EPlayerAction::FillBottle) NetSurvivalAction(0, commit->instanceId);
        }
        if (commit->action == NextDayz::EPlayerAction::DrinkFromWell)
        {
            survival_.DrinkFromWell();
            audio_.Drink();
        }
        else if (commit->action == NextDayz::EPlayerAction::FillBottle)
        {
            if (survival_.FillBottle(inventory_, commit->instanceId))
            {
                Notify("Bottle filled with water", 3);
            }
        }
        else if (commit->action == NextDayz::EPlayerAction::Rest)
        {
            if (survival_.Rest())
            {
                Notify("You rest for a while and feel better", 3);
            }
            else
            {
                Notify("You cannot rest while bleeding", 1);
            }
        }
        else
        {
            const NextDayz::FItemInstance* instance = inventory_.FindInstance(commit->instanceId);
            const NextDayz::FItemDef* definition = instance ? NextDayz::FindItemDef(instance->defId) : nullptr;
            const NextDayz::EItemUse use = definition ? definition->use : NextDayz::EItemUse::None;
            std::string message;
            if (survival_.TryUseItem(inventory_, commit->instanceId, &message))
            {
                if (use == NextDayz::EItemUse::Eat) audio_.Eat();
                else if (use == NextDayz::EItemUse::Drink) audio_.Drink();
                else audio_.Bandage();
                if (!message.empty())
                {
                    const bool good = use == NextDayz::EItemUse::Bandage || use == NextDayz::EItemUse::FirstAid;
                    Notify(message, good ? 3 : 1);
                }
            }
        }
    }

    const bool actionLocked = actions_.IsActive();
    const bool traversalLocked = player_.IsTraversing();
    const bool dead = !survival_.IsAlive();
    section.emplace(timers, ETimer::Player);
    player_.SetMovementLocked(actionLocked || traversalLocked || showInventory_ || showMap_ || dead);
    player_.SetMeleeEquipped(weapons_.ActiveIsMelee());
    // Traversal keeps the first-person weapon: WeaponSystem tucks it down and
    // out of the way while the hands are on the obstacle.
    weapons_.SetPresentationSuppressed(actionLocked || dead || knobs.orbit);
    if (!weapons_.HasActiveWeapon() && player_.IsAds()) player_.CancelAiming();
    if (actionLocked || traversalLocked || showInventory_ || showMap_ || dead)
    {
        player_.CancelAiming();
        weapons_.SetTriggerDown(false);
        weapons_.CancelMelee();
    }
    player_.SetSprintBlocked((weapons_.TriggerDown() && weapons_.HasActiveWeapon()) || weapons_.IsSwinging() ||
                             weapons_.MeleeInputPending() || survival_.IsWeak());
    player_.Update(dt, GetEngine());
    section.emplace(timers, ETimer::Survival);
    // Cold: night and bad weather against whatever the player is wearing.
    survival_.SetCold(std::clamp(time_.Chill() - inventory_.Warmth() * 0.8f, 0.0f, 1.0f));
    if (!knobs.freezeNeeds && !NetOwnsSurvival())
    {
        survival_.Update(dt, player_.LocomotionState().gait == NextDayz::EPlayerGait::Sprint);
    }
    section.emplace(timers, ETimer::Weapons);
    weapons_.Update(dt, player_, inventory_, GetEngine());
    if (weapons_.ConsumeDryFire())
    {
        audio_.DryFire();
        debugger_.Log(NextDayz::Debug::EChannel::Weapon, 0, NextDayz::L10n::Message("Dry fire {}", weapons_.ActiveWeaponId()));
    }
    if (const NextDayz::EWeaponPresentationAction action = weapons_.PresentationAction(); action != lastWeaponAction_)
    {
        if (action == NextDayz::EWeaponPresentationAction::Reload) audio_.Reload();
        if (action == NextDayz::EWeaponPresentationAction::Switch) audio_.Switch();
        lastWeaponAction_ = action;
    }
    if (weapons_.IsReloading() || weapons_.IsSwitching())
    {
        player_.CancelAiming();
    }
    for (const NextDayz::FWeaponHitEvent& hit : weapons_.ConsumeHitEvents())
    {
        if (netMode_ == ENetMode::Client)
        {
            // The server owns the infected: it resolves the hit and tells everyone.
            NetSendWeaponHit(hit);
        }
        else if (const NextDayz::FWeaponDef* definition = NextDayz::FindWeaponDef(hit.weaponId))
        {
            world_.combat.ProcessHit(hit, definition->damageFalloffDistance);
        }
    }
    HandleDamageEvents();
    // Effects leave from the barrel the player sees: the view model in first
    // person, the rig's weapon otherwise. The hitscan itself runs from the eye.
    glm::vec3 visualMuzzle(0.0f);
    glm::vec3 visualMuzzleForward(0.0f, 0.0f, 1.0f);
    const bool firstPersonView = player_.IsFirstPerson() && !knobs.orbit;
    const bool hasVisualMuzzle = firstPersonView ? weapons_.ViewModelMuzzle(visualMuzzle, visualMuzzleForward)
                                                 : rig_.WeaponMuzzle(visualMuzzle, visualMuzzleForward);
    for (NextDayz::FWeaponTraceEvent& trace : weapons_.ConsumeTraceEvents())
    {
        // Every round of a burst gets a streak; a shotgun shows a few of its pellets.
        if (!trace.melee && trace.pellet % 3 == 0)
        {
            effects_.Tracer(hasVisualMuzzle ? visualMuzzle : trace.origin, trace.endPoint);
        }
        if (trace.hit && !trace.hitZombie)
        {
            effects_.WorldImpact(trace.endPoint, trace.normal);
            audio_.Impact(trace.endPoint);
            if (!trace.melee && trace.pellet == 0) ScoreRangeTarget(trace);
        }
        if (netMode_ == ENetMode::Client) NetCheckPvp(trace);
        debugger_.RecordTrace(trace);
    }
    for (const NextDayz::FShotEvent& shot : weapons_.ConsumeShotEvents())
    {
        lastShotMelee_ = shot.melee;
        debugger_.Log(NextDayz::Debug::EChannel::Weapon, 0,
                      NextDayz::L10n::Message("{} {}", shot.melee ? "Swung" : "Fired", shot.weaponId));
        if (shot.melee)
        {
            // Dedicated melee clips own the body and weapon; no firearm recoil.
            audio_.MeleeSwing();
            if (const NextDayz::FWeaponDef* definition = NextDayz::FindWeaponDef(shot.weaponId))
            {
                world_.noise.Emit(player_.Position(), definition->noiseRadius, 0.5f, NextDayz::ENoiseType::Melee);
            }
            continue;
        }
        const NextDayz::FWeaponDef* shotWeapon = NextDayz::FindWeaponDef(shot.weaponId);
        const NextDayz::EWeaponFamily shotFamily = shotWeapon ? shotWeapon->family : NextDayz::EWeaponFamily::Rifle;
        if (hasVisualMuzzle)
        {
            const float flash = shotFamily == NextDayz::EWeaponFamily::Sidearm ? 0.6f
                : shotFamily == NextDayz::EWeaponFamily::Smg ? 0.7f
                : shotFamily == NextDayz::EWeaponFamily::Shotgun ? 1.25f
                : shot.weaponId == "aks74u" ? 1.35f : 1.0f; // the muzzle booster's fireball
            effects_.MuzzleFlash(visualMuzzle, visualMuzzleForward, flash * (firstPersonView ? 0.55f : 0.6f) * (shot.suppressed ? 0.15f : 1.0f));
        }
        audio_.Shot(shot.weaponId, shot.suppressed);
        player_.ApplyCameraRecoil(shot.cameraImpulseRadians);
        rig_.TriggerRecoil(shot.rigRecoilScale);
        if (shotWeapon)
        {
            const NextDayz::ENoiseType noiseType = shotFamily == NextDayz::EWeaponFamily::Shotgun ? NextDayz::ENoiseType::Shotgun
                : shotFamily == NextDayz::EWeaponFamily::Sidearm || shotFamily == NextDayz::EWeaponFamily::Smg
                    ? NextDayz::ENoiseType::Pistol : NextDayz::ENoiseType::Rifle;
            world_.noise.Emit(player_.Position(), shotWeapon->noiseRadius * (shot.suppressed ? 0.25f : 1.0f), 1.0f, noiseType);
        }
    }

    const float zombieDt = dt * knobs.zombieTimeScale;
    std::vector<NextDayz::FPlayerDamageRequest> strikes;
    if (netMode_ == ENetMode::Client)
    {
        // The infected and the other survivors are the server's; ease toward them.
        section.emplace(timers, ETimer::InfectedAi);
        ApplyZombieReplication(zombieDt);
        UpdateRemoteSurvivors(dt);
        strikes = std::exchange(netStrikes_, {});
    }
    else
    {
        // The world sees this client's survivor as it stands after its own move.
        world_.UpsertSurvivor(LocalSurvivor());
        section.emplace(timers, ETimer::SpawnDirector);
        world_.TickSpawns(dt);
        section.emplace(timers, ETimer::InfectedAi);
        world_.TickInfected(zombieDt);
        strikes = world_.ConsumeSurvivorHits();
    }
    section.emplace(timers, ETimer::Survival);
    for (const NextDayz::FPlayerDamageRequest& damage : strikes)
    {
        if (damage.targetId != LocalSurvivorId())
        {
            continue;
        }
        const bool invulnerable = (validationInvulnerable_ && GOption->AgentValidation) || knobs.god;
        const int woundsBefore = survival_.Snapshot().bleedingWounds;
        // Online the server has already applied it to the body it keeps.
        const bool landed = NetOwnsSurvival() ||
            (!invulnerable && survival_.ApplyInfectedHit(damage.amount, inventory_.Armor(), survivalRng_.UnitFloat()));
        debugger_.RecordPlayerHit(damage.sourcePosition, damage.amount, landed,
                                  survival_.Snapshot().bleedingWounds > woundsBefore);
        if (invulnerable || landed)
        {
            actions_.RequestCancel();
            // Struck: the view snaps away from the blow and the HUD shows where it came from.
            const glm::vec3 toAttacker = damage.sourcePosition - player_.Position();
            hurtYawRadians_ = std::remainder(std::atan2(toAttacker.x, toAttacker.z) - player_.Yaw(),
                                             glm::two_pi<float>());
            hurtSeconds_ = kHurtSeconds;
            audio_.PlayerHurt();
            player_.ApplyCameraRecoil(glm::vec2(hurtYawRadians_ > 0.0f ? -0.035f : 0.035f, 0.045f));
        }
    }
    if (!survival_.IsAlive())
    {
        SetMouseCaptured(false);
        if (netMode_ == ENetMode::Client) NetDropOnDeath();
    }
    world_.noise.Update(dt);
    section.emplace(timers, ETimer::InfectedVisuals);
    SyncZombieVisuals(zombieDt);
    section.emplace(timers, ETimer::Presentation);
    UpdateZombieAudio(zombieDt);
    effects_.Update(dt * knobs.fxTimeScale);

    section.emplace(timers, ETimer::Loot);
    if (netMode_ == ENetMode::Client)
    {
        // The server restocks; here we only see what is under the crosshair.
        loot_.UpdateHover(player_.EyePosition(), player_.Forward());
    }
    else
    {
        loot_.Update(dt, player_.EyePosition(), player_.Forward(), GetEngine());
    }
    section.emplace(timers, ETimer::World);
    hoveredWell_ = {};
    float bestWellDot = 0.45f;
    for (const NextDayz::FWorldAnchorHandle handle : world_.anchors.Find(NextDayz::EWorldAnchorType::Well))
    {
        const NextDayz::FWorldAnchor* well = world_.anchors.Resolve(handle);
        if (!well)
        {
            continue;
        }
        const glm::vec3 delta = well->worldPos - player_.EyePosition();
        const float distance = glm::length(delta);
        if (distance > 0.01f && distance <= config_.Loot.ReachMeters)
        {
            const float dot = glm::dot(delta / distance, player_.Forward());
            if (dot > bestWellDot)
            {
                bestWellDot = dot;
                hoveredWell_ = handle;
            }
        }
    }
    time_.Tick(static_cast<double>(dt), GetEngine().GetScene());
    UpdateGrass(dt);
    UpdateWorldPressure();
    UpdateFirecrackers(dt);
    UpdateSurvivalFeedback(dt);
    audio_.UpdateAmbience(dt, static_cast<float>(time_.Hour()), time_.Daylight(), time_.Fog(), time_.Storm());
    if (lanternLit_)
    {
        lanternSeconds_ -= dt;
        if (lanternSeconds_ <= 0.0f || inventory_.CountOf("lantern") == 0)
        {
            lanternLit_ = false;
            lanternSeconds_ = std::max(lanternSeconds_, 0.0f);
            Notify("The lantern went out", 1);
        }
    }
    if (cacheMarker_)
    {
        cacheSeconds_ -= dt;
        if (glm::distance(*cacheMarker_, player_.Position()) < 4.0f)
        {
            Notify("This is the supply cache", 3);
            cacheMarker_.reset();
        }
        else if (cacheSeconds_ <= 0.0f)
        {
            Notify("The broadcast has gone silent - the cache marker is lost", 1);
            cacheMarker_.reset();
        }
    }

    section.emplace(timers, ETimer::Presentation);
    rig_.SetVisible(!player_.IsFirstPerson() || knobs.orbit || showInventory_);
    const NextDayz::FPlayerPresentationState presentation = LocalPresentation();
    rig_.Update(player_.Position(), presentation, dt);
    rig_.UpdateCameraFade(player_.CameraPosition(), config_.Camera.TpsFadeNearDistance,
                          config_.Camera.TpsFadeFarDistance, !player_.IsFirstPerson() && !knobs.orbit);
    if (inventoryCommitted) SaveCampaign();
}

NextDayz::FPlayerPresentationState NextDayzGameInstance::LocalPresentation() const
{
    NextDayz::FPlayerPresentationState presentation;
    presentation.locomotion = player_.LocomotionState();
    presentation.aimWeight = player_.IsAiming() ? 1.0f : 0.0f;
    presentation.aimPitchRadians = player_.Pitch();
    presentation.aimYawRadians = player_.Yaw();
    presentation.aimDirection = player_.Forward();
    presentation.sprintAlpha = player_.SprintAlpha();
    presentation.hasWeapon = weapons_.HasActiveWeapon();
    presentation.weaponId = weapons_.ActiveWeaponId();
    presentation.weaponAction = weapons_.PresentationAction();
    presentation.weaponActionTime01 = weapons_.PresentationActionTime();
    presentation.activeWeaponSlot = weapons_.ActiveSlot();
    presentation.switchSourceSlot = weapons_.SwitchSourceSlot();
    presentation.switchTargetSlot = weapons_.SwitchTargetSlot();
    presentation.slotWeaponIds = {
        weapons_.SlotWeaponId(0),
        weapons_.SlotWeaponId(1),
        weapons_.SlotWeaponId(2),
    };
    presentation.action = actions_.Action();
    for (int slot = 0; slot < NextDayz::kWeaponSlotCount; ++slot)
        presentation.slotAttachments[slot] = weapons_.SlotAttachments(slot);
    presentation.actionTime01 = actions_.NormalizedTime();
    return presentation;
}

bool NextDayzGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    if (cabinScene_)
    {
        // A slow drift in view space: the airframe is never perfectly still.
        const glm::vec3 sway(std::sin(cabinSeconds_ * 0.65f) * 0.006f,
                             std::sin(cabinSeconds_ * 0.41f + 1.3f) * 0.004f, 0.0f);
        outRenderCamera.ModelView = glm::translate(glm::mat4(1.0f), -sway) * cabinView_;
        outRenderCamera.FieldOfView = cabinFov_;
        outRenderCamera.Aperture = cabinAperture_;
        outRenderCamera.FocalDistance = cabinFocal_;
        outRenderCamera.NearPlane = 0.03f;
        outRenderCamera.FarPlane = 80.0f;
        return true;
    }
    if (!player_.IsValid())
    {
        return false;
    }
    float fov = config_.Camera.BaseFov;
    player_.FillCamera(outRenderCamera.ModelView, fov);
    if (CampaignEnabled() && campaign_.flight.boarded)
    {
        const glm::vec3 position = rescueHelicopter_.Position(campaign_.flight, extractionConfig_);
        outRenderCamera.ModelView = glm::lookAt(position + glm::vec3(10.0f,6.0f,-12.0f),
            position + glm::vec3(0,1.5f,0), glm::vec3(0,1,0));
    }
    const NextDayz::Debug::FKnobs& knobs = debugger_.Knobs();
    if (knobs.orbit)
    {
        float yaw = rig_.BodyYaw();
        glm::vec3 focus = player_.Position();
        if (knobs.orbitZombie)
        {
            if (const NextDayz::FZombieRuntime* zombie = FocusZombie())
            {
                focus = zombie->position;
                yaw = std::atan2(zombie->forward.x, zombie->forward.z);
            }
        }
        const glm::vec3 behind(-std::sin(yaw), 0.0f, -std::cos(yaw));
        const glm::vec3 toEye =
            glm::angleAxis(glm::radians(-knobs.orbitYaw), glm::vec3(0.0f, 1.0f, 0.0f)) * behind;
        const glm::vec3 center = focus + glm::vec3(0.0f, knobs.orbitHeight, 0.0f);
        const glm::vec3 eye = center + glm::normalize(toEye + glm::vec3(0.0f, 0.12f, 0.0f)) * knobs.orbitDistance;
        outRenderCamera.ModelView = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
        fov = config_.Camera.BaseFov;
    }
    outRenderCamera.FieldOfView = fov;
    outRenderCamera.NearPlane = player_.IsFirstPerson() && !knobs.orbit ? 0.02f : 0.1f;
    outRenderCamera.FarPlane = 1500.f;
    return true;
}

bool NextDayzGameInstance::OnRenderUI()
{
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false))
    {
        using namespace NextDayz::L10n;
        SetLanguage(Language() == ELanguage::Chinese ? ELanguage::English : ELanguage::Chinese);
    }
    if (DrawLobby())
    {
        DrawSessionMenu();
        DrawSettings();
        return true;
    }
    DrawNetStatus();
    if (!sceneReady_)
    {
        return false;
    }
    if (CampaignEnabled() && DrawSafehouse())
    {
        DrawSessionMenu();
        DrawSettings();
        return true;
    }

    NextDayz::FHudContext ctx;
    ctx.titleFont = inventoryTitleFont_;
    ctx.freeLooking = player_.IsFreeLooking();
    ctx.aiming = player_.IsAiming();
    ctx.firstPerson = player_.IsFirstPerson();
    ctx.scopeView = weapons_.IsScopeView();
    ctx.optic = weapons_.ActiveOptic();
    ctx.sightReady = weapons_.SightReady();
    {
        // Screen projection from the render camera, in pixels from the centre.
        const glm::vec3 eye = player_.CameraPosition();
        const glm::vec3 forward = player_.CameraForward();
        const glm::vec3 up = player_.CameraUp();
        const glm::vec3 right = glm::normalize(glm::cross(forward, up));
        const float focal = ImGui::GetMainViewport()->Size.y / (2.0f * std::tan(glm::radians(player_.CurrentFov()) * 0.5f));
        const auto project = [&](const glm::vec3& point, glm::vec2& out)
        {
            const glm::vec3 delta = point - eye;
            const float depth = glm::dot(delta, forward);
            if (depth <= 0.01f) return false;
            out = glm::vec2(glm::dot(delta, right), -glm::dot(delta, up)) * focal / depth;
            return true;
        };
        glm::vec3 sight, sightForward;
        const bool hasSight = weapons_.ViewModelSight(sight, sightForward);
        // Optics project their mark at infinity: where the sight axis points.
        if (ctx.sightReady && hasSight) project(sight + sightForward * 4.0f, ctx.reflexOffsetPx);
        glm::vec3 window, halfX, halfY;
        glm::vec2 windowCenter, windowX, windowY;
        if (weapons_.ViewModelReflexWindow(window, halfX, halfY) && project(window, windowCenter) &&
            project(window + halfX, windowX) && project(window + halfY, windowY))
        {
            ctx.reflexWindowValid = true;
            ctx.reflexWindowCenterPx = windowCenter;
            ctx.reflexWindowHalfPx = glm::vec2(glm::length(windowX - windowCenter), glm::length(windowY - windowCenter));
        }
        ctx.scopeBlend = weapons_.ScopeBlend();
        ctx.adsVignette = player_.IsFirstPerson() && weapons_.HasActiveWeapon() && !weapons_.ActiveIsMelee()
                              ? player_.AimBlend() * (1.0f - ctx.scopeBlend) : 0.0f;
        if (ctx.scopeBlend > 0.0f && hasSight) project(sight + sightForward * 4.0f, ctx.scopeReticlePx);
        // The eye box lags the look: turning right shades the right of the lens.
        const float lens = std::min(ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y) * 0.455f;
        const glm::vec2 sway = weapons_.SwayAngle();
        ctx.scopeEyeBoxPx = glm::clamp(glm::vec2(-sway.x, sway.y) * lens * 3.0f, glm::vec2(-lens * 0.12f), glm::vec2(lens * 0.12f));
        ctx.factoryScope = weapons_.ActiveWeaponId() == "svd" && weapons_.ActiveOptic() == NextDayz::EWeaponAttachment::None;
        // The hip crosshair gives way to the sight as the weapon comes up; in
        // third-person shoulder aim it stays and tightens.
        const bool sightPicture = player_.WantsFirstPerson() || player_.IsFirstPerson();
        ctx.crosshairAlpha = sightPicture ? 1.0f - glm::smoothstep(0.10f, 0.55f, player_.AimProgress()) : 1.0f;
        ctx.crosshairGapScale = sightPicture ? 1.0f : glm::mix(1.0f, 0.55f, player_.AimBlend());
    }
    ctx.hasWeapon = weapons_.HasActiveWeapon();
    ctx.melee = weapons_.ActiveIsMelee();
    ctx.swinging = weapons_.IsSwinging();
    ctx.swingProgress = ctx.swinging ? weapons_.PresentationActionTime() : 0.0f;
    ctx.weaponName = NextDayz::L10n::Tr(weapons_.ActiveDisplayName());
    ctx.ammoInMag = weapons_.AmmoInMag();
    ctx.ammoReserve = weapons_.AmmoReserve(inventory_);
    ctx.reloading = weapons_.IsReloading();
    ctx.activeSlot = weapons_.ActiveSlot();
    ctx.actionActive = actions_.IsActive();
    if (actions_.IsActive())
    {
        ctx.actionLabel = NextDayz::L10n::Tr(NextDayz::ActionLabel(actions_.Action()));
        ctx.actionProgress = static_cast<float>(actions_.NormalizedTime());
        ctx.interactionPrompt = fmt::format("{}...", ctx.actionLabel);
    }
    else if (loot_.HasHovered())
    {
        ctx.interactionPrompt = fmt::format("[E] Take {}", loot_.HoveredPrompt());
    }
    else if (hoveredWell_.IsValid())
    {
        ctx.interactionPrompt = inventory_.CountOf("water_bottle_empty") > 0 ? "[E] Drink   [F] Fill bottle"
                                                                              : "[E] Drink from the well";
    }
    if (CampaignEnabled() && !ExtractionPrompt().empty()) ctx.interactionPrompt = ExtractionPrompt();
    ctx.hour = time_.HourInt();
    ctx.minute = time_.MinuteInt();
    ctx.overcast = time_.Overcast();
    ctx.weather = NextDayz::L10n::Tr(NextDayz::WeatherName(time_.Weather()));
    ctx.showInventory = showInventory_;
    ctx.showMap = showMap_;
    ctx.armor = inventory_.Armor();
    ctx.warmth = inventory_.Warmth();
    ctx.weak = survival_.IsWeak();
    ctx.lanternLit = lanternLit_;
    ctx.lanternMinutes = lanternSeconds_ / 60.0f;
    ctx.kills = world_.zombies.KillCount();
    ctx.hasCompass = inventory_.HasTool(NextDayz::EItemTool::Compass);
    ctx.hasMap = inventory_.HasTool(NextDayz::EItemTool::Map);
    ctx.yawRadians = player_.Yaw();
    ctx.playerPosition = player_.Position();
    ctx.cacheMarker = cacheMarker_;
    ctx.quickSlots = quickSlots_;
    ctx.notifications = notifications_;
    if (showMap_)
    {
        for (const NextDayz::FWorldAnchor& anchor : world_.anchors.All())
        {
            if (anchor.type == NextDayz::EWorldAnchorType::Poi)
            {
                ctx.pois.push_back({anchor.label, anchor.profile, glm::vec2(anchor.worldPos.x, anchor.worldPos.z)});
            }
            else if (anchor.type == NextDayz::EWorldAnchorType::Extraction)
                ctx.pois.push_back({"[H] " + anchor.label, "extraction", glm::vec2(anchor.worldPos.x, anchor.worldPos.z)});
        }
    }
    if (showInventory_)
    {
        for (const int index : loot_.Nearby(player_.EyePosition(), config_.Loot.ReachMeters))
        {
            const NextDayz::FLootEntry& entry = loot_.Entries()[index];
            ctx.nearby.push_back({index, entry.displayName, glm::distance(entry.worldPos, player_.EyePosition())});
            if (!entry.contents.empty())
            {
                ctx.nearby.back().itemId = entry.contents.front().itemId;
                ctx.nearby.back().count = entry.contents.front().count;
            }
            for (const NextDayz::FLootStack& stack : entry.contents)
            {
                const NextDayz::FItemDef* definition = NextDayz::FindItemDef(stack.itemId);
                if (definition && definition->kind == NextDayz::EItemKind::Clothing)
                {
                    ctx.nearby.back().equipSlot = definition->equipSlot;
                    break;
                }
            }
        }
    }
    ctx.inventory = &inventory_;
    ctx.weapons = &weapons_;
    ctx.survival = survival_.Snapshot();
    ctx.paused = paused_;
    ctx.settingsOpen = settingsOpen_;
    ctx.sessionLabel = NextDayz::L10n::Tr(SessionModeLabel());
    ctx.openSettings = [this]() { OpenSettings(); };
    ctx.resumeSession = [this]() { paused_ = false; SetMouseCaptured(true); };
    ctx.survivalSeconds = survivalSeconds_;
    ctx.objective = CurrentObjective();
    ctx.hitMarker = std::min(1.0f, hitMarkerSeconds_ / kHitMarkerSeconds);
    ctx.hitMarkerKill = hitMarkerKill_;
    ctx.hitMarkerHead = hitMarkerHead_;
    ctx.hurt = hurtSeconds_ / kHurtSeconds;
    ctx.hurtYawRadians = hurtYawRadians_;
    // Run inventory mutations after drawing: their transactions replace vectors
    // that the HUD is currently iterating and holding item references into.
    std::vector<std::function<void()>> hudRequests;
    ctx.closeInventory = [this, &hudRequests]()
    {
        hudRequests.emplace_back([this]() { SetInventoryOpen(false); });
    };
    ctx.returnToLobby = [this, &hudRequests]()
    {
        hudRequests.emplace_back([this]() { ReturnToSessionLobby(); });
    };
    if (showInventory_)
    {
        ctx.itemIcon = [this](std::string_view id) -> uint64_t
        {
            auto& icons = NextDayz::GetItemIconCache(GetEngine().GetRenderer());
            const uint32_t slot = icons.Request(id, loot_.VisualForItem(id));
            return slot != std::numeric_limits<uint32_t>::max()
                ? GetEngine().GetUserInterface()->RequestImTextureIdRaw(slot) : 0;
        };
        ctx.characterPreview = [this](glm::vec2 size, float yaw, float distance) -> uint64_t
        {
            auto& preview = NextDayz::GetInventoryPreview(GetEngine().GetRenderer());
            preview.Request(rig_.WorldRoot(), {static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y)}, yaw, distance);
            return preview.IsReady() ? GetEngine().GetUserInterface()->RequestImTextureIdRaw(preview.SampleSlot()) : 0;
        };
    }
    ctx.equipWeapon = [this, &hudRequests](NextDayz::FItemInstanceId id, int slot)
    {
        hudRequests.emplace_back([this, id, slot]() { EquipWeaponInstance(id, slot); });
    };
    ctx.unequipWeapon = [this](int slot) { weapons_.Unequip(slot); };
    ctx.installAttachment = [this, &hudRequests](auto weapon, auto attachment)
    { hudRequests.emplace_back([this, weapon, attachment]() { InstallAttachment(weapon, attachment); }); };
    ctx.removeAttachment = [this, &hudRequests](auto weapon, auto slot)
    { hudRequests.emplace_back([this, weapon, slot]() { RemoveAttachment(weapon, slot); }); };
    ctx.wear = [this, &hudRequests](NextDayz::FItemInstanceId id)
    {
        hudRequests.emplace_back([this, id]() { WearItem(id); });
    };
    ctx.takeOff = [this, &hudRequests](NextDayz::FItemInstanceId id)
    {
        hudRequests.emplace_back([this, id]() { TakeOffItem(id); });
    };
    ctx.useItem = [this, &hudRequests](NextDayz::FItemInstanceId id)
    {
        hudRequests.emplace_back([this, id]() { UseInventoryItem(id); });
    };
    ctx.dropItem = [this, &hudRequests](NextDayz::FItemInstanceId id, int count)
    {
        hudRequests.emplace_back([this, id, count]() { DropItem(id, count); });
    };
    ctx.moveItem = [this, &hudRequests](NextDayz::FItemInstanceId id, NextDayz::FContainerId container)
    {
        hudRequests.emplace_back([this, id, container]() { MoveItem(id, container); });
    };
    ctx.splitItem = [this, &hudRequests](NextDayz::FItemInstanceId id)
    {
        hudRequests.emplace_back([this, id]() { SplitItem(id); });
    };
    ctx.placeItem = [this, &hudRequests](auto id, auto container, NextDayz::FGridPlacement placement)
    {
        hudRequests.emplace_back([this, id, container, placement]() { PlaceItem(id, container, placement); });
    };
    ctx.mergeItems = [this, &hudRequests](auto source, auto target)
    {
        hudRequests.emplace_back([this, source, target]()
        {
            if (!inventory_.TryMerge(source, target)) Notify("Cannot merge those stacks", 1);
        });
    };
    ctx.takeLootAt = [this, &hudRequests](int index, auto container, NextDayz::FGridPlacement placement)
    {
        hudRequests.emplace_back([this, index, container, placement]()
        { TakeLoot(index, container, NextDayz::EEquipSlot::None, placement); });
    };
    ctx.takeLoot = [this, &hudRequests](int index, NextDayz::FContainerId destination, NextDayz::EEquipSlot slot)
    {
        hudRequests.emplace_back([this, index, destination, slot]() { TakeLoot(index, destination, slot); });
    };
    ctx.bindQuick = [this](int slot, const std::string& itemId)
    {
        if (slot >= 0 && slot < static_cast<int>(quickSlots_.size()))
        {
            quickSlots_[slot] = itemId;
        }
    };
    if (!CampaignEnabled() && netMode_ == ENetMode::Offline)
        ctx.restartSession = [this, &hudRequests]() { hudRequests.emplace_back([this]() { ResetSession(); }); };
    {
        NextDayz::Debug::ScopedTimer timer(debugger_.Timers(), NextDayz::Debug::ETimer::Hud);
        NextDayz::NextDayzHUD::Draw(ctx);
    }
    for (const auto& request : hudRequests)
    {
        request();
    }
    if (!hudRequests.empty()) SaveCampaign();
    debugger_.Draw(DebugFacts());
    DrawSessionMenu();
    DrawSettings();
    return true;
}

void NextDayzGameInstance::Notify(NextDayz::L10n::FText text, int severity)
{
    // HUD severity 0 info, 1 warning, 2 danger, 3 good news -> log severity.
    debugger_.Log(NextDayz::Debug::EChannel::Hud, severity == 3 ? 0 : severity, text);
    // The same complaint twice in a row only refreshes the first one.
    if (!notifications_.empty() && notifications_.back().text == text)
    {
        notifications_.back().age = 0.0f;
        return;
    }
    notifications_.push_back({std::move(text), 0.0f, severity});
    if (notifications_.size() > 6)
    {
        notifications_.erase(notifications_.begin());
    }
}

void NextDayzGameInstance::StartNewLife()
{
    ++netLife_;
    netDeathDropped_ = false;
    quickSlots_ = {"bandage", "water_bottle", "food_can", ""};
    notifications_.clear();
    firecrackers_.clear();
    lanternLit_ = false;
    lanternSeconds_ = 0.0f;
    cacheMarker_.reset();
    cacheSeconds_ = 0.0f;
    lastWounds_ = 0;
    warnedHunger_ = warnedThirst_ = warnedCold_ = warnedWeak_ = false;
    showMap_ = false;
    Notify("You wake up by the road. Find water, food and something to fight with.", 0);
}

void NextDayzGameInstance::InjectLootCatalog(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials)
{
    std::unordered_map<std::string, NextDayz::FLootVisual> catalog;
    NextDayz::FLootVisual fallback;
    if (lootCatalogLoaded_)
    {
        constexpr std::string_view prefix = "bone_item_";
        for (const Assets::FRigPart& part : lootCatalogAsset_.parts)
        {
            if (part.bone < 0 || part.bone >= static_cast<int32_t>(lootCatalogAsset_.bones.size()) || part.modelIndex < 0 ||
                part.modelIndex >= static_cast<int32_t>(lootCatalogAsset_.partModels.size()))
            {
                continue;
            }
            const std::string& bone = lootCatalogAsset_.bones[part.bone].name;
            if (!bone.starts_with(prefix))
            {
                continue;
            }
            const std::string itemId = bone.substr(prefix.size());
            if (catalog.contains(itemId))
            {
                continue;
            }
            NextDayz::FLootVisual visual;
            models.push_back(lootCatalogAsset_.partModels[part.modelIndex]);
            visual.modelId = static_cast<uint32_t>(models.size() - 1);
            for (size_t section = 0; section < part.sectionColors.size() && section < visual.materials.size(); ++section)
            {
                visual.materials[section] = Assets::SceneBuilder::AddRigSectionMaterial(materials, part, section);
            }
            visual.valid = true;
            if (itemId == "unknown")
            {
                fallback = visual;
            }
            else
            {
                catalog.emplace(itemId, visual);
            }
        }
        for (const NextDayz::FItemDef& definition : NextDayz::AllItemDefs())
        {
            if (!catalog.contains(std::string(definition.id)))
            {
                SPDLOG_WARN("[NextDayz] loot catalog has no model for '{}'", definition.id);
            }
        }
    }
    loot_.SetVisualCatalog(std::move(catalog), fallback);
}

std::vector<NextDayz::FLootSpotSeed> NextDayzGameInstance::CollectLootSpots() const
{
    std::vector<NextDayz::FLootSpotSeed> spots;
    for (const NextDayz::FWorldAnchorHandle handle : world_.anchors.Find(NextDayz::EWorldAnchorType::Loot))
    {
        if (const NextDayz::FWorldAnchor* anchor = world_.anchors.Resolve(handle))
        {
            const std::optional<NextDayz::ELootProfile> profile = NextDayz::ParseLootProfile(anchor->profile);
            spots.push_back({anchor->worldPos, profile.value_or(NextDayz::ELootProfile::Residential), anchor->item,
                             anchor->count, anchor->yaw});
        }
    }
    return spots;
}

void NextDayzGameInstance::ScoreRangeTarget(const NextDayz::FWeaponTraceEvent& trace)
{
    const Assets::Node* node = GetEngine().GetScene().GetNodeByInstanceId(trace.hitInstanceId);
    while (node && node->GetName() != "nd_target") node = node->GetParent();
    if (!node) return;
    // nd_target's origin is the aim point; the face is thin, so the distance
    // from the origin is the distance from the centre of the bull.
    const Assets::Scad::FMetadata metadata = Assets::Scad::ParseScadMetadata(node->GetMetadata());
    const int range = static_cast<int>(Assets::Scad::MetadataNumber(metadata, "d", 0.0));
    const float radius = static_cast<float>(Assets::Scad::MetadataNumber(metadata, "r", 0.0));
    const glm::vec3 local = glm::inverse(node->WorldTransform()) * glm::vec4(trace.endPoint, 1.0f);
    ++rangeHits_;
    if (radius <= 0.0f)
    {
        Notify(NextDayz::L10n::Message("Target {} m: hit", range), 0);
        return;
    }
    const int ring = std::clamp(10 - static_cast<int>(std::floor(glm::length(local) / radius * 10.0f)), 0, 10);
    if (ring == 0) Notify(NextDayz::L10n::Message("Target {} m: outside the rings", range), 0);
    else Notify(NextDayz::L10n::Message("Target {} m: {} ring", range, ring), 0);
}

void NextDayzGameInstance::SyncClothingVisuals()
{
    rig_.SetClothing(inventory_.EquippedClothing());
    weapons_.SetArmClothing(inventory_.EquippedClothing());
}

void NextDayzGameInstance::EquipWeaponInstance(NextDayz::FItemInstanceId instanceId, int slot)
{
    const NextDayz::FItemInstance* instance = inventory_.FindInstance(instanceId);
    if (!instance || !weapons_.EquipInstance(slot, inventory_, instanceId))
    {
        Notify("That is not something you can hold", 1);
        return;
    }
    Notify(NextDayz::L10n::Message("{} in hand ({})", ItemDisplayName(instance->defId), std::to_string(slot + 1)));
}

void NextDayzGameInstance::InstallAttachment(NextDayz::FItemInstanceId weapon, NextDayz::FItemInstanceId attachment)
{
    if (!survival_.IsAlive() || actions_.IsActive()) return;
    weapons_.CopyMagazinesTo(inventory_);
    std::string error;
    if (!inventory_.TryAttach(weapon, attachment, &error)) { Notify(error, 1); return; }
    weapons_.RefreshInventory(inventory_);
    player_.CancelAiming();
    weapons_.SetTriggerDown(false);
    audio_.Pickup();
    Notify("Attachment installed", 3);
}

void NextDayzGameInstance::RemoveAttachment(NextDayz::FItemInstanceId weapon, NextDayz::EAttachmentSlot slot)
{
    if (!survival_.IsAlive() || actions_.IsActive()) return;
    weapons_.CopyMagazinesTo(inventory_);
    std::string error;
    if (!inventory_.TryDetach(weapon, slot, &error)) { Notify(error, 1); return; }
    weapons_.RefreshInventory(inventory_);
    player_.CancelAiming();
    weapons_.SetTriggerDown(false);
    audio_.Pickup();
    Notify("Attachment removed", 3);
}

void NextDayzGameInstance::WearItem(NextDayz::FItemInstanceId instanceId)
{
    const NextDayz::FItemInstance* instance = inventory_.FindInstance(instanceId);
    if (!instance)
    {
        return;
    }
    const std::string name = ItemDisplayName(instance->defId);
    if (inventory_.TryEquip(instanceId))
    {
        SyncClothingVisuals();
        audio_.Pickup();
        Notify(NextDayz::L10n::Message("Wearing {}", name));
    }
    else
    {
        Notify("No room for what you are wearing in that slot", 1);
    }
}

void NextDayzGameInstance::TakeOffItem(NextDayz::FItemInstanceId instanceId)
{
    if (inventory_.TryUnequip(instanceId))
    {
        SyncClothingVisuals();
        audio_.Pickup();
    }
    else
    {
        Notify("No room to take it off - drop it instead", 1);
    }
}

void NextDayzGameInstance::DropItem(NextDayz::FItemInstanceId instanceId, int count)
{
    weapons_.CopyMagazinesTo(inventory_);
    NextDayz::Inventory transaction = inventory_;
    const std::optional<NextDayz::FItemInstance> removed = transaction.TryRemove(instanceId, count);
    if (!removed)
    {
        Notify("Empty it first: there is no room for what is inside", 1);
        return;
    }
    glm::vec3 forward = player_.Forward();
    forward.y = 0.0f;
    forward = glm::length(forward) > 1.0e-3f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 at = player_.Position() + forward * 0.8f;
    GetEngine().RayCast(at + glm::vec3(0.0f, 1.2f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), [&](Assets::RayCastResult result)
    {
        if (result.Hit && result.T < 4.0f)
        {
            at.y = static_cast<float>(result.HitPoint.y);
        }
        return true;
    });
    if (!PlacePile({{removed->defId, removed->count, removed->loadedAmmo, removed->attachments}}, at,
                   survivalRng_.UnitFloat() * 6.28f))
    {
        Notify("Cannot drop it here", 1);
        return;
    }
    inventory_ = std::move(transaction);
    SaveCampaign();
    SyncClothingVisuals();
    audio_.Drop();
    Notify(removed->count > 1 ? NextDayz::L10n::Message("Dropped {} x{}", ItemDisplayName(removed->defId), removed->count)
                              : NextDayz::L10n::Message("Dropped {}", ItemDisplayName(removed->defId)));
}

void NextDayzGameInstance::MoveItem(NextDayz::FItemInstanceId instanceId, NextDayz::FContainerId containerId)
{
    if (!inventory_.TryMove(instanceId, containerId))
    {
        Notify("It does not fit there", 1);
    }
    else
    {
        SyncClothingVisuals();
    }
}

void NextDayzGameInstance::PlaceItem(NextDayz::FItemInstanceId instanceId, NextDayz::FContainerId containerId,
                                    NextDayz::FGridPlacement placement)
{
    if (!inventory_.TryPlace(instanceId, containerId, placement)) Notify("It does not fit there", 1);
    else SyncClothingVisuals();
}

void NextDayzGameInstance::SplitItem(NextDayz::FItemInstanceId instanceId)
{
    const NextDayz::FItemInstance* instance = inventory_.FindInstance(instanceId);
    NextDayz::FItemInstanceId split = 0;
    if (!instance || instance->count < 2 || !inventory_.TrySplit(instanceId, instance->count / 2, split))
    {
        Notify("Cannot split that here", 1);
    }
}

void NextDayzGameInstance::TakeLoot(int lootIndex, NextDayz::FContainerId destination, NextDayz::EEquipSlot equipSlot,
                                   std::optional<NextDayz::FGridPlacement> placement)
{
    if (actions_.IsActive() || player_.IsTraversing() || !survival_.IsAlive())
    {
        return;
    }
    const auto nearby = loot_.Nearby(player_.EyePosition(), config_.Loot.ReachMeters);
    if (std::find(nearby.begin(), nearby.end(), lootIndex) == nearby.end())
    {
        return;
    }
    const std::optional<NextDayz::FLootHandle> handle = loot_.ReserveEntry(lootIndex);
    if (!handle)
    {
        return;
    }
    if (actions_.BeginLoot(*handle))
    {
        lootDestination_ = destination;
        lootEquipSlot_ = equipSlot;
        lootPlacement_ = placement;
        player_.CancelAiming();
        weapons_.SetTriggerDown(false);
        weapons_.SetPresentationSuppressed(true);
    }
    else
    {
        loot_.Cancel(*handle);
    }
}

void NextDayzGameInstance::UseInventoryItem(NextDayz::FItemInstanceId instanceId)
{
    if (!survival_.IsAlive() || actions_.IsActive())
    {
        return;
    }
    const NextDayz::FItemInstance* instance = inventory_.FindInstance(instanceId);
    const NextDayz::FItemDef* definition = instance ? NextDayz::FindItemDef(instance->defId) : nullptr;
    if (!definition)
    {
        return;
    }
    using NextDayz::EItemUse;
    if (definition->kind == NextDayz::EItemKind::Attachment)
    {
        InstallAttachment(weapons_.SlotInstance(weapons_.ActiveSlot()), instanceId);
        return;
    }
    using NextDayz::EPlayerAction;
    switch (definition->use)
    {
    case EItemUse::Eat:
        actions_.BeginUse(EPlayerAction::Eat, instanceId);
        return;
    case EItemUse::Drink:
        actions_.BeginUse(EPlayerAction::Drink, instanceId);
        return;
    case EItemUse::Bandage:
    case EItemUse::FirstAid:
    case EItemUse::Painkillers:
        actions_.BeginUse(EPlayerAction::Heal, instanceId);
        return;
    case EItemUse::Rest:
        if (survival_.IsBleeding())
        {
            Notify("You cannot rest while bleeding", 1);
        }
        else
        {
            actions_.BeginUse(EPlayerAction::Rest);
        }
        return;
    case EItemUse::Radio:
        UseRadio();
        return;
    case EItemUse::Lantern:
        ToggleLantern();
        return;
    case EItemUse::Firecrackers:
        ThrowFirecrackers(instanceId);
        return;
    case EItemUse::SignalFlare:
        FireSignal(instanceId);
        return;
    case EItemUse::None:
        break;
    }
    if (NextDayz::IsWieldable(*definition))
    {
        EquipWeaponInstance(instanceId, NextDayz::PreferredWeaponSlot(*definition));
    }
    else if (definition->kind == NextDayz::EItemKind::Clothing)
    {
        inventory_.IsWorn(instanceId) ? TakeOffItem(instanceId) : WearItem(instanceId);
    }
    else if (!definition->description.empty())
    {
        Notify(std::string(definition->description));
    }
}

void NextDayzGameInstance::UseQuickSlot(int slot)
{
    if (slot < 0 || slot >= static_cast<int>(quickSlots_.size()) || quickSlots_[slot].empty())
    {
        return;
    }
    const std::string& itemId = quickSlots_[slot];
    const NextDayz::FItemInstance* chosen = nullptr;
    for (const NextDayz::FItemInstance& instance : inventory_.Instances())
    {
        if (instance.defId == itemId && !inventory_.IsWorn(instance.instanceId))
        {
            chosen = &instance;
            break;
        }
    }
    if (!chosen)
    {
        Notify(NextDayz::L10n::Message("No {} left", ItemDisplayName(itemId)), 1);
        return;
    }
    UseInventoryItem(chosen->instanceId);
}

void NextDayzGameInstance::QuickBandage()
{
    const NextDayz::FSurvivalSnapshot& state = survival_.Snapshot();
    if (state.bleedingWounds == 0 && state.health >= 99.5f)
    {
        Notify("You are not hurt");
        return;
    }
    for (const char* itemId : {"bandage", "rag", "medkit"})
    {
        if (const NextDayz::FItemInstance* instance = inventory_.FindFirst(itemId))
        {
            UseInventoryItem(instance->instanceId);
            return;
        }
    }
    Notify("Nothing to dress the wound with: find bandages or rags", 2);
}

void NextDayzGameInstance::ToggleLantern()
{
    if (lanternLit_)
    {
        lanternLit_ = false;
        audio_.Lantern();
        Notify("Lantern out");
        return;
    }
    if (inventory_.CountOf("lantern") == 0)
    {
        Notify("You have no lantern", 1);
        return;
    }
    if (lanternSeconds_ <= 0.0f)
    {
        if (inventory_.Consume("fuel", 1) == 0)
        {
            Notify("The lantern needs a fuel canister", 1);
            return;
        }
        lanternSeconds_ = kLanternFuelSeconds;
    }
    lanternLit_ = true;
    audio_.Lantern();
    Notify("Lantern lit - and the infected can see it too", 1);
}

void NextDayzGameInstance::UseRadio()
{
    if (inventory_.CountOf("radio") == 0)
    {
        return;
    }
    if (inventory_.Consume("battery", 1) == 0)
    {
        Notify("The radio is dead. It needs a battery", 1);
        return;
    }
    audio_.RadioStatic();
    if (netMode_ == ENetMode::Client)
    {
        // Supply drops are not part of the shared world yet.
        Notify("Only static on the military band...", 1);
        return;
    }
    const std::optional<glm::vec3> cache = loot_.PlantCache(player_.Position(), 120.0f, GetEngine());
    if (!cache)
    {
        Notify("Only static on the military band...", 1);
        return;
    }
    cacheMarker_ = *cache;
    cacheSeconds_ = kCacheMarkerSeconds;
    const glm::vec3 delta = *cache - player_.Position();
    Notify(NextDayz::L10n::Message("Broadcast: supply cache {:.0f} m to the {} (marked on map and compass)",
                       glm::length(glm::vec2(delta.x, delta.z)), CompassWord(delta)), 3);
}

void NextDayzGameInstance::ThrowFirecrackers(NextDayz::FItemInstanceId instanceId)
{
    if (!inventory_.TryConsumeInstance(instanceId))
    {
        return;
    }
    // Lob it along the view; it lands on whatever is in the way, or ~18 m out.
    const glm::vec3 eye = player_.EyePosition();
    const glm::vec3 forward = player_.Forward();
    float distance = 18.0f;
    GetEngine().RayCast(eye, forward, [&](Assets::RayCastResult result)
    {
        if (result.Hit && result.T < distance)
        {
            distance = std::max(0.5f, result.T - 0.3f);
        }
        return true;
    });
    glm::vec3 landing = eye + forward * distance;
    GetEngine().RayCast(landing + glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), [&](Assets::RayCastResult result)
    {
        if (result.Hit && result.T < 30.0f)
        {
            landing.y = static_cast<float>(result.HitPoint.y);
        }
        return true;
    });
    firecrackers_.push_back({landing, 1.2f, 0});
    audio_.Drop();
    Notify("Firecrackers thrown - the infected will come to the noise");
}

void NextDayzGameInstance::UpdateFirecrackers(float deltaSeconds)
{
    for (FFirecracker& cracker : firecrackers_)
    {
        cracker.timer -= deltaSeconds;
        if (cracker.timer <= 0.0f)
        {
            world_.noise.Emit(cracker.position, 75.0f, 1.3f, NextDayz::ENoiseType::Rifle, 2.0f);
            audio_.Firecracker(cracker.position);
            effects_.WorldImpact(cracker.position + glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ++cracker.bangs;
            cracker.timer = 0.7f;
        }
    }
    std::erase_if(firecrackers_, [](const FFirecracker& cracker) { return cracker.bangs >= 4; });
}

void NextDayzGameInstance::UpdateWorldPressure()
{
    const NextDayz::FSurvivalSnapshot& state = survival_.Snapshot();
    const int dressings = inventory_.CountOf("bandage") + inventory_.CountOf("rag") + inventory_.CountOf("medkit");
    loot_.SetScarcity(state.hunger < 30.0f || state.hydration < 30.0f, state.bleedingWounds > 0 && dressings == 0);

    // Darkness hides you, a crouch hides you more, fog hides everything; a lit
    // lantern at night is a beacon.
    const float night = 1.0f - time_.Daylight();
    // The lantern and crouch are per survivor (LocalSurvivor).
    world_.SetWorldSightScale(WorldSightScale());
    // More of them walk the night.
    world_.spawns.SetGlobalCap(night > 0.6f ? 48 : 40);
    time_.SetLanternLight(lanternLit_ ? 1.0f : 0.0f);
}

float NextDayzGameInstance::WorldSightScale() const
{
    const float night = 1.0f - time_.Daylight();
    return glm::mix(1.0f, 0.55f, night) * glm::mix(1.0f, 0.7f, time_.Fog());
}

void NextDayzGameInstance::UpdateSurvivalFeedback(float /*deltaSeconds*/)
{
    const NextDayz::FSurvivalSnapshot& state = survival_.Snapshot();
    if (!survival_.IsAlive())
    {
        return;
    }
    if (state.bleedingWounds > lastWounds_)
    {
        Notify("You are bleeding! Bandage it [H]", 2);
    }
    lastWounds_ = state.bleedingWounds;
    const auto warn = [this](bool condition, bool clear, bool& latch, const char* text, int severity)
    {
        if (condition && !latch)
        {
            Notify(text, severity);
            latch = true;
        }
        else if (clear)
        {
            latch = false;
        }
    };
    warn(state.hunger < 25.0f, state.hunger > 35.0f, warnedHunger_, "You are hungry", 1);
    warn(state.hydration < 25.0f, state.hydration > 35.0f, warnedThirst_, "You are thirsty", 1);
    warn(state.cold > 0.45f, state.cold < 0.25f, warnedCold_, "You are cold: find warmer clothes", 1);
    warn(survival_.IsWeak(), !survival_.IsWeak(), warnedWeak_, "You feel weak - too hungry or thirsty to run", 2);
}

void NextDayzGameInstance::ApplyValidationLoadout()
{
    if (inventory_.TotalCapacity() <= 10)
    {
        NextDayz::FItemInstanceId backpackId = 0;
        if (inventory_.TryAdd("backpack", "Backpack", NextDayz::EItemKind::Clothing, 1, &backpackId))
        {
            inventory_.TryEquip(backpackId);
            SyncClothingVisuals();
        }
    }
    NextDayz::FItemInstanceId akId = 0;
    if (const NextDayz::FItemInstance* existing = inventory_.FindFirst("ak"))
    {
        akId = existing->instanceId;
    }
    else
    {
        inventory_.TryAdd("ak", "AK-74", NextDayz::EItemKind::Weapon, 1, &akId);
    }
    if (inventory_.CountOf("ammo_545") < 60)
    {
        inventory_.TryAdd("ammo_545", "5.45x39", NextDayz::EItemKind::Ammo,
                          60 - inventory_.CountOf("ammo_545"));
    }
    if (akId != 0)
    {
        inventory_.SetLoadedAmmo(akId, 30);
        weapons_.EquipInstance(0, inventory_, akId);
    }
    else
    {
        weapons_.Equip(0, "ak");
    }
}

NextDayz::FSurvivorPresence NextDayzGameInstance::LocalSurvivor() const
{
    NextDayz::FSurvivorPresence survivor;
    survivor.id = NextDayz::kLocalPlayerTargetId;
    survivor.feet = player_.Position();
    survivor.eye = player_.EyePosition();
    survivor.forward = player_.Forward();
    survivor.alive = survival_.IsAlive();
    // Night and fog are world-wide (UpdateWorldPressure); a lit lantern makes
    // this survivor a beacon and a crouch hides it.
    const float night = 1.0f - time_.Daylight();
    const float darkness = glm::mix(1.0f, 0.55f, night);
    survivor.sightScale = lanternLit_ ? std::max(darkness, glm::mix(1.0f, 1.4f, night)) / darkness : 1.0f;
    if (player_.LocomotionState().actualStance == NextDayz::EPlayerStance::Crouched)
    {
        survivor.sightScale *= 0.7f;
    }
    return survivor;
}

void NextDayzGameInstance::BuildZombieNavigation()
{
    // A server has no survivor of its own: windows open as clients arrive.
    if (netMode_ == ENetMode::Server) world_.ClearSurvivors();
    else if (netMode_ == ENetMode::Offline) world_.UpsertSurvivor(LocalSurvivor());
    // Geographic maps fit the sampling band to their terrain; the legacy scene
    // keeps its fixed band.
    const bool geographic = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_) != nullptr;
    world_.BuildNavigation(GetEngine().GetScene(), false, geographic);
}

void NextDayzGameInstance::ResetSession()
{
    if (NetUsesMaster() && netMode_ == ENetMode::Client)
    {
        // A sortie ended in death: the next one starts from the lobby.
        NetReturnToLobby("You died. Your gear lies where you fell.");
        return;
    }
    if (CampaignEnabled())
    {
        if (!survival_.IsAlive() && campaign_.inField) ReturnToSafehouse(ESafehouseReturn::Death);
        return;
    }
    if (!sceneReady_)
    {
        return;
    }
    debugger_.OnSessionStart("new life");
    actions_.Reset();
    inventory_.Clear();
    inventory_.TryAdd("bandage", "Bandage", NextDayz::EItemKind::Consumable, 1);
    inventory_.TryAdd("water_bottle_empty", "Empty Bottle", NextDayz::EItemKind::Misc, 1);
    survival_.Reset();
    StartNewLife();
    weapons_.ResetRuntime();
    world_.ResetPopulation(sessionRng_.Derive(0x5A4F4D42ULL).Seed());
    zombieVisuals_.Reset();
    effects_.Reset();
    if (netMode_ != ENetMode::Client)
    {
        // Online, the world (loot and clock) is the server's and outlives our death.
        loot_.ResetSession(GetEngine());
        time_.Reset(config_.Time, sessionRng_.Derive(0x57454154ULL).Seed());
    }
    netDeathDropped_ = false;
    player_.Destroy();
    player_.Create(GetEngine().GetPhysicsEngine(), ResolveSpawnPosition(), config_);
    if ((validationLoadout_ && GOption->AgentValidation) || (netMode_ == ENetMode::Client && netKitCvar_ && !NetUsesMaster()))
    {
        ApplyValidationLoadout();
    }
    showInventory_ = false;
    showMap_ = false;
    paused_ = false;
    survivalSeconds_ = 0.0;
    hoveredWell_ = {};
    SyncClothingVisuals();
    SyncZombieVisuals();
    SetMouseCaptured(true);
}

std::string NextDayzGameInstance::CurrentObjective() const
{
    const NextDayz::FSurvivalSnapshot& state = survival_.Snapshot();
    if (state.bleedingWounds > 0)
    {
        return NextDayz::L10n::Tr("Stop the bleeding: bandage or rags [H]");
    }
    if (CampaignEnabled())
    {
        if (campaign_.flight.phase != NextDayz::EFlightPhase::Idle)
            return NextDayz::L10n::Format("Rescue: {} ({:.0f}s)", NextDayz::FlightPhaseName(campaign_.flight.phase),
                               campaign_.flight.remaining);
        if (inventory_.CountOf("signal_flare") > 0)
        {
            const NextDayz::FWorldAnchor* nearest = nullptr;
            float distance = std::numeric_limits<float>::max();
            for (const auto& anchor : world_.anchors.All())
            {
                if (anchor.type != NextDayz::EWorldAnchorType::Extraction) continue;
                const float candidate = glm::distance(player_.Position(), anchor.worldPos);
                if (candidate < distance) { nearest = &anchor; distance = candidate; }
            }
            if (nearest)
                return NextDayz::L10n::Format("Reach {} ({:.0f}m {}) | [B] Fire signal", nearest->label, distance,
                                   CompassWord(nearest->worldPos - player_.Position()));
        }
    }
    if (inventory_.CountOf("water_bottle") == 0 && inventory_.CountOf("drink_soda") == 0)
    {
        return NextDayz::L10n::Tr("Find water: a bottle, a soda, or a well or pump in a village");
    }
    bool hasFood = false;
    for (const NextDayz::FItemInstance& instance : inventory_.Instances())
    {
        const NextDayz::FItemDef* definition = NextDayz::FindItemDef(instance.defId);
        hasFood = hasFood || (definition && definition->use == NextDayz::EItemUse::Eat);
    }
    if (!hasFood)
    {
        return NextDayz::L10n::Tr("Find food: houses, shops and the supermarket");
    }
    if (inventory_.TotalCapacity() <= 10)
    {
        return NextDayz::L10n::Tr("Find a backpack or clothes with pockets");
    }
    if (!weapons_.HasActiveWeapon())
    {
        return NextDayz::L10n::Tr("Find something to fight with: a knife, crowbar, axe or a gun");
    }
    if (!inventory_.HasTool(NextDayz::EItemTool::Map) && !inventory_.HasTool(NextDayz::EItemTool::Compass))
    {
        return NextDayz::L10n::Tr("Find a map or compass to navigate");
    }
    return CampaignEnabled() ? NextDayz::L10n::Tr("Find a signal flare gun at a military, police or wilderness site")
                             : NextDayz::L10n::Tr("Raid a fortress or the military base for better gear");
}

void NextDayzGameInstance::CreateZombieVisuals()
{
    Assets::Scene& scene = GetEngine().GetScene();
    zombieRigVisuals_.clear();
    zombieRigVisuals_.resize(world_.zombies.Capacity());
    zombieVisualOwners_.assign(world_.zombies.Capacity(), {});
    if (zombieRigLoaded_ && !zombieRigPartModelIds_.empty())
    {
        for (size_t index = 0; index < world_.zombies.Capacity(); ++index)
        {
            NextGameplay::FRigInstanceDesc desc;
            desc.namePrefix = fmt::format("nd_infected_{}/rig", index);
            desc.partModelIds = zombieRigPartModelIds_;
            desc.partMaterialIds = zombieRigPartMaterialIds_;
            const uint32_t shirt = zombieTintMaterialIds_[index % zombieTintMaterialIds_.size()];
            for (size_t partIndex = 0; partIndex < zombieRigAsset_.parts.size(); ++partIndex)
            {
                const Assets::FRigPart& part = zombieRigAsset_.parts[partIndex];
                for (size_t section = 0; section < part.sectionTintable.size() && section < 16; ++section)
                {
                    if (part.sectionTintable[section])
                    {
                        desc.partMaterialIds[partIndex][section] = shirt;
                    }
                }
            }
            if (!zombieRigVisuals_[index].Instantiate(scene, zombieRigAsset_, zombieRigClips_, desc,
                                                      fmt::format("nd_infected_{}", index)))
            {
                SPDLOG_WARN("[NextDayz] infected body {} failed to instantiate", index);
            }
        }
    }
    scene.MarkDirty();
}

void NextDayzGameInstance::SyncZombieVisuals(float deltaSeconds)
{
    const auto& slots = world_.zombies.Slots();
    // The infected look at, and reach for, the player's chest.
    const glm::vec3 lookTarget = player_.EyePosition() - glm::vec3(0.0f, 0.25f, 0.0f);
    std::array<NextDayz::FHitCapsule, NextDayz::ZombieRigVisual::kHitCapsuleCount> standing{};
    for (size_t index = 0; index < slots.size() && index < zombieVisualOwners_.size(); ++index)
    {
        const NextDayz::FZombieRuntime& zombie = slots[index];
        NextDayz::ZombieRigVisual* visual =
            index < zombieRigVisuals_.size() && zombieRigVisuals_[index].IsBound() ? &zombieRigVisuals_[index] : nullptr;
        NextDayz::FZombieHandle& owner = zombieVisualOwners_[index];
        if (!zombie.active)
        {
            if (owner.IsValid())
            {
                world_.combat.ClearHitboxes(owner);
                zombieVisuals_.Release(owner);
                owner = {};
            }
            if (visual)
            {
                visual->Hide();
            }
            continue;
        }
        if (owner != zombie.handle)
        {
            if (owner.IsValid())
            {
                world_.combat.ClearHitboxes(owner);
                zombieVisuals_.Release(owner);
            }
            owner = zombie.handle;
            zombieVisuals_.Acquire(zombie.handle);
            if (visual)
            {
                visual->Reset(zombie);
            }
        }
        if (visual)
        {
            visual->Update(zombie, lookTarget, config_.Zombie, deltaSeconds);
        }
        if (zombie.state == NextDayz::EZombieState::Dead)
        {
            world_.combat.ClearHitboxes(zombie.handle);
        }
        else if (visual)
        {
            world_.combat.SetHitboxes(zombie.handle, visual->Hitboxes());
        }
        else
        {
            const size_t count = NextDayz::ZombieRigVisual::StandingHitboxes(zombie.position, zombie.forward, standing);
            world_.combat.SetHitboxes(zombie.handle, std::span<const NextDayz::FHitCapsule>(standing.data(), count));
        }
    }
    GetEngine().GetScene().MarkTransformDirty();
}

void NextDayzGameInstance::HandleDamageEvents()
{
    // Offline every hit is the player's; a client hears every survivor's from the server.
    std::vector<FNetDamage> events = std::exchange(netDamage_, {});
    if (netMode_ != ENetMode::Client)
    {
        for (const NextDayz::FDamageEvent& damage : world_.combat.ConsumeDamageEvents())
        {
            events.push_back({damage, true});
        }
    }
    for (const FNetDamage& notice : events)
    {
        const NextDayz::FDamageEvent& damage = notice.damage;
        debugger_.RecordDamage(damage);
        if (damage.target.index < zombieRigVisuals_.size() && zombieRigVisuals_[damage.target.index].IsBound())
        {
            zombieRigVisuals_[damage.target.index].AddHit(damage, config_.Zombie);
        }
        effects_.BloodHit(damage.worldPoint, damage.direction, -damage.direction, damage.hitZone, damage.killed);
        audio_.Hit(damage.hitZone, damage.killed);
        if (!notice.mine)
        {
            continue;   // another survivor's round: no marker, and no drop here
        }
        if (weapons_.ActiveIsMelee())
        {
            audio_.MeleeImpact();
        }
        if (damage.killed && netMode_ == ENetMode::Offline)
        {
            if (const NextDayz::FZombieRuntime* zombie = world_.zombies.Resolve(damage.target))
            {
                // Under the chest once it has fallen: behind the feet, ahead, or where it knelt.
                const float along = zombie->death == NextDayz::EZombieDeath::Back ? -0.7f
                    : zombie->death == NextDayz::EZombieDeath::Forward ? 0.8f : 0.6f;
                // Spread it on whatever the body lies on.
                glm::vec3 ground = zombie->position + zombie->forward * along;
                glm::vec3 groundNormal(0.0f, 1.0f, 0.0f);
                GetEngine().RayCast(ground + glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
                                    [&](Assets::RayCastResult result)
                                    {
                                        if (result.Hit && result.T < 2.5f)
                                        {
                                            ground = glm::vec3(result.HitPoint);
                                            groundNormal = glm::vec3(result.Normal);
                                        }
                                        return true;
                                    });
                effects_.BloodPool(ground, groundNormal, 0.8f);
                // Some of them were still carrying something.
                if (survivalRng_.UnitFloat() < 0.35f)
                {
                    using NextDayz::ELootProfile;
                    using NextDayz::EZombieProfile;
                    const ELootProfile profile = zombie->profile == EZombieProfile::Military ? ELootProfile::Military
                        : zombie->profile == EZombieProfile::Industrial                     ? ELootProfile::Industrial
                        : zombie->profile == EZombieProfile::Wilderness                     ? ELootProfile::Wilderness
                                                                                            : ELootProfile::Residential;
                    NextDayz::FLootStack stack = NextDayz::RollLoot(profile, survivalRng_.NextU32());
                    const glm::vec3 side(zombie->forward.z, 0.0f, -zombie->forward.x);
                    loot_.SpawnPile({std::move(stack)}, ground + side * 0.5f, survivalRng_.UnitFloat() * 6.28f,
                                    GetEngine());
                }
            }
        }
        // One marker per burst: a kill or headshot anywhere in it colours it.
        if (hitMarkerSeconds_ <= 0.0f)
        {
            hitMarkerKill_ = false;
            hitMarkerHead_ = false;
        }
        hitMarkerKill_ = hitMarkerKill_ || damage.killed;
        hitMarkerHead_ = hitMarkerHead_ || damage.hitZone == NextDayz::EHitZone::Head;
        hitMarkerSeconds_ = std::max(hitMarkerSeconds_, damage.killed ? kKillMarkerSeconds : kHitMarkerSeconds);
    }
}

void NextDayzGameInstance::UpdateZombieAudio(float deltaSeconds)
{
    const auto& slots = world_.zombies.Slots();
    zombieAudio_.resize(slots.size());
    for (size_t index = 0; index < slots.size(); ++index)
    {
        const NextDayz::FZombieRuntime& zombie = slots[index];
        FZombieAudio& audio = zombieAudio_[index];
        if (!zombie.active)
        {
            audio = {};
            continue;
        }
        if (audio.handle != zombie.handle)
        {
            audio = {};
            audio.handle = zombie.handle;
            audio.state = zombie.state;
            audio.groanSeconds = 2.0f + 6.0f * zombie.variant.phase;
        }
        const glm::vec3 voice = zombie.position + glm::vec3(0.0f, 1.5f, 0.0f);
        if (zombie.state != audio.state)
        {
            switch (zombie.state)
            {
            case NextDayz::EZombieState::Alert: audio_.ZombieAlert(voice); break;
            case NextDayz::EZombieState::Attack: audio_.ZombieAttack(voice); break;
            case NextDayz::EZombieState::Dead:
                audio_.ZombieDeath(voice);
                audio.fallSeconds = 0.55f;
                break;
            default: break;
            }
            audio.state = zombie.state;
        }
        if (audio.fallSeconds > 0.0f)
        {
            audio.fallSeconds -= deltaSeconds;
            if (audio.fallSeconds <= 0.0f)
            {
                audio_.BodyFall(zombie.position);
            }
        }
        const bool vocal = zombie.state == NextDayz::EZombieState::Wander ||
                           zombie.state == NextDayz::EZombieState::Investigate ||
                           zombie.state == NextDayz::EZombieState::Chase;
        if (vocal)
        {
            audio.groanSeconds -= deltaSeconds;
            if (audio.groanSeconds <= 0.0f)
            {
                audio_.ZombieGroan(voice);
                // A chasing infected never stops; a wandering one mutters now and then.
                const float jitter = std::fmod(zombie.variant.phase * 7.31f + audio.groans * 0.618f, 1.0f);
                audio.groanSeconds = (zombie.state == NextDayz::EZombieState::Chase ? 1.8f : 5.0f) + 4.0f * jitter;
                ++audio.groans;
            }
        }
    }
}

const NextDayz::FZombieRuntime* NextDayzGameInstance::FocusZombie() const
{
    if (const NextDayz::FZombieRuntime* zombie = debugger_.SelectedZombie())
    {
        return zombie;
    }
    if (const NextDayz::FZombieRuntime* zombie = world_.zombies.Resolve(validationZombie_))
    {
        return zombie;
    }
    for (const NextDayz::FZombieRuntime& zombie : world_.zombies.Slots())
    {
        if (zombie.active)
        {
            return &zombie;
        }
    }
    return nullptr;
}

void NextDayzGameInstance::ValidationHitZombie(NextDayz::EHitZone zone)
{
    const NextDayz::FWeaponDef* rifle = NextDayz::FindWeaponDef("ak");
    if (const NextDayz::FZombieRuntime* focus = FocusZombie())
    {
        const NextDayz::FZombieRuntime& zombie = *focus;
        if (zombie.state == NextDayz::EZombieState::Dead)
        {
            return;
        }
        for (const NextDayz::FHitCapsule& capsule : world_.combat.Hitboxes(zombie.handle))
        {
            if (capsule.zone != zone)
            {
                continue;
            }
            // A real trace from in front of the body; if a reaching arm is in
            // the way, fall back to the capsule itself so the fixture stays on
            // the zone it asked for.
            const glm::vec3 target = (capsule.a + capsule.b) * 0.5f;
            const glm::vec3 origin = target + zombie.forward * 2.5f;
            const glm::vec3 direction = glm::normalize(target - origin);
            std::optional<NextDayz::FHitboxHit> hit = world_.combat.TraceHitboxes(origin, direction, 400.0f);
            if (!hit || hit->zone != zone || hit->zombie != zombie.handle)
            {
                hit = NextDayz::FHitboxHit{zombie.handle, zone, 2.5f, target, -direction};
            }
            if (!rifle)
            {
                return;
            }
            NextDayz::FWeaponHitEvent event;
            event.sequence = ++validationAttackSequence_;
            event.weaponId = "ak";
            event.origin = origin;
            event.direction = direction;
            event.hitPoint = hit->point;
            event.normal = hit->normal;
            event.baseDamage = rifle->baseDamage;
            event.impactScale = rifle->impactScale;
            event.zombie = hit->zombie;
            event.zone = hit->zone;
            world_.combat.ProcessHit(event, rifle->damageFalloffDistance);
            HandleDamageEvents();
            return;
        }
        return;
    }
}

void NextDayzGameInstance::BindDebugger()
{
    NextDayz::Debug::FSystems systems;
    systems.engine = &GetEngine();
    systems.config = &config_;
    systems.player = &player_;
    systems.actions = &actions_;
    systems.survival = &survival_;
    systems.rig = &rig_;
    systems.weapons = &weapons_;
    systems.inventory = &inventory_;
    systems.loot = &loot_;
    systems.anchors = &world_.anchors;
    systems.zombies = &world_.zombies;
    systems.combat = &world_.combat;
    systems.effects = &effects_;
    systems.audio = &audio_;
    systems.noise = &world_.noise;
    systems.spawns = &world_.spawns;
    systems.navGrid = [this]() { return &world_.Navigation(); };
    systems.time = &time_;
    systems.zombieVisuals = &zombieRigVisuals_;

    NextDayz::Debug::FCommands commands;
    commands.project = [this](const glm::vec3& world, glm::vec2& screen)
    {
        ImVec2 projected;
        if (!Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, world, projected))
        {
            return false;
        }
        screen = glm::vec2(projected.x, projected.y);
        return true;
    };
    commands.viewCamera = [this]() -> std::optional<NextDayz::Debug::FViewCamera>
    {
        Assets::Camera camera{};
        if (!OverrideRenderCamera(camera))
        {
            return std::nullopt;
        }
        return NextDayz::Debug::FViewCamera{camera.ModelView, camera.FieldOfView, camera.NearPlane, camera.FarPlane};
    };
    commands.raycast = [this](const glm::vec3& origin, const glm::vec3& direction, float maxDistance)
        -> std::optional<NextDayz::Debug::FRayHit>
    {
        std::optional<NextDayz::Debug::FRayHit> hit;
        GetEngine().RayCast(origin, direction, [&](Assets::RayCastResult result)
        {
            if (result.Hit && result.T <= maxDistance && (!hit || result.T < hit->distance))
            {
                hit = NextDayz::Debug::FRayHit{glm::vec3(result.HitPoint), glm::vec3(result.Normal), result.T,
                                               result.InstanceId};
            }
            return true;
        });
        return hit;
    };
    commands.nodeName = [this](uint32_t instanceId)
    {
        const Assets::Node* node = instanceId ? GetEngine().GetScene().GetNodeByInstanceId(instanceId) : nullptr;
        return node ? node->GetName() : std::string("-");
    };
    commands.teleport = [this](const glm::vec3& feet) { TeleportPlayer(feet); };
    commands.spawnZombie = [this](NextDayz::EZombieProfile profile, const glm::vec3& feet)
    {
        return SpawnZombieAt(profile, feet);
    };
    commands.killZombie = [this](NextDayz::FZombieHandle zombie) { KillZombie(zombie); };
    commands.giveItem = [this](const std::string& itemId, int count) { GiveItem(itemId, count); };
    commands.useItem = [this](NextDayz::FItemInstanceId instanceId) { UseInventoryItem(instanceId); };
    commands.dropItem = [this](NextDayz::FItemInstanceId instanceId) { DropItem(instanceId, -1); };
    commands.resetSession = [this]() { ResetSession(); };
    commands.expeditionAction = [this](NextDayz::Debug::EExpeditionAction action) { DebugExpeditionAction(action); };
    commands.syncZombieVisuals = [this]() { SyncZombieVisuals(); };
    commands.setCursorFree = [this](bool free)
    {
        // Leaving the debug cursor gives the mouse back to the game only if
        // nothing else (inventory, map, pause, death) still wants it free.
        SetMouseCaptured(!free && !showInventory_ && !showMap_ && !paused_ && survival_.IsAlive() &&
                         (!CampaignEnabled() || campaign_.inField));
        if (free)
        {
            weapons_.SetTriggerDown(false);
            player_.CancelAiming();
        }
    };
    debugger_.Bind(systems, std::move(commands));
}

NextDayz::Debug::FGameFacts NextDayzGameInstance::DebugFacts() const
{
    NextDayz::Debug::FGameFacts facts;
    facts.survivalSeconds = survivalSeconds_;
    facts.paused = paused_;
    facts.mouseFree = !mouseCaptured_;
    facts.lanternLit = lanternLit_;
    facts.lanternSeconds = lanternSeconds_;
    facts.cacheMarker = cacheMarker_;
    facts.firecrackers.reserve(firecrackers_.size());
    for (const FFirecracker& cracker : firecrackers_)
    {
        facts.firecrackers.push_back(cracker.position);
    }
    facts.soldierRigLoaded = rig_.HasRig();
    facts.zombieRigLoaded = zombieRigLoaded_;
    facts.lootCatalogLoaded = lootCatalogLoaded_;
    facts.hoveringWell = hoveredWell_.IsValid();
    facts.objective = CurrentObjective();
    facts.campaignEnabled = CampaignEnabled();
    facts.inField = campaign_.inField;
    facts.expeditionBusy = pendingCampaign_.has_value() || !loadingMapPath_.empty();
    facts.saveBlocked = campaignSaveBlocked_;
    const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_);
    facts.regionName = map ? map->name : ActiveMapId();
    facts.flightPhase = NextDayz::FlightPhaseName(campaign_.flight.phase);
    return facts;
}

void NextDayzGameInstance::TeleportPlayer(const glm::vec3& feet)
{
    if (!sceneReady_)
    {
        return;
    }
    // Recreating the controller moves the character through walls cleanly;
    // the view (yaw, pitch, FPS/TPS) carries over.
    player_.Destroy();
    player_.Create(GetEngine().GetPhysicsEngine(), feet + glm::vec3(0.0f, 1.0f, 0.0f), config_);
    debugger_.Log(NextDayz::Debug::EChannel::Player, 1,
                  NextDayz::L10n::Message("Teleported to ({:.1f}, {:.1f}, {:.1f})", feet.x, feet.y, feet.z), &feet);
}

NextDayz::FZombieHandle NextDayzGameInstance::SpawnZombieAt(NextDayz::EZombieProfile profile, const glm::vec3& feet)
{
    const NextDayz::FZombieHandle handle = world_.zombies.Spawn(profile, feet);
    if (NextDayz::FZombieRuntime* zombie = world_.zombies.Resolve(handle))
    {
        // Face the player so a fixture starts from a known pose.
        glm::vec3 toPlayer = player_.Position() - feet;
        toPlayer.y = 0.0f;
        if (glm::length(toPlayer) > 1.0e-3f)
        {
            zombie->forward = glm::normalize(toPlayer);
        }
    }
    else
    {
        debugger_.Log(NextDayz::Debug::EChannel::Spawn, 2, "Spawn failed: every infected slot is in use");
    }
    SyncZombieVisuals();
    return handle;
}

void NextDayzGameInstance::KillZombie(NextDayz::FZombieHandle zombie)
{
    // Through combat like any blow, so death clips, blood, loot and the log all run.
    if (world_.combat.ProcessMelee(++validationAttackSequence_, zombie, 1000.0f, NextDayz::EHitZone::Head))
    {
        HandleDamageEvents();
    }
}

void NextDayzGameInstance::GiveItem(const std::string& itemId, int count)
{
    const NextDayz::FItemDef* definition = NextDayz::FindItemDef(itemId);
    if (!definition || count <= 0)
    {
        return;
    }
    const int fits = inventory_.FitCount(itemId, count);
    if (fits > 0)
    {
        inventory_.TryAdd(itemId, std::string(definition->displayName), definition->kind, fits);
    }
    if (fits < count)
    {
        // What does not fit lands at the feet, like a drop.
        PlacePile({{itemId, count - fits, 0}}, player_.Position(), 0.0f);
    }
    debugger_.Log(NextDayz::Debug::EChannel::Player, 0,
                  fits < count ? NextDayz::L10n::Message("Debug give {} x{} ({} on the ground)", itemId, count, count - fits)
                               : NextDayz::L10n::Message("Debug give {} x{}", itemId, count));
}

void NextDayzGameInstance::ApplyDebugSelection(const std::string& selection)
{
    if (selection.empty() || selection == "none")
    {
        debugger_.ClearSelection();
        return;
    }
    if (selection == "nearest")
    {
        const NextDayz::FZombieRuntime* nearest = nullptr;
        for (const NextDayz::FZombieRuntime& zombie : world_.zombies.Slots())
        {
            if (zombie.active && zombie.state != NextDayz::EZombieState::Dead &&
                (!nearest || glm::distance(zombie.position, player_.Position()) <
                                 glm::distance(nearest->position, player_.Position())))
            {
                nearest = &zombie;
            }
        }
        if (nearest)
        {
            debugger_.SelectZombie(nearest->handle);
            debugger_.RequestTab(NextDayz::Debug::ETab::Infected);
        }
        return;
    }
    const int index = std::atoi(selection.c_str() + 1);
    switch (selection.front())
    {
    case 'z':
        if (index >= 0 && index < static_cast<int>(world_.zombies.Slots().size()) && world_.zombies.Slots()[index].active)
        {
            debugger_.SelectZombie(world_.zombies.Slots()[index].handle);
            debugger_.RequestTab(NextDayz::Debug::ETab::Infected);
        }
        break;
    case 'l':
        debugger_.SelectLoot(index);
        debugger_.RequestTab(NextDayz::Debug::ETab::Loot);
        break;
    case 's':
        debugger_.SelectSpawnPoint(index);
        debugger_.RequestTab(NextDayz::Debug::ETab::Spawns);
        break;
    default:
        SPDLOG_WARN("[NextDayz] nextdayz.debug.select: unknown selection '{}'", selection);
        break;
    }
}

void NextDayzGameInstance::SetMouseCaptured(bool captured)
{
    // The debug cursor (F9) keeps the mouse free until it is switched off.
    captured = captured && !settingsOpen_ && !sessionMenuOpen_ && !debugger_.CursorMode();
    mouseCaptured_ = captured;
    if (!captured)
    {
        player_.CancelAiming();
        player_.SetFreeLook(false);
        leftAltHeld_ = false;
        rightAltHeld_ = false;
    }
    resetMouse_ = true;
    if (!GOption->AgentValidation)
    {
        SDL_SetWindowRelativeMouseMode(GetEngine().GetWindow().Handle(), captured);
    }
}

void NextDayzGameInstance::SetInventoryOpen(bool open)
{
    showInventory_ = open;
    player_.SetMovementLocked(open);
    if (open)
    {
        player_.CancelAiming();
        weapons_.SetTriggerDown(false);
        SetMouseCaptured(false);
    }
    else
    {
        SetMouseCaptured(true);
    }
}

bool NextDayzGameInstance::OnKey(SDL_Event& event)
{
    if (settingsOpen_ || sessionMenuOpen_) return true;
    if (cabinScene_ || NetInLobby())
    {
        return false; // cabin shortcuts are handled by ImGui, without controlling the survivor
    }
    if (NetUsesMaster() && netMode_ == ENetMode::Client && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.key == SDLK_B && !actions_.IsActive() && !player_.IsTraversing() && NetRequestExtract())
    {
        return true;
    }
    if (CampaignEnabled())
    {
        if (!campaign_.inField || campaignSaveBlocked_) return true;
        if (campaign_.flight.boarded)
        {
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
            { paused_ = !paused_; SetMouseCaptured(!paused_); }
            return true;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_E &&
            !actions_.IsActive() && !player_.IsTraversing() && BoardHelicopter()) return true;
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_B &&
            !actions_.IsActive() && !player_.IsTraversing())
        {
            if (const auto* flare = inventory_.FindFirst("signal_flare")) FireSignal(flare->instanceId);
            else Notify("Find a signal flare gun to call rescue", 1);
            return true;
        }
    }
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
    {
        return false;
    }
    const bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
    if (pressed && actions_.IsActive() &&
        (event.key.key == SDLK_W || event.key.key == SDLK_S || event.key.key == SDLK_A ||
         event.key.key == SDLK_D || event.key.key == SDLK_SPACE))
    {
        actions_.RequestCancel();
    }
    // Debugger keys: F5 window, F6/F7/F8 layers, F9 cursor, F10 freeze (Shift+F10 step), PgUp/PgDn tabs.
    if (debugger_.HandleKey(event.key.key, pressed && !event.key.repeat, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0))
    {
        return true;
    }
    switch (event.key.key)
    {
    case SDLK_W: player_.SetMoveKey(0, pressed); return true;
    case SDLK_S: player_.SetMoveKey(1, pressed); return true;
    case SDLK_A: player_.SetMoveKey(2, pressed); return true;
    case SDLK_D: player_.SetMoveKey(3, pressed); return true;
    case SDLK_LALT:
    case SDLK_RALT:
        if (event.key.repeat) return true;
        if (event.key.key == SDLK_LALT) leftAltHeld_ = pressed;
        else rightAltHeld_ = pressed;
        player_.SetFreeLook((leftAltHeld_ || rightAltHeld_) && mouseCaptured_ && !paused_ &&
                            survival_.IsAlive() && !weapons_.TriggerDown());
        return true;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        player_.SetSprintModifier(pressed);
        return true;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        player_.SetWalkModifier(pressed);
        return true;
    case SDLK_C:
        if (pressed && !actions_.IsActive() && !player_.IsTraversing()) player_.QueueCrouchToggle();
        return true;
    case SDLK_SPACE:
        // Held jump keeps the airborne ledge catch armed (PlayerController::WantsAirCatch).
        player_.SetJumpHeld(pressed);
        if (pressed && !event.key.repeat && !actions_.IsActive() &&
            weapons_.PresentationAction() == NextDayz::EWeaponPresentationAction::None)
        {
            player_.QueueContextualJump(GetEngine());
        }
        return true;
    case SDLK_R:
        if (showInventory_) return false; // R rotates the inventory drag preview.
        if (pressed && !player_.IsTraversing())
        {
            weapons_.RequestReload(inventory_);
            if (weapons_.IsReloading()) player_.CancelAiming();
        }
        return true;
    case SDLK_1:
        if (pressed && !player_.IsTraversing())
        {
            weapons_.SwitchSlot(0);
            if (weapons_.IsSwitching()) player_.CancelAiming();
        }
        return true;
    case SDLK_2:
        if (pressed && !player_.IsTraversing())
        {
            weapons_.SwitchSlot(1);
            if (weapons_.IsSwitching()) player_.CancelAiming();
        }
        return true;
    case SDLK_Q:
        if (pressed && !player_.IsTraversing())
        {
            weapons_.SwitchPrevious();
            if (weapons_.IsSwitching()) player_.CancelAiming();
        }
        return true;
    case SDLK_3:
        if (pressed && !player_.IsTraversing())
        {
            weapons_.SwitchSlot(NextDayz::kSidearmSlot);
            if (weapons_.IsSwitching()) player_.CancelAiming();
        }
        return true;
    case SDLK_4:
    case SDLK_5:
    case SDLK_6:
    case SDLK_7:
        if (pressed && !player_.IsTraversing())
        {
            UseQuickSlot(static_cast<int>(event.key.key - SDLK_4));
        }
        return true;
    case SDLK_H:
        if (pressed && !player_.IsTraversing()) QuickBandage();
        return true;
    case SDLK_L:
        if (pressed) ToggleLantern();
        return true;
    case SDLK_M:
        if (pressed)
        {
            showMap_ = !showMap_;
            if (showMap_)
            {
                SetInventoryOpen(false);
                player_.CancelAiming();
                weapons_.SetTriggerDown(false);
            }
            SetMouseCaptured(!showMap_);
        }
        return true;
    case SDLK_E:
        if (pressed && !actions_.IsActive() && !player_.IsTraversing())
        {
            if (const std::optional<NextDayz::FLootHandle> handle = loot_.ReserveHovered())
            {
                if (actions_.BeginLoot(*handle))
                {
                    lootDestination_ = 0;
                    lootEquipSlot_ = NextDayz::EEquipSlot::None;
                    lootPlacement_.reset();
                    player_.CancelAiming();
                    player_.SetMovementLocked(true);
                    weapons_.SetTriggerDown(false);
                    weapons_.SetPresentationSuppressed(true);
                }
                else
                {
                    loot_.Cancel(*handle);
                }
            }
            else if (hoveredWell_.IsValid())
            {
                actions_.BeginUse(NextDayz::EPlayerAction::DrinkFromWell);
            }
        }
        return true;
    case SDLK_F:
        if (pressed && hoveredWell_.IsValid() && !actions_.IsActive())
        {
            const auto bottle = std::find_if(inventory_.Instances().begin(), inventory_.Instances().end(),
                [](const NextDayz::FItemInstance& instance) { return instance.defId == "water_bottle_empty"; });
            if (bottle != inventory_.Instances().end())
            {
                actions_.BeginUse(NextDayz::EPlayerAction::FillBottle, bottle->instanceId);
            }
        }
        return true;
    case SDLK_V:
        if (pressed) player_.ToggleView();
        return true;
    case SDLK_G:
        if (pressed) time_.ToggleOvercast();
        return true;
    case SDLK_T:
        if (pressed) time_.Skip(120.0); // debug: jump 2 game hours
        return true;
    case SDLK_TAB:
    case SDLK_I:
        if (pressed && !player_.IsTraversing())
        {
            showMap_ = false;
            SetInventoryOpen(!showInventory_);
        }
        return true;
    case SDLK_ESCAPE:
        if (pressed)
        {
            if (showMap_)
            {
                showMap_ = false;
                SetMouseCaptured(true);
            }
            else if (showInventory_)
            {
                SetInventoryOpen(false);
            }
            else
            {
                paused_ = !paused_;
                SetMouseCaptured(!paused_);
            }
        }
        return true;
    default:
        return false;
    }
}

bool NextDayzGameInstance::OnCursorPosition(double xpos, double ypos)
{
    if (settingsOpen_ || sessionMenuOpen_ || !mouseCaptured_ || showInventory_ || showMap_)
    {
        return false;
    }

    if (SDL_GetWindowRelativeMouseMode(GetEngine().GetWindow().Handle()))
    {
        player_.OnLook(static_cast<float>(xpos) * mouseSensitivity_,
                       static_cast<float>(ypos) * mouseSensitivity_ * (invertMouseY_ ? -1.0f : 1.0f));
    }
    else
    {
        if (resetMouse_)
        {
            mousePos_ = glm::dvec2(xpos, ypos);
            resetMouse_ = false;
            return true;
        }
        const glm::dvec2 delta = glm::dvec2(xpos, ypos) - mousePos_;
        mousePos_ = glm::dvec2(xpos, ypos);
        player_.OnLook(static_cast<float>(delta.x) * mouseSensitivity_,
                       static_cast<float>(delta.y) * mouseSensitivity_ * (invertMouseY_ ? -1.0f : 1.0f));
    }
    return true;
}

bool NextDayzGameInstance::OnMouseButton(SDL_Event& event)
{
    if (settingsOpen_ || sessionMenuOpen_) return true;
    if (cabinScene_ || NetInLobby()) return false;
    if (CampaignEnabled() && (!campaign_.inField || campaign_.flight.boarded || campaignSaveBlocked_)) return false;
    if (paused_ || !survival_.IsAlive()) return true;
    if (debugger_.CursorMode())
    {
        // Debug cursor: clicks that ImGui did not take select things in the world.
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            debugger_.PickAtCursor();
        }
        return true;
    }
    if (showInventory_ || showMap_)
    {
        return false; // let ImGui handle panel interaction
    }
    if (actions_.IsActive())
    {
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        if (!mouseCaptured_)
        {
            SetMouseCaptured(true);
            return true;
        }
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            player_.SetFreeLook(false);
            weapons_.SetTriggerDown(true);
            return true;
        }
        if (event.button.button == SDL_BUTTON_RIGHT)
        {
            if (player_.IsTraversing() || weapons_.IsReloading() || weapons_.IsSwitching())
            {
                player_.CancelAiming();
                return true;
            }
            player_.SetAimButton(true, weapons_.HasActiveWeapon() && !weapons_.ActiveIsMelee());
            return true;
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            weapons_.SetTriggerDown(false);
            return true;
        }
        if (event.button.button == SDL_BUTTON_RIGHT)
        {
            player_.SetAimButton(false, false);
            return true;
        }
    }
    return false;
}

void NextDayzGameInstance::UpdateGrass(float dt)
{
    grassInteractors_.clear();
    grassInteractors_.reserve(world_.zombies.Capacity() + 1);
    const auto& locomotion = player_.LocomotionState();
    if (grassPlayerInteraction_ && survival_.IsAlive() && locomotion.onGround && !player_.IsTraversing())
        grassInteractors_.push_back({0, player_.Position(), 0.55f, 1.0f});
    if (grassZombieInteraction_)
    {
        for (const auto& zombie : world_.zombies.Slots())
        {
            if (!zombie.active || zombie.state == NextDayz::EZombieState::Dead) continue;
            const uint64_t id = (uint64_t(zombie.handle.generation) << 32) | (uint64_t(zombie.handle.index) + 1);
            grassInteractors_.push_back({id, zombie.position, 0.5f, 1.0f});
        }
    }
    grass_.Tick(GetEngine().GetScene(), dt, player_.Position(), time_.Storm(), grassInteractors_);
}

void NextDayzGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg)
{
    reg.Add("sessionMode", [this]() -> Runtime::Agent::FAgentQueryValue { return std::string(trainingSession_ ? "training" :
        netMode_ == ENetMode::Server ? "server" : netMode_ == ENetMode::Client ? NetUsesMaster() ? "master" : "direct" : "offline"); });
    reg.Add("sessionMenuOpen", [this]() -> Runtime::Agent::FAgentQueryValue { return sessionMenuOpen_; });
    reg.Add("sessionMenuTab", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(sessionMenuTab_); });
    reg.Add("masterServerCount", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(masterServers_.size()); });
    reg.Add("loggedIn", [this]() -> Runtime::Agent::FAgentQueryValue { return NetLoggedIn(); });
    reg.Add("accountName", [this]() -> Runtime::Agent::FAgentQueryValue { return NetAccountName(); });
    reg.Add("masterUrl", [this]() -> Runtime::Agent::FAgentQueryValue { return netMasterCvar_; });
    reg.Add("sessionMessage", [this]() -> Runtime::Agent::FAgentQueryValue { return sessionMenuMessage_.Display(); });
    reg.Add("settingsOpen", [this]() -> Runtime::Agent::FAgentQueryValue { return settingsOpen_; });
    reg.Add("settingsTab", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(settingsTab_); });
    RegisterNetQueries(reg);
    reg.Add("cabinScene", [this]() -> Runtime::Agent::FAgentQueryValue { return cabinScene_ && sceneReady_; });
    reg.Add("fieldAssetsLoaded", [this]() -> Runtime::Agent::FAgentQueryValue { return fieldAssetsLoaded_; });
    reg.Add("cabinPanel", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(safehouseTab_); });
    reg.Add("cabinClip", [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.CurrentBaseClipName(); });
    reg.Add("magazineCapacity", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(weapons_.MagazineCapacity()); });
    reg.Add("reloadDuration", [this]() -> Runtime::Agent::FAgentQueryValue { return double(weapons_.ReloadDuration()); });
    reg.Add("stabilityScale", [this]() -> Runtime::Agent::FAgentQueryValue { return double(NextDayz::WeaponStabilityScale(weapons_.SlotAttachments(weapons_.ActiveSlot()))); });
    reg.Add("suppressed", [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.IsSuppressed(); });
    reg.Add("activeOptic", [this]() -> Runtime::Agent::FAgentQueryValue
    { const auto* optic = NextDayz::FindAttachmentDef(weapons_.ActiveOptic()); return optic ? std::string(optic->id) : std::string("factory"); });
    reg.Add("viewModelAttachments", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(weapons_.VisibleAttachments()); });
    reg.Add("renderedAttachmentError", [this]() -> Runtime::Agent::FAgentQueryValue { return double(weapons_.RenderedAttachmentError()); });
    reg.Add("heldAttachments", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(rig_.HeldAttachmentCount()); });
    reg.Add("rangeHits", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(rangeHits_); });
    reg.Add("displayLoot", [this]() -> Runtime::Agent::FAgentQueryValue
    {
        int64_t count = 0;
        for (const NextDayz::FWorldAnchorHandle handle : world_.anchors.Find(NextDayz::EWorldAnchorType::Loot))
            if (const auto* anchor = world_.anchors.Resolve(handle); anchor && !anchor->item.empty()) ++count;
        return count;
    });
    reg.Add("activeMapId", [this]() -> Runtime::Agent::FAgentQueryValue { return ActiveMapId(); });
    reg.Add("sceneGeneration", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(geographySceneGeneration_); });
    reg.Add("mapLoading", [this]() -> Runtime::Agent::FAgentQueryValue { return pendingCampaign_.has_value() || !loadingMapPath_.empty(); });
    reg.Add("publishedMaps", [this]() -> Runtime::Agent::FAgentQueryValue
    { return int64_t(std::count_if(expeditionMaps_.begin(), expeditionMaps_.end(), [](const auto& map) { return map.published; })); });
    reg.Add("currentMapSorties", [this]() -> Runtime::Agent::FAgentQueryValue
    { const auto count = campaign_.mapSorties.find(ActiveMapId()); return int64_t(count == campaign_.mapSorties.end() ? 0 : count->second); });
    reg.Add("atlasPolygons", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(atlasPolygons_.size()); });
    reg.Add("browsedMapId", [this]() -> Runtime::Agent::FAgentQueryValue
    {
        int index = 0;
        for (const auto& map : expeditionMaps_)
            if (map.published || authoringMode_)
            {
                if (index++ == selectedWorldMap_) return map.id;
            }
        return std::string{};
    });
    reg.Add("safeSpawns", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(world_.anchors.Count(NextDayz::EWorldAnchorType::PlayerSafe)); });
    reg.Add("campaignInField", [this]() -> Runtime::Agent::FAgentQueryValue { return campaign_.inField; });
    reg.Add("campaignGold", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(campaign_.gold); });
    reg.Add("campaignStorage", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(campaign_.storage.size()); });
    reg.Add("campaignSorties", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(campaign_.sorties); });
    reg.Add("campaignExtractions", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(campaign_.extractions); });
    reg.Add("campaignSaveBlocked", [this]() -> Runtime::Agent::FAgentQueryValue { return campaignSaveBlocked_; });
    reg.Add("campaignSaveError", [this]() -> Runtime::Agent::FAgentQueryValue { return saveError_; });
    reg.Add("flightPhase", [this]() -> Runtime::Agent::FAgentQueryValue { return std::string(NextDayz::FlightPhaseName(campaign_.flight.phase)); });
    reg.Add("flightRemaining", [this]() -> Runtime::Agent::FAgentQueryValue { return double(campaign_.flight.remaining); });
    reg.Add("flightBoarded", [this]() -> Runtime::Agent::FAgentQueryValue { return campaign_.flight.boarded; });
    reg.Add("signalFlares", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(inventory_.CountOf("signal_flare")); });
    reg.Add("worldSeconds", [this]() -> Runtime::Agent::FAgentQueryValue { return loot_.WorldSeconds(); });
    reg.Add("worldCooling", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(loot_.CooldownCount()); });
    reg.Add("extractionPoints", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(world_.anchors.Count(NextDayz::EWorldAnchorType::Extraction)); });
    reg.Add("atHospitalRoofHeight", [this]() -> Runtime::Agent::FAgentQueryValue
    {
        for (const auto& anchor : world_.anchors.All())
            if (anchor.type == NextDayz::EWorldAnchorType::Extraction && anchor.profile == "gorka_hospital")
                return player_.Position().y >= anchor.worldPos.y - 0.5f;
        return false;
    });
    reg.Add("atHospitalHelipad", [this]() -> Runtime::Agent::FAgentQueryValue
    {
        for (const auto& anchor : world_.anchors.All())
            if (anchor.type == NextDayz::EWorldAnchorType::Extraction && anchor.profile == "gorka_hospital")
                return glm::distance(player_.Position(), anchor.worldPos) <= extractionConfig_.callRadius;
        return false;
    });
    reg.Add("atHospitalStairLanding", [this]() -> Runtime::Agent::FAgentQueryValue
    {
        for (const auto& anchor : world_.anchors.All())
            if (anchor.type == NextDayz::EWorldAnchorType::Extraction && anchor.profile == "gorka_hospital")
                return player_.Position().y >= anchor.worldPos.y + 0.25f;
        return false;
    });
    reg.Add("sceneReady", [this]() -> Runtime::Agent::FAgentQueryValue { return sceneReady_; });
    reg.Add("inventoryOpen", [this]() -> Runtime::Agent::FAgentQueryValue { return showInventory_; });
    reg.Add("inventoryPreviewReady", [this]() -> Runtime::Agent::FAgentQueryValue
    {
        const auto* provider = GetEngine().GetRenderer().ViewServices().FindProvider("NextDayzLoadout");
        return provider && static_cast<const NextDayz::InventoryPreview*>(provider)->IsReady();
    });
    reg.Add("inventoryIconCount", [this]() -> Runtime::Agent::FAgentQueryValue
    {
        const auto* provider = GetEngine().GetRenderer().ViewServices().FindProvider("NextDayzItemIcons");
        return int64_t(provider ? static_cast<const NextDayz::ItemIconCache*>(provider)->ReadyCount() : 0);
    });
    reg.Add("inventoryIconsRendered", [this]() -> Runtime::Agent::FAgentQueryValue
    {
        const auto* provider = GetEngine().GetRenderer().ViewServices().FindProvider("NextDayzItemIcons");
        return int64_t(provider ? static_cast<const NextDayz::ItemIconCache*>(provider)->RenderedCount() : 0);
    });
    reg.Add("inventoryIconCacheHits", [this]() -> Runtime::Agent::FAgentQueryValue
    {
        const auto* provider = GetEngine().GetRenderer().ViewServices().FindProvider("NextDayzItemIcons");
        return int64_t(provider ? static_cast<const NextDayz::ItemIconCache*>(provider)->CacheHitCount() : 0);
    });
    reg.Add("grassInstances", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(grass_.ActiveCount()); });
    reg.Add("grassNearInstances", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(grass_.ActiveCount(0)); });
    reg.Add("grassMidInstances", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(grass_.ActiveCount(1)); });
    reg.Add("grassFarInstances", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(grass_.ActiveCount(2)); });
    reg.Add("grassFarthestDistance", [this]() -> Runtime::Agent::FAgentQueryValue { return double(grass_.MaxActiveDistance()); });
    reg.Add("grassEdgeInstances", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(grass_.EdgeCount()); });
    reg.Add("grassPoolSize", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(grass_.PoolSize()); });
    reg.Add("grassMeshes", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(grass_.MeshCount()); });
    reg.Add("grassInteractionCount", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(grass_.InteractionCount()); });
    reg.Add("grassContactCount", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(grass_.ContactCount()); });
    reg.Add("grassContactChecks", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(grass_.ContactChecks()); });
    reg.Add("grassUpdatedInstances", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(grass_.UpdatedCount()); });
    reg.Add("grassMaxPressure", [this]() -> Runtime::Agent::FAgentQueryValue { return double(grass_.MaxPressure()); });
    reg.Add("grassPoseChanges", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(grass_.PoseChanges()); });
    reg.Add("grassTileBuilds", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(grass_.TileBuilds()); });
    reg.Add("ammoInMag", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(weapons_.AmmoInMag()); });
    reg.Add("ammoReserve",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(weapons_.AmmoReserve(inventory_)); });
    reg.Add("inventoryCount", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(inventory_.Items().size());
    });
    reg.Add("inventoryUsed", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(inventory_.UsedCapacity());
    });
    reg.Add("inventoryCapacity", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(inventory_.TotalCapacity());
    });
    reg.Add("inventoryContents", [this]() -> Runtime::Agent::FAgentQueryValue {
        std::string contents;
        for (const NextDayz::FItemInstance& item : inventory_.Instances())
        {
            const NextDayz::FInventoryContainer* container = inventory_.FindContainer(item.containerId);
            contents += fmt::format("{}@{}:{};", item.defId,
                inventory_.IsWorn(item.instanceId) ? "worn" : container ? container->displayName : "missing", item.count);
        }
        return contents;
    });
    reg.Add("seed", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(config_.Seed);
    });
    reg.Add("inventoryLayout", [this]() -> Runtime::Agent::FAgentQueryValue
    {
        std::string layout;
        for (const auto& item : inventory_.Instances())
        {
            const auto size = NextDayz::Inventory::InstanceSize(item);
            layout += fmt::format("{}@{}:{},{}:{}x{}:{};", item.defId, item.containerId,
                item.placement.x, item.placement.y, size.width, size.height, item.placement.rotated ? 1 : 0);
        }
        return layout;
    });
    reg.Add("runState", [this]() -> Runtime::Agent::FAgentQueryValue {
        return !survival_.IsAlive() ? std::string("dead") : paused_ ? std::string("paused") : std::string("playing");
    });
    reg.Add("health", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(survival_.Snapshot().health);
    });
    reg.Add("hunger", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(survival_.Snapshot().hunger);
    });
    reg.Add("hydration", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(survival_.Snapshot().hydration);
    });
    reg.Add("lastDamageSource", [this]() -> Runtime::Agent::FAgentQueryValue {
        return survival_.Snapshot().lastDamageSource;
    });
    reg.Add("survivalSeconds", [this]() -> Runtime::Agent::FAgentQueryValue {
        return survivalSeconds_;
    });
    reg.Add("currentObjective", [this]() -> Runtime::Agent::FAgentQueryValue {
        return CurrentObjective();
    });
    reg.Add("equippedWeapon",
            [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.ActiveWeaponId(); });
    reg.Add("isReloading", [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.IsReloading(); });
    reg.Add("isSwitching", [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.IsSwitching(); });
    reg.Add("isSwinging", [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.IsSwinging(); });
    reg.Add("meleeStrikeResolved", [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.MeleeStrikeResolved(); });
    reg.Add("weaponActionClip", [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.CurrentWeaponActionClipName(); });
    reg.Add("weaponAction", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::WeaponActionName(weapons_.PresentationAction()));
    });
    reg.Add("weaponActionTime", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(weapons_.PresentationActionTime());
    });
    reg.Add("activeWeaponSlot", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(weapons_.ActiveSlot());
    });
    reg.Add("previousWeaponSlot", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(weapons_.PreviousSlot()); });
    reg.Add("weaponSlot1", [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.SlotWeaponId(0); });
    reg.Add("weaponSlot2", [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.SlotWeaponId(1); });
    reg.Add("weaponSlot3", [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.SlotWeaponId(2); });
    reg.Add("isAiming", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.IsAiming(); });
    reg.Add("adsMode", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.IsAds(); });
    reg.Add("scopeView", [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.IsScopeView(); });
    reg.Add("cameraFov", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.CurrentFov()); });
    reg.Add("firstPerson", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.WantsFirstPerson(); });
    reg.Add("viewBlend", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.ViewBlend()); });
    reg.Add("aimProgress", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.AimProgress()); });
    reg.Add("presentationFirstPerson", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.IsFirstPerson(); });
    reg.Add("scopeBlend", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(weapons_.ScopeBlend()); });
    reg.Add("fpsArmParts", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(weapons_.VisibleArmParts()); });
    reg.Add("fpsArmGripError", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(weapons_.ArmGripError()); });
    reg.Add("fpsArmRenderedError", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(weapons_.RenderedArmError()); });
    reg.Add("freeLookActive", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.IsFreeLooking(); });
    reg.Add("playerYaw", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.Yaw()); });
    reg.Add("playerPitch", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.Pitch()); });
    reg.Add("cameraYaw", [this]() -> Runtime::Agent::FAgentQueryValue {
        const glm::vec3 forward = player_.CameraForward();
        return static_cast<double>(std::atan2(forward.x, forward.z));
    });
    reg.Add("cameraPitch", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(std::asin(glm::clamp(-player_.CameraForward().y, -1.0f, 1.0f)));
    });
    reg.Add("cameraArmLength", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.CameraArmLength());
    });
    reg.Add("cameraArmFraction", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.CameraArmFraction());
    });
    reg.Add("cameraPosX", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.CameraPosition().x);
    });
    reg.Add("cameraPosY", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.CameraPosition().y);
    });
    reg.Add("cameraPosZ", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.CameraPosition().z);
    });
    reg.Add("playerRigVisible", [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.IsVisible(); });
    reg.Add("heldWeaponVisible", [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.IsHeldWeaponVisible(); });
    reg.Add("viewModelVisible", [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.IsViewModelVisible(); });
    reg.Add("adsSightCenterError", [this]() -> Runtime::Agent::FAgentQueryValue {
        glm::vec3 sight, forward;
        if (!weapons_.ViewModelSight(sight, forward)) return 180.0;
        const glm::vec3 eyeToSight = glm::normalize(sight - player_.CameraPosition());
        return static_cast<double>(glm::degrees(std::acos(glm::clamp(glm::dot(eyeToSight, player_.CameraForward()), -1.0f, 1.0f))));
    });
    reg.Add("adsSightAxisError", [this]() -> Runtime::Agent::FAgentQueryValue {
        glm::vec3 sight, forward;
        if (!weapons_.ViewModelSight(sight, forward)) return 180.0;
        return static_cast<double>(glm::degrees(std::acos(glm::clamp(glm::dot(forward, player_.CameraForward()), -1.0f, 1.0f))));
    });
    reg.Add("adsRenderedSightCenterError", [this]() -> Runtime::Agent::FAgentQueryValue {
        glm::vec3 sight, forward;
        if (!weapons_.ViewModelRenderedSight(sight, forward)) return 180.0;
        const glm::vec3 eyeToSight = glm::normalize(sight - player_.CameraPosition());
        return static_cast<double>(glm::degrees(std::acos(glm::clamp(glm::dot(eyeToSight, player_.CameraForward()), -1.0f, 1.0f))));
    });
    reg.Add("adsRenderedSightAxisError", [this]() -> Runtime::Agent::FAgentQueryValue {
        glm::vec3 sight, forward;
        if (!weapons_.ViewModelRenderedSight(sight, forward)) return 180.0;
        return static_cast<double>(glm::degrees(std::acos(glm::clamp(glm::dot(forward, player_.CameraForward()), -1.0f, 1.0f))));
    });
    reg.Add("adsEyeRelief", [this]() -> Runtime::Agent::FAgentQueryValue {
        glm::vec3 sight, forward;
        if (!weapons_.ViewModelSight(sight, forward)) return -1.0;
        return static_cast<double>(glm::dot(sight - player_.CameraPosition(), player_.CameraForward()));
    });
    reg.Add("elbowConstraintError", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(rig_.ElbowConstraintErrorDegrees());
    });
    reg.Add("leftHandHeight", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(rig_.HandHeightInTorso(0));
    });
    reg.Add("rightHandHeight", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(rig_.HandHeightInTorso(1));
    });
    reg.Add("backWeapon1Visible", [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.IsHolsteredWeaponVisible(0); });
    reg.Add("backWeapon2Visible", [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.IsHolsteredWeaponVisible(1); });
    reg.Add("waistWeaponVisible", [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.IsHolsteredWeaponVisible(2); });
    reg.Add("waistWeaponX", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(rig_.HolsterPosition(2).x); });
    reg.Add("waistWeaponY", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(rig_.HolsterPosition(2).y); });
    reg.Add("waistMuzzleVertical", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(rig_.HolsteredMuzzleVertical(2)); });
    reg.Add("weapon3HolsterDistance", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(rig_.WeaponDistanceFromHolster(2)); });
    reg.Add("backWeapon1X", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(rig_.HolsterPosition(0).x); });
    reg.Add("backWeapon2X", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(rig_.HolsterPosition(1).x); });
    reg.Add("weapon1BackDistance", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(rig_.WeaponDistanceFromHolster(0));
    });
    reg.Add("weapon2BackDistance", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(rig_.WeaponDistanceFromHolster(1));
    });
    reg.Add("clothingVisuals", [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.ClothingVisuals(); });
    reg.Add("bareTorsoVisible", [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.IsBodyPartVisible("bone_torso"); });
    reg.Add("shortsVisible", [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.IsBodyPartVisible("bone_pelvis"); });
    reg.Add("playerRigOpacity", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(rig_.CameraOpacity());
    });
    reg.Add("hoveredLoot", [this]() -> Runtime::Agent::FAgentQueryValue { return loot_.HoveredPrompt(); });
    reg.Add("lootRemaining",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(loot_.RemainingCount()); });
    reg.Add("lootCooldown",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(loot_.CooldownCount()); });
    reg.Add("lootSpots", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(loot_.SpotCount()); });
    reg.Add("lootPiles", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(loot_.PileCount()); });
    reg.Add("lootZones", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(loot_.Zones().size());
    });
    reg.Add("poiCount", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(world_.anchors.Count(NextDayz::EWorldAnchorType::Poi));
    });
    reg.Add("zombieCap", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(world_.spawns.GlobalCap());
    });
    reg.Add("zombieSpawnPoints", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(world_.spawns.Points().size());
    });
    reg.Add("bleeding", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(survival_.Snapshot().bleedingWounds);
    });
    reg.Add("cold", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(survival_.Snapshot().cold); });
    reg.Add("armor", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(inventory_.Armor()); });
    reg.Add("weather", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::WeatherName(time_.Weather()));
    });
    reg.Add("weatherBlend", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(time_.Storm());
    });
    reg.Add("fogDensity", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(GetEngine().GetScene().GetEnvSettings().Atmosphere.FogDensity);
    });
    reg.Add("sunIntensity", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(GetEngine().GetScene().GetEnvSettings().SunIntensity);
    });
    reg.Add("lanternLit", [this]() -> Runtime::Agent::FAgentQueryValue { return lanternLit_; });
    reg.Add("cacheMarked", [this]() -> Runtime::Agent::FAgentQueryValue { return cacheMarker_.has_value(); });
    reg.Add("showMap", [this]() -> Runtime::Agent::FAgentQueryValue { return showMap_; });
    reg.Add("language", []() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::L10n::Language() == NextDayz::L10n::ELanguage::Chinese ? "zhCN" : "en");
    });
    reg.Add("lastNotification", [this]() -> Runtime::Agent::FAgentQueryValue {
        return notifications_.empty() ? std::string() : notifications_.back().text.Display();
    });
    reg.Add("itemCount", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(inventory_.Instances().size());
    });
    reg.Add("worldZombieAnchors", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(world_.anchors.Count(NextDayz::EWorldAnchorType::Zombie));
    });
    reg.Add("worldLootAnchors", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(world_.anchors.Count(NextDayz::EWorldAnchorType::Loot));
    });
    reg.Add("activeZombies", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(world_.zombies.ActiveCount());
    });
    reg.Add("alertedZombies", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(world_.zombies.AlertedCount());
    });
    reg.Add("zombieKills", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(world_.zombies.KillCount());
    });
    reg.Add("lastHitZombie", [this]() -> Runtime::Agent::FAgentQueryValue {
        const NextDayz::FZombieHandle handle = world_.combat.LastHitZombie();
        return handle.IsValid() ? static_cast<int64_t>(handle.index) : static_cast<int64_t>(-1);
    });
    reg.Add("hour", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(time_.HourInt()); });
    reg.Add("posX", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.Position().x); });
    reg.Add("posY", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.Position().y); });
    reg.Add("posZ", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.Position().z); });
    reg.Add("stance", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::StanceName(player_.LocomotionState().actualStance));
    });
    reg.Add("desiredStance", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::StanceName(player_.LocomotionState().desiredStance));
    });
    reg.Add("standBlocked",
            [this]() -> Runtime::Agent::FAgentQueryValue { return player_.LocomotionState().standBlocked; });
    reg.Add("controllerHeight",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.ControllerHeight()); });
    reg.Add("gait", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::GaitName(player_.LocomotionState().gait));
    });
    reg.Add("jumpPhase", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::JumpPhaseName(player_.LocomotionState().jumpPhase));
    });
    reg.Add("jumpPhaseTime", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().jumpPhaseTime01);
    });
    reg.Add("traversalAction", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::TraversalActionName(
            player_.LocomotionState().traversalAction));
    });
    reg.Add("traversalTime", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().traversalTime01);
    });
    reg.Add("traversalHeight", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().traversalHeight);
    });
    reg.Add("traversalProbe", [this]() -> Runtime::Agent::FAgentQueryValue {
        return player_.TraversalProbeResult();
    });
    reg.Add("onGround",
            [this]() -> Runtime::Agent::FAgentQueryValue { return player_.LocomotionState().onGround; });
    reg.Add("verticalVelocity", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().worldVelocity.y);
    });
    reg.Add("velocityX", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().worldVelocity.x);
    });
    reg.Add("velocityZ", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().worldVelocity.z);
    });
    reg.Add("localMoveX", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().localMove.x);
    });
    reg.Add("localMoveY", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().localMove.y);
    });
    reg.Add("baseAnim",
            [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.CurrentBaseClipName(); });
    reg.Add("aimWeight",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(rig_.AimWeight()); });
    reg.Add("weaponAimError", [this]() -> Runtime::Agent::FAgentQueryValue {
        glm::vec3 position, forward;
        if (!rig_.WeaponMuzzle(position, forward)) return 180.0;
        return static_cast<double>(glm::degrees(std::acos(glm::clamp(glm::dot(forward, player_.Forward()), -1.0f, 1.0f))));
    });
    reg.Add("horizontalSpeed",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.HorizontalSpeed()); });
    reg.Add("sprintAlpha",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.SprintAlpha()); });
    reg.Add("weaponReady", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.WeaponReady(); });
    reg.Add("bodyYawLag", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(std::remainder(player_.Yaw() - rig_.BodyYaw(), glm::two_pi<float>()));
    });
    reg.Add("turningInPlace", [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.TurningInPlace(); });
    reg.Add("leanRoll",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(rig_.LeanRollDegrees()); });
    reg.Add("gripError",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(rig_.GripError()); });
    reg.Add("plantError",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(rig_.PlantError()); });
    reg.Add("shotSequence", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(weapons_.LastShotSequence());
    });
    reg.Add("recoilActive", [this]() -> Runtime::Agent::FAgentQueryValue {
        return player_.RecoilActive() || weapons_.ViewModelRecoilActive() || rig_.RecoilActive();
    });
    reg.Add("recoilCamera", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.RecoilActive(); });
    reg.Add("recoilViewModel",
            [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.ViewModelRecoilActive(); });
    reg.Add("recoilRig", [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.RecoilActive(); });
    reg.Add("cameraRecoilPitch", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.CameraRecoil().y);
    });
    reg.Add("action", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::ActionName(actions_.Action()));
    });
    reg.Add("actionTime", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(actions_.NormalizedTime());
    });
    reg.Add("debugVisible", [this]() -> Runtime::Agent::FAgentQueryValue { return debugger_.Visible(); });
    reg.Add("debugZombieOverlay", [this]() -> Runtime::Agent::FAgentQueryValue {
        return debugger_.LayerOn(NextDayz::Debug::LayerInfected);
    });
    reg.Add("debugLootOverlay", [this]() -> Runtime::Agent::FAgentQueryValue {
        return debugger_.LayerOn(NextDayz::Debug::LayerLoot);
    });
    reg.Add("debugHitProxyOverlay", [this]() -> Runtime::Agent::FAgentQueryValue {
        return debugger_.LayerOn(NextDayz::Debug::LayerHitboxes);
    });
    reg.Add("debugLayers", [this]() -> Runtime::Agent::FAgentQueryValue { return debugger_.LayerString(); });
    reg.Add("debugTab", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::Debug::TabName(debugger_.Tab()));
    });
    reg.Add("debugCursor", [this]() -> Runtime::Agent::FAgentQueryValue { return debugger_.CursorMode(); });
    reg.Add("mouseCaptured", [this]() -> Runtime::Agent::FAgentQueryValue { return mouseCaptured_; });
    reg.Add("debugFrozen", [this]() -> Runtime::Agent::FAgentQueryValue { return debugger_.Knobs().frozen; });
    reg.Add("debugFrame", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(debugger_.Frame());
    });
    reg.Add("debugEvents", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(debugger_.Events().TotalPushed());
    });
    reg.Add("debugLastEvent", [this]() -> Runtime::Agent::FAgentQueryValue {
        const NextDayz::Debug::FEvent* event = debugger_.Events().Newest();
        return event ? fmt::format("[{}] {}", NextDayz::Debug::ChannelName(event->channel), event->text) : std::string();
    });
    reg.Add("debugWarnings", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(debugger_.Warnings().size());
    });
    reg.Add("debugWarning", [this]() -> Runtime::Agent::FAgentQueryValue {
        return debugger_.Warnings().empty() ? std::string() : debugger_.Warnings().front().text.Display();
    });
    reg.Add("debugSelected", [this]() -> Runtime::Agent::FAgentQueryValue { return debugger_.SelectionLabel(); });
    reg.Add("debugSnapshot", [this]() -> Runtime::Agent::FAgentQueryValue { return debugger_.LastSnapshotPath(); });
    reg.Add("debugTickMs", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(debugger_.Timers().Stat(NextDayz::Debug::ETimer::Tick).averageMs);
    });
    reg.Add("spawnVerdict", [this]() -> Runtime::Agent::FAgentQueryValue {
        // The nearest spawn point's last verdict: why the closest anchor did or did not spawn.
        const NextDayz::FZombieSpawnPoint* nearest = nullptr;
        for (const NextDayz::FZombieSpawnPoint& point : world_.spawns.Points())
        {
            if (!nearest || point.playerDistance < nearest->playerDistance) nearest = &point;
        }
        return std::string(nearest ? NextDayz::SpawnVerdictName(nearest->verdict) : "none");
    });
    reg.Add("pathSolves", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(world_.zombies.PathSolvesLastUpdate());
    });
    reg.Add("zombieNavReady", [this]() -> Runtime::Agent::FAgentQueryValue {
        return world_.NavigationReady();
    });
    reg.Add("navBuildCount", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(world_.NavigationBuildCount()); });
    reg.Add("navBuildMs", [this]() -> Runtime::Agent::FAgentQueryValue { return world_.NavigationBuildMs(); });
    reg.Add("navSampledCells", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(world_.Navigation().SampledCellCount());
    });
    reg.Add("navApplyPeakMs", [this]() -> Runtime::Agent::FAgentQueryValue { return world_.NavigationApplyPeakMs(); });
    reg.Add("navBuildFrameCount", [this]() -> Runtime::Agent::FAgentQueryValue { return int64_t(navBuildFrames_.frames); });
    reg.Add("navBuildFrameP95Ms", [this]() -> Runtime::Agent::FAgentQueryValue { return navBuildFrames_.P95(); });
    reg.Add("navIdleFrameP95Ms", [this]() -> Runtime::Agent::FAgentQueryValue { return navIdleFrames_.P95(); });
    reg.Add("navFramePeakMs", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::max(navBuildFrames_.peakMs, navIdleFrames_.peakMs);
    });
    reg.Add("zombiePosX", [this]() -> Runtime::Agent::FAgentQueryValue {
        const auto* zombie = FocusZombie();
        return static_cast<double>(zombie ? zombie->position.x : 0.0f);
    });
    reg.Add("zombiePosY", [this]() -> Runtime::Agent::FAgentQueryValue {
        const auto* zombie = FocusZombie();
        return static_cast<double>(zombie ? zombie->position.y : 0.0f);
    });
    reg.Add("zombiePosZ", [this]() -> Runtime::Agent::FAgentQueryValue {
        const auto* zombie = FocusZombie();
        return static_cast<double>(zombie ? zombie->position.z : 0.0f);
    });
    // Infected with live skeleton hitboxes / their capsules (names kept from the proxy era).
    reg.Add("hitProxyRegistered", [this]() -> Runtime::Agent::FAgentQueryValue {
        int64_t bodies = 0;
        for (const NextDayz::FZombieRuntime& zombie : world_.zombies.Slots())
            bodies += zombie.active && !world_.combat.Hitboxes(zombie.handle).empty() ? 1 : 0;
        return bodies;
    });
    reg.Add("hitProxyCpuEligible", [this]() -> Runtime::Agent::FAgentQueryValue {
        int64_t capsules = 0;
        for (const NextDayz::FZombieRuntime& zombie : world_.zombies.Slots())
            capsules += zombie.active ? static_cast<int64_t>(world_.combat.Hitboxes(zombie.handle).size()) : 0;
        return capsules;
    });
    reg.Add("lastHitZone", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(world_.combat.LastHitZombie().IsValid() ? HitZoneName(world_.combat.LastHitZone()) : "none");
    });
    reg.Add("headshots", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(world_.combat.HeadshotCount());
    });
    reg.Add("zombieStaggers", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(world_.zombies.StaggerCount());
    });
    reg.Add("zombieState", [this]() -> Runtime::Agent::FAgentQueryValue {
        const NextDayz::FZombieRuntime* zombie = FocusZombie();
        return std::string(zombie ? ZombieStateName(zombie->state) : "none");
    });
    reg.Add("zombieHealth", [this]() -> Runtime::Agent::FAgentQueryValue {
        const NextDayz::FZombieRuntime* zombie = FocusZombie();
        return static_cast<double>(zombie ? zombie->health : 0.0f);
    });
    reg.Add("zombieSpeed", [this]() -> Runtime::Agent::FAgentQueryValue {
        const NextDayz::FZombieRuntime* zombie = FocusZombie();
        return static_cast<double>(zombie ? zombie->Speed() : 0.0f);
    });
    reg.Add("zombieCrippled", [this]() -> Runtime::Agent::FAgentQueryValue {
        const NextDayz::FZombieRuntime* zombie = FocusZombie();
        return zombie != nullptr && zombie->crippled;
    });
    reg.Add("zombieDeath", [this]() -> Runtime::Agent::FAgentQueryValue {
        const NextDayz::FZombieRuntime* zombie = FocusZombie();
        if (!zombie || zombie->state != NextDayz::EZombieState::Dead) return std::string("none");
        return std::string(NextDayz::ZombieDeathName(zombie->death));
    });
    reg.Add("zombieBaseClip", [this]() -> Runtime::Agent::FAgentQueryValue {
        const NextDayz::FZombieRuntime* zombie = FocusZombie();
        if (!zombie || zombie->handle.index >= zombieRigVisuals_.size()) return std::string("none");
        return std::string(zombieRigVisuals_[zombie->handle.index].BaseClipName());
    });
    reg.Add("zombieActionClip", [this]() -> Runtime::Agent::FAgentQueryValue {
        const NextDayz::FZombieRuntime* zombie = FocusZombie();
        if (!zombie || zombie->handle.index >= zombieRigVisuals_.size()) return std::string("none");
        return std::string(zombieRigVisuals_[zombie->handle.index].ActionClipName());
    });
    reg.Add("zombieFlinch", [this]() -> Runtime::Agent::FAgentQueryValue {
        double flinch = 0.0;
        for (const NextDayz::ZombieRigVisual& visual : zombieRigVisuals_)
            flinch = std::max(flinch, static_cast<double>(visual.FlinchDegrees()));
        return flinch;
    });
    reg.Add("hitMarker", [this]() -> Runtime::Agent::FAgentQueryValue { return hitMarkerSeconds_ > 0.0f; });
    reg.Add("hitMarkerKill", [this]() -> Runtime::Agent::FAgentQueryValue {
        return hitMarkerSeconds_ > 0.0f && hitMarkerKill_;
    });
    reg.Add("hurt", [this]() -> Runtime::Agent::FAgentQueryValue { return hurtSeconds_ > 0.0f; });
    reg.Add("audioPlayed", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(audio_.PlayedCount());
    });
    reg.Add("ambienceDay", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(audio_.AmbienceMix().day);
    });
    reg.Add("ambienceEvening", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(audio_.AmbienceMix().evening);
    });
    reg.Add("ambienceNight", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(audio_.AmbienceMix().night);
    });
    reg.Add("ambienceBirds", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(audio_.AmbienceMix().birds);
    });
    reg.Add("ambienceOwls", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(audio_.AmbienceMix().owls);
    });
    reg.Add("ambienceWind", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(audio_.AmbienceMix().wind);
    });
    reg.Add("fxParticles", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(effects_.ActiveParticles());
    });
    reg.Add("fxTracers", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(effects_.ActiveTracers());
    });
    reg.Add("fxMuzzleFlash", [this]() -> Runtime::Agent::FAgentQueryValue { return effects_.FlashVisible(); });
    reg.Add("zombiePathSegments", [this]() -> Runtime::Agent::FAgentQueryValue {
        int64_t segments = 0;
        for (const NextDayz::FZombieRuntime& zombie : world_.zombies.Slots())
        {
            if (zombie.active && zombie.pathWaypoint < zombie.path.size())
                segments += static_cast<int64_t>(zombie.path.size() - zombie.pathWaypoint);
        }
        return segments;
    });
    reg.Add("lootDebugEntries", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(loot_.Entries().size());
    });
    reg.Add("lootMissingCategories", [this]() -> Runtime::Agent::FAgentQueryValue {
        std::array<bool, 6> present{};
        for (const NextDayz::FLootEntry& entry : loot_.Entries())
        {
            if (entry.state != NextDayz::FLootEntry::EState::Available) continue;
            present[static_cast<size_t>(entry.category)] = true;
        }
        return static_cast<int64_t>(std::count(present.begin(), present.end(), false));
    });
    reg.Add("recentWeaponTraces", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(debugger_.Traces().size());
    });
    reg.Add("zombieOffset", [this]() -> Runtime::Agent::FAgentQueryValue {
        const NextDayz::FZombieRuntime* zombie = FocusZombie();
        if (!zombie) return std::string("none");
        const glm::vec3 delta = zombie->position - player_.Position();
        return fmt::format("{:.2f} {:.2f} {:.2f} hitboxes {}", delta.x, delta.y, delta.z,
                           world_.combat.Hitboxes(zombie->handle).size());
    });
    reg.Add("lastTraceNode", [this]() -> Runtime::Agent::FAgentQueryValue {
        if (debugger_.Traces().empty()) return std::string("none");
        const NextDayz::FWeaponTraceEvent& trace = debugger_.Traces().back().trace;
        const Assets::Node* node = trace.hitInstanceId ? GetEngine().GetScene().GetNodeByInstanceId(trace.hitInstanceId)
                                                       : nullptr;
        return fmt::format("{} @ {:.2f} m", node ? node->GetName() : std::string("-"),
                           glm::distance(trace.origin, trace.endPoint));
    });
    reg.Add("lastTraceResult", [this]() -> Runtime::Agent::FAgentQueryValue {
        if (debugger_.Traces().empty()) return std::string("none");
        const NextDayz::FWeaponTraceEvent& trace = debugger_.Traces().back().trace;
        if (!trace.hit) return std::string("miss");
        return trace.hitZombie ? std::string("zombie-hitbox") : std::string("world-blocker");
    });
}
