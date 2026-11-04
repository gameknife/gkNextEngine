#include "NextDayzGameInstance.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

#include <SDL3/SDL_events.h>
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
#include "Modules/ScadLoader/ScadModule.hpp"

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
    ConfigureWindow(config, options, "NextDayz", 1920, 1080, false);
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
    zombies_.Configure(config_.Zombie);
    audio_.SetEngine(&GetEngine());
    weapons_.SetTargetTracer([this](const glm::vec3& origin, const glm::vec3& direction, float maxDistance)
    {
        return combat_.TraceHitboxes(origin, direction, maxDistance);
    });

    if (!rig_.LoadRig(kSoldierRig))
    {
        SPDLOG_WARN("[NextDayz] soldier rig unavailable; player will have no visual");
    }
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

    std::string scene = kDefaultScene;
    if (!GOption->SceneName.empty())
    {
        scene = GOption->SceneName;
    }
    SPDLOG_INFO("[NextDayz] loading scene '{}'", scene);
    GetEngine().RequestLoadScene({.filename = scene});
}

void NextDayzGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
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
    cvars.RegisterFloat("nextdayz.grass.windStrength", 0.65f, &grass_.settings.windStrength, NextCVar::ECVarFlags::None,
                        "Wind bending strength", nullptr, 0.0, 1.0);
    cvars.RegisterFloat("nextdayz.grass.windYaw", 0.7f, &grass_.settings.windYaw, NextCVar::ECVarFlags::None,
                        "Wind heading in radians: +X toward -Z");
    cvars.RegisterFloat("nextdayz.grass.windSpeed", 1.0f, &grass_.settings.windSpeed, NextCVar::ECVarFlags::None,
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
    cvars.RegisterBool("nextdayz.validation.addBackpack", false, &validationAddBackpack_, NextCVar::ECVarFlags::None,
                       "Agent-only capacity fixture", [this, validationReady]()
                       {
                           if (!validationAddBackpack_ || !validationReady()) return;
                           NextDayz::FItemInstanceId id = 0;
                           if (inventory_.TryAdd("backpack", "Backpack", NextDayz::EItemKind::Clothing, 1, &id))
                           {
                               inventory_.TryEquip(id);
                               rig_.SetClothing("backpack", true);
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
                           for (const NextDayz::FZombieRuntime& zombie : zombies_.Slots())
                           {
                               if (zombie.active && zombie.state != NextDayz::EZombieState::Dead)
                               {
                                   combat_.ProcessMelee(++validationAttackSequence_, zombie.handle, 200.0f,
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
                                survival_.ApplyDamage(validationDamagePlayer_, "validation", false);
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
}

void NextDayzGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                              std::vector<Assets::Model>& models,
                                              std::vector<Assets::FMaterial>& materials,
                                              std::vector<Assets::LightObject>& /*lights*/,
                                              std::vector<Assets::AnimationTrack>& /*tracks*/)
{
    grass_.Prepare(nodes, models, materials);
    rig_.InjectAssets(models, materials);
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
}

glm::vec3 NextDayzGameInstance::ResolveSpawnPosition() const
{
    // Prefer spawning on the same ground as nearby supplies: a plain downward
    // raycast at the gas station hits the canopy roof, not the pad. Using a loot
    // item's world position gives real ground height and puts the player facing
    // it (loot is placed a few metres ahead along +Z, our default yaw).
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

void NextDayzGameInstance::OnSceneLoaded()
{
    debugger_.OnSessionStart("scene loaded");
    worldAnchors_.Scan(GetEngine());
    BuildZombieNavigation();
    zombies_.Reset();
    combat_.Reset();
    noise_.Reset();
    zombieVisuals_.Reset();
    zombieSpawns_.Reset(sessionRng_.Derive(0x5A4F4D42ULL).Seed());
    ConfigureZombieSpawnPoints();
    // Loot scan first so the spawn can be derived from real supply ground height.
    loot_.OnSceneLoaded(GetEngine().GetScene(), CollectLootSpots());

    const glm::vec3 spawn = ResolveSpawnPosition();
    player_.Create(GetEngine().GetPhysicsEngine(), spawn, config_);

    rig_.OnSceneLoaded(GetEngine().GetScene());
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
    if (validationLoadout_ && GOption->AgentValidation)
    {
        ApplyValidationLoadout();
    }

    showInventory_ = false;
    paused_ = false;
    survivalSeconds_ = 0.0;
    SetMouseCaptured(true);
    sceneReady_ = true;
    grass_.Bind(GetEngine().GetScene());
    grass_.Tick(GetEngine().GetScene(), 0.0f, player_.Position(), time_.Storm());
    SPDLOG_INFO("[NextDayz] scene loaded ({} nodes)", GetEngine().GetScene().Nodes().size());
}

void NextDayzGameInstance::OnSceneUnloaded()
{
    sceneReady_ = false;
    grass_.Clear();
    player_.Destroy();
    rig_.OnSceneUnloaded();
    weapons_.OnSceneUnloaded();
    effects_.OnSceneUnloaded();
    loot_.OnSceneUnloaded();
    worldAnchors_.Clear();
    inventory_.Clear();
    actions_.Reset();
    survival_.Reset();
    zombies_.Reset();
    combat_.Reset();
    noise_.Reset();
    zombieSpawns_.Reset(sessionRng_.Derive(0x5A4F4D42ULL).Seed());
    zombieVisuals_.Reset();
    zombieNavGrid_ = {};
    zombieVisualOwners_.clear();
    zombieRigVisuals_.clear();
    zombieAudio_.clear();
    notifications_.clear();
    firecrackers_.clear();
}

void NextDayzGameInstance::OnDestroy()
{
    player_.Destroy();
}

void NextDayzGameInstance::OnTick(double deltaSeconds)
{
    debugger_.BeginFrame();
    if (!sceneReady_ || !player_.IsValid())
    {
        return;
    }
    float dt = static_cast<float>(deltaSeconds);
    if (paused_)
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
}

void NextDayzGameInstance::SimulateFrame(float dt)
{
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
    }
    if (const std::optional<NextDayz::FLootHandle> commit = actions_.ConsumeCommitRequest())
    {
        const NextDayz::FLootCommitResult result = loot_.Commit(*commit, inventory_, GetEngine(),
            std::exchange(lootDestination_, 0), std::exchange(lootEquipSlot_, NextDayz::EEquipSlot::None));
        if (result.taken)
        {
            SyncClothingVisuals();
            audio_.Pickup();
            for (const NextDayz::FLootStack& stack : result.stacks)
            {
                Notify(stack.count > 1 ? fmt::format("Picked up {} x{}", ItemDisplayName(stack.itemId), stack.count)
                                       : fmt::format("Picked up {}", ItemDisplayName(stack.itemId)));
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
    if (actionLocked || traversalLocked || showInventory_ || showMap_ || dead)
    {
        player_.SetAiming(false);
        weapons_.SetTriggerDown(false);
        weapons_.CancelMelee();
    }
    player_.SetSprintBlocked((weapons_.TriggerDown() && weapons_.HasActiveWeapon()) || weapons_.IsSwinging() ||
                             weapons_.MeleeInputPending() || survival_.IsWeak());
    player_.Update(dt, GetEngine());
    section.emplace(timers, ETimer::Survival);
    // Cold: night and bad weather against whatever the player is wearing.
    survival_.SetCold(std::clamp(time_.Chill() - inventory_.Warmth() * 0.8f, 0.0f, 1.0f));
    if (!knobs.freezeNeeds)
    {
        survival_.Update(dt, player_.LocomotionState().gait == NextDayz::EPlayerGait::Sprint);
    }
    section.emplace(timers, ETimer::Weapons);
    weapons_.Update(dt, player_, inventory_, GetEngine());
    if (weapons_.ConsumeDryFire())
    {
        audio_.DryFire();
        debugger_.Log(NextDayz::Debug::EChannel::Weapon, 0, fmt::format("Dry fire {}", weapons_.ActiveWeaponId()));
    }
    if (const NextDayz::EWeaponPresentationAction action = weapons_.PresentationAction(); action != lastWeaponAction_)
    {
        if (action == NextDayz::EWeaponPresentationAction::Reload) audio_.Reload();
        if (action == NextDayz::EWeaponPresentationAction::Switch) audio_.Switch();
        lastWeaponAction_ = action;
    }
    if (weapons_.IsReloading() || weapons_.IsSwitching())
    {
        player_.SetAiming(false);
    }
    for (const NextDayz::FWeaponHitEvent& hit : weapons_.ConsumeHitEvents())
    {
        if (const NextDayz::FWeaponDef* definition = NextDayz::FindWeaponDef(hit.weaponId))
        {
            combat_.ProcessHit(hit, definition->damageFalloffDistance);
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
        }
        debugger_.RecordTrace(trace);
    }
    for (const NextDayz::FShotEvent& shot : weapons_.ConsumeShotEvents())
    {
        lastShotMelee_ = shot.melee;
        debugger_.Log(NextDayz::Debug::EChannel::Weapon, 0,
                      fmt::format("{} {}", shot.melee ? "Swung" : "Fired", shot.weaponId));
        if (shot.melee)
        {
            // Dedicated melee clips own the body and weapon; no firearm recoil.
            audio_.MeleeSwing();
            if (const NextDayz::FWeaponDef* definition = NextDayz::FindWeaponDef(shot.weaponId))
            {
                noise_.Emit(player_.Position(), definition->noiseRadius, 0.5f, NextDayz::ENoiseType::Melee);
            }
            continue;
        }
        if (hasVisualMuzzle)
        {
            const float flash = shot.weaponId == "pistol" ? 0.6f : shot.weaponId == "shotgun" ? 1.25f : 1.0f;
            effects_.MuzzleFlash(visualMuzzle, visualMuzzleForward, flash * (firstPersonView ? 0.55f : 0.6f));
        }
        audio_.Shot(shot.weaponId);
        player_.ApplyCameraRecoil(shot.cameraImpulseRadians);
        rig_.TriggerRecoil(shot.rigRecoilScale);
        if (const NextDayz::FWeaponDef* definition = NextDayz::FindWeaponDef(shot.weaponId))
        {
            const NextDayz::ENoiseType noiseType = shot.weaponId == "shotgun" ? NextDayz::ENoiseType::Shotgun
                : shot.weaponId == "pistol" ? NextDayz::ENoiseType::Pistol : NextDayz::ENoiseType::Rifle;
            noise_.Emit(player_.Position(), definition->noiseRadius, 1.0f, noiseType);
        }
    }

    const auto visibleFromPlayer = [this](const glm::vec3& position)
    {
        const glm::vec3 delta = position - player_.EyePosition();
        const float distance = glm::length(delta);
        return distance > 0.01f && distance < 150.0f && glm::dot(delta / distance, player_.Forward()) > 0.45f;
    };
    section.emplace(timers, ETimer::SpawnDirector);
    zombieSpawns_.Update(dt, player_.Position(), zombies_, visibleFromPlayer,
                         [this](const glm::vec3& point)
                         {
                             return !zombieNavGrid_.IsBuilt() || zombieNavGrid_.IsWalkable(point);
                         });
    const auto lineOfSight = [this](const glm::vec3& from, const glm::vec3& to)
    {
        const glm::vec3 delta = to - from;
        const float distance = glm::length(delta);
        if (distance <= 0.01f)
        {
            return true;
        }
        bool clear = true;
        GetEngine().RayCast(from, delta / distance, [&](Assets::RayCastResult result)
        {
            if (result.Hit && result.T < distance - 0.35f)
            {
                clear = false;
            }
            return true;
        });
        return clear;
    };
    const auto resolvePath = [this](const glm::vec3& from, const glm::vec3& to)
    {
        if (!zombieNavGrid_.IsBuilt())
        {
            return std::vector<glm::vec3>{to};
        }
        return zombieNavGrid_.FindPath(from, to, from.y, config_.Zombie.MaxPathExpansions);
    };
    const float zombieDt = dt * knobs.zombieTimeScale;
    section.emplace(timers, ETimer::InfectedAi);
    zombies_.Update(zombieDt, player_.Position(), player_.Forward(), survival_.IsAlive(),
                    lineOfSight, noise_.Events(), resolvePath);
    // Keep the infected on whatever floor they are over (under a canopy, inside
    // a house) rather than the grid's single top surface.
    section.emplace(timers, ETimer::InfectedGround);
    for (const NextDayz::FZombieRuntime& slot : zombies_.Slots())
    {
        NextDayz::FZombieRuntime* zombie = slot.active ? zombies_.Resolve(slot.handle) : nullptr;
        if (!zombie)
        {
            continue;
        }
        GetEngine().RayCast(zombie->position + glm::vec3(0.0f, 1.2f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
                            [zombie](Assets::RayCastResult result)
                            {
                                if (result.Hit && result.T < 3.5f)
                                {
                                    zombie->position.y = static_cast<float>(result.HitPoint.y);
                                }
                                return true;
                            });
    }
    section.emplace(timers, ETimer::Survival);
    for (const NextDayz::FPlayerDamageRequest& damage : zombies_.ConsumePlayerDamageRequests())
    {
        const bool invulnerable = (validationInvulnerable_ && GOption->AgentValidation) || knobs.god;
        const int woundsBefore = survival_.Snapshot().bleedingWounds;
        const bool landed = !invulnerable &&
            survival_.ApplyInfectedHit(damage.amount, inventory_.Armor(), survivalRng_.UnitFloat());
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
    }
    noise_.Update(dt);
    section.emplace(timers, ETimer::InfectedVisuals);
    SyncZombieVisuals(zombieDt);
    section.emplace(timers, ETimer::Presentation);
    UpdateZombieAudio(zombieDt);
    effects_.Update(dt * knobs.fxTimeScale);

    section.emplace(timers, ETimer::Loot);
    loot_.Update(dt, player_.EyePosition(), player_.Forward(), GetEngine());
    section.emplace(timers, ETimer::World);
    hoveredWell_ = {};
    float bestWellDot = 0.45f;
    for (const NextDayz::FWorldAnchorHandle handle : worldAnchors_.Find(NextDayz::EWorldAnchorType::Well))
    {
        const NextDayz::FWorldAnchor* well = worldAnchors_.Resolve(handle);
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
    audio_.UpdateAmbience(dt, time_.Daylight(), time_.Storm());
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
    rig_.SetVisible(!player_.IsFirstPerson() || knobs.orbit);
    NextDayz::FPlayerPresentationState presentation;
    presentation.locomotion = player_.LocomotionState();
    presentation.aimWeight = player_.IsAiming() ? 1.0f : 0.0f;
    presentation.aimPitchRadians = player_.Pitch();
    presentation.aimYawRadians = player_.Yaw();
    presentation.sprintAlpha = player_.SprintAlpha();
    presentation.hasWeapon = weapons_.HasActiveWeapon();
    presentation.weaponId = weapons_.ActiveWeaponId();
    presentation.weaponAction = weapons_.PresentationAction();
    presentation.weaponActionTime01 = weapons_.PresentationActionTime();
    presentation.activeWeaponSlot = weapons_.ActiveSlot();
    presentation.slotWeaponIds = {
        weapons_.SlotWeaponId(0),
        weapons_.SlotWeaponId(1),
    };
    presentation.action = actions_.Action();
    presentation.actionTime01 = actions_.NormalizedTime();
    rig_.Update(player_.Position(), presentation, dt);
    rig_.UpdateCameraFade(player_.CameraPosition(), config_.Camera.TpsFadeNearDistance,
                          config_.Camera.TpsFadeFarDistance, !player_.IsFirstPerson() && !knobs.orbit);
}

bool NextDayzGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    if (!player_.IsValid())
    {
        return false;
    }
    float fov = config_.Camera.BaseFov;
    player_.FillCamera(outRenderCamera.ModelView, fov);
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
    outRenderCamera.NearPlane = 0.1f;
    outRenderCamera.FarPlane = 1500.f;
    return true;
}

bool NextDayzGameInstance::OnRenderUI()
{
    if (!sceneReady_)
    {
        return false;
    }

    NextDayz::FHudContext ctx;
    ctx.aiming = player_.IsAiming();
    ctx.firstPerson = player_.IsFirstPerson();
    ctx.hasWeapon = weapons_.HasActiveWeapon();
    ctx.melee = weapons_.ActiveIsMelee();
    ctx.swinging = weapons_.IsSwinging();
    ctx.swingProgress = ctx.swinging ? weapons_.PresentationActionTime() : 0.0f;
    ctx.weaponName = weapons_.ActiveDisplayName();
    ctx.ammoInMag = weapons_.AmmoInMag();
    ctx.ammoReserve = weapons_.AmmoReserve(inventory_);
    ctx.reloading = weapons_.IsReloading();
    ctx.activeSlot = weapons_.ActiveSlot();
    ctx.actionActive = actions_.IsActive();
    if (actions_.IsActive())
    {
        ctx.actionLabel = std::string(NextDayz::ActionLabel(actions_.Action()));
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
    ctx.hour = time_.HourInt();
    ctx.minute = time_.MinuteInt();
    ctx.overcast = time_.Overcast();
    ctx.weather = NextDayz::WeatherName(time_.Weather());
    ctx.showInventory = showInventory_;
    ctx.showMap = showMap_;
    ctx.armor = inventory_.Armor();
    ctx.warmth = inventory_.Warmth();
    ctx.weak = survival_.IsWeak();
    ctx.lanternLit = lanternLit_;
    ctx.lanternMinutes = lanternSeconds_ / 60.0f;
    ctx.kills = zombies_.KillCount();
    ctx.hasCompass = inventory_.HasTool(NextDayz::EItemTool::Compass);
    ctx.hasMap = inventory_.HasTool(NextDayz::EItemTool::Map);
    ctx.yawRadians = player_.Yaw();
    ctx.playerPosition = player_.Position();
    ctx.cacheMarker = cacheMarker_;
    ctx.quickSlots = quickSlots_;
    ctx.notifications = notifications_;
    if (showMap_)
    {
        for (const NextDayz::FWorldAnchor& anchor : worldAnchors_.All())
        {
            if (anchor.type == NextDayz::EWorldAnchorType::Poi)
            {
                ctx.pois.push_back({anchor.label, anchor.profile, glm::vec2(anchor.worldPos.x, anchor.worldPos.z)});
            }
        }
    }
    if (showInventory_)
    {
        for (const int index : loot_.Nearby(player_.EyePosition(), config_.Loot.ReachMeters))
        {
            const NextDayz::FLootEntry& entry = loot_.Entries()[index];
            ctx.nearby.push_back({index, entry.displayName, glm::distance(entry.worldPos, player_.EyePosition())});
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
    ctx.equipWeapon = [this, &hudRequests](NextDayz::FItemInstanceId id, int slot)
    {
        hudRequests.emplace_back([this, id, slot]() { EquipWeaponInstance(id, slot); });
    };
    ctx.unequipWeapon = [this](int slot) { weapons_.Unequip(slot); };
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
    ctx.restartSession = [this, &hudRequests]() { hudRequests.emplace_back([this]() { ResetSession(); }); };
    {
        NextDayz::Debug::ScopedTimer timer(debugger_.Timers(), NextDayz::Debug::ETimer::Hud);
        NextDayz::NextDayzHUD::Draw(ctx);
    }
    for (const auto& request : hudRequests)
    {
        request();
    }
    debugger_.Draw(DebugFacts());
    return true;
}

void NextDayzGameInstance::Notify(std::string text, int severity)
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
    for (const NextDayz::FWorldAnchorHandle handle : worldAnchors_.Find(NextDayz::EWorldAnchorType::Loot))
    {
        if (const NextDayz::FWorldAnchor* anchor = worldAnchors_.Resolve(handle))
        {
            const std::optional<NextDayz::ELootProfile> profile = NextDayz::ParseLootProfile(anchor->profile);
            spots.push_back({anchor->worldPos, profile.value_or(NextDayz::ELootProfile::Residential)});
        }
    }
    return spots;
}

void NextDayzGameInstance::SyncClothingVisuals()
{
    // The rig has a helmet and a backpack attachment; any worn head/back item shows them.
    const NextDayz::FItemInstance* head = inventory_.FindInstance(inventory_.WornInSlot(NextDayz::EEquipSlot::Head));
    rig_.SetClothing("helmet", head && head->defId == "helmet");
    rig_.SetClothing("backpack", inventory_.WornInSlot(NextDayz::EEquipSlot::Back) != 0);
}

void NextDayzGameInstance::EquipWeaponInstance(NextDayz::FItemInstanceId instanceId, int slot)
{
    const NextDayz::FItemInstance* instance = inventory_.FindInstance(instanceId);
    if (!instance || !weapons_.EquipInstance(slot, inventory_, instanceId))
    {
        Notify("That is not something you can hold", 1);
        return;
    }
    Notify(fmt::format("{} in hand ({})", ItemDisplayName(instance->defId), slot == 0 ? "1" : "2"));
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
        Notify(fmt::format("Wearing {}", name));
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
    if (!loot_.SpawnPile({{removed->defId, removed->count, removed->loadedAmmo}}, at,
                         survivalRng_.UnitFloat() * 6.28f, GetEngine()))
    {
        Notify("Cannot drop it here", 1);
        return;
    }
    inventory_ = std::move(transaction);
    SyncClothingVisuals();
    audio_.Drop();
    Notify(removed->count > 1 ? fmt::format("Dropped {} x{}", ItemDisplayName(removed->defId), removed->count)
                              : fmt::format("Dropped {}", ItemDisplayName(removed->defId)));
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

void NextDayzGameInstance::SplitItem(NextDayz::FItemInstanceId instanceId)
{
    const NextDayz::FItemInstance* instance = inventory_.FindInstance(instanceId);
    NextDayz::FItemInstanceId split = 0;
    if (!instance || instance->count < 2 || !inventory_.TrySplit(instanceId, instance->count / 2, split))
    {
        Notify("Cannot split that here", 1);
    }
}

void NextDayzGameInstance::TakeLoot(int lootIndex, NextDayz::FContainerId destination, NextDayz::EEquipSlot equipSlot)
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
        player_.SetAiming(false);
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
    case EItemUse::None:
        break;
    }
    if (NextDayz::IsWieldable(*definition))
    {
        EquipWeaponInstance(instanceId, definition->equipSlot == NextDayz::EEquipSlot::PrimaryWeapon ? 0 : 1);
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
        Notify(fmt::format("No {} left", ItemDisplayName(itemId)), 1);
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
    const std::optional<glm::vec3> cache = loot_.PlantCache(player_.Position(), 120.0f, GetEngine());
    if (!cache)
    {
        Notify("Only static on the military band...", 1);
        return;
    }
    cacheMarker_ = *cache;
    cacheSeconds_ = kCacheMarkerSeconds;
    const glm::vec3 delta = *cache - player_.Position();
    Notify(fmt::format("Broadcast: supply cache {:.0f} m to the {} (marked on map and compass)",
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
            noise_.Emit(cracker.position, 75.0f, 1.3f, NextDayz::ENoiseType::Rifle, 2.0f);
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
    float sight = glm::mix(1.0f, 0.55f, night);
    if (lanternLit_)
    {
        sight = std::max(sight, glm::mix(1.0f, 1.4f, night));
    }
    if (player_.LocomotionState().actualStance == NextDayz::EPlayerStance::Crouched)
    {
        sight *= 0.7f;
    }
    if (time_.Weather() == NextDayz::EWeather::Fog)
    {
        sight *= 0.7f;
    }
    zombies_.SetSightScale(sight);
    // More of them walk the night.
    zombieSpawns_.SetGlobalCap(night > 0.6f ? 48 : 40);
    time_.SetLanternLight(lanternLit_ ? 1.0f : 0.0f);
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
            rig_.SetClothing("backpack", true);
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

void NextDayzGameInstance::BuildZombieNavigation()
{
    Assets::Scene& scene = GetEngine().GetScene();
    NextGameplay::FNavGridSettings settings;
    settings.cellSize = 2.0f;
    settings.agentRadius = 0.35f;
    settings.maxSlopeAngle = 48.0f;
    settings.clearanceHeight = 2.1f;
    settings.maxStepHeight = 0.65f;
    settings.sampleCeiling = 100.0f;
    settings.floorHeightTolerance = 2.0f;
    settings.worldMin = {-510.0f, 0.0f, -510.0f};
    settings.worldMax = {510.0f, 0.0f, 510.0f};
    const auto buildStart = std::chrono::steady_clock::now();
    zombieNavGrid_.Build(scene.GetCPUAccelerationStructure(), settings);
    const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart).count();

    Runtime::TerrainComponent* terrain = nullptr;
    for (Runtime::TerrainComponent* candidate : scene.Components<Runtime::TerrainComponent>())
    {
        terrain = candidate;
        break;
    }
    if (terrain)
    {
        // The grid keeps one surface per cell, the highest. Water is out, and
        // so is anything well above the terrain: roofs, canopies and wall tops
        // would otherwise be walked on. The infected go round buildings and
        // only close the last metres to a player inside one directly.
        zombieNavGrid_.MaskUnwalkable([terrain](const glm::vec3& point)
        {
            const bool water = terrain->IsWater(point.x, point.z) &&
                               point.y < terrain->WaterSurface(point.x, point.z) + 0.05f;
            return water || point.y > terrain->SampleHeight(point.x, point.z) + 1.2f;
        });
    }
    SPDLOG_INFO("[NextDayz] infected navigation ready: {}x{} cells at {:.1f} m in {:.0f} ms",
                zombieNavGrid_.GetWidth(), zombieNavGrid_.GetHeight(), zombieNavGrid_.GetCellSize(), buildMs);
}

void NextDayzGameInstance::ConfigureZombieSpawnPoints()
{
    std::vector<NextDayz::FZombieSpawnPoint> zombiePoints;
    for (const NextDayz::FWorldAnchorHandle handle : worldAnchors_.Find(NextDayz::EWorldAnchorType::Zombie))
    {
        const NextDayz::FWorldAnchor* anchor = worldAnchors_.Resolve(handle);
        if (!anchor)
        {
            continue;
        }
        NextDayz::EZombieProfile profile = NextDayz::EZombieProfile::Civilian;
        if (anchor->profile == "military") profile = NextDayz::EZombieProfile::Military;
        else if (anchor->profile == "industrial") profile = NextDayz::EZombieProfile::Industrial;
        else if (anchor->profile == "wilderness") profile = NextDayz::EZombieProfile::Wilderness;
        NextDayz::FZombieSpawnPoint point{anchor->worldPos, profile};
        point.groupSize = anchor->group;
        zombiePoints.push_back(point);
    }
    zombieSpawns_.SetPoints(std::move(zombiePoints));
}

void NextDayzGameInstance::ResetSession()
{
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
    zombies_.Reset();
    combat_.Reset();
    noise_.Reset();
    zombieSpawns_.Reset(sessionRng_.Derive(0x5A4F4D42ULL).Seed());
    ConfigureZombieSpawnPoints();
    zombieVisuals_.Reset();
    effects_.Reset();
    loot_.ResetSession(GetEngine());
    time_.Reset(config_.Time, sessionRng_.Derive(0x57454154ULL).Seed());
    player_.Destroy();
    player_.Create(GetEngine().GetPhysicsEngine(), ResolveSpawnPosition(), config_);
    if (validationLoadout_ && GOption->AgentValidation)
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
        return "Stop the bleeding: bandage or rags [H]";
    }
    if (inventory_.CountOf("water_bottle") == 0 && inventory_.CountOf("drink_soda") == 0)
    {
        return "Find water: a bottle, a soda, or a well or pump in a village";
    }
    bool hasFood = false;
    for (const NextDayz::FItemInstance& instance : inventory_.Instances())
    {
        const NextDayz::FItemDef* definition = NextDayz::FindItemDef(instance.defId);
        hasFood = hasFood || (definition && definition->use == NextDayz::EItemUse::Eat);
    }
    if (!hasFood)
    {
        return "Find food: houses, shops and the supermarket";
    }
    if (inventory_.TotalCapacity() <= 10)
    {
        return "Find a backpack or clothes with pockets";
    }
    if (!weapons_.HasActiveWeapon())
    {
        return "Find something to fight with: a knife, crowbar, axe or a gun";
    }
    if (!inventory_.HasTool(NextDayz::EItemTool::Map) && !inventory_.HasTool(NextDayz::EItemTool::Compass))
    {
        return "Find a map or compass to navigate";
    }
    return "Raid a fortress or the military base for better gear";
}

void NextDayzGameInstance::CreateZombieVisuals()
{
    Assets::Scene& scene = GetEngine().GetScene();
    zombieRigVisuals_.clear();
    zombieRigVisuals_.resize(zombies_.Capacity());
    zombieVisualOwners_.assign(zombies_.Capacity(), {});
    if (zombieRigLoaded_ && !zombieRigPartModelIds_.empty())
    {
        for (size_t index = 0; index < zombies_.Capacity(); ++index)
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
    const auto& slots = zombies_.Slots();
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
                combat_.ClearHitboxes(owner);
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
                combat_.ClearHitboxes(owner);
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
            combat_.ClearHitboxes(zombie.handle);
        }
        else if (visual)
        {
            combat_.SetHitboxes(zombie.handle, visual->Hitboxes());
        }
        else
        {
            const size_t count = NextDayz::ZombieRigVisual::StandingHitboxes(zombie.position, zombie.forward, standing);
            combat_.SetHitboxes(zombie.handle, std::span<const NextDayz::FHitCapsule>(standing.data(), count));
        }
    }
    GetEngine().GetScene().MarkTransformDirty();
}

void NextDayzGameInstance::HandleDamageEvents()
{
    for (const NextDayz::FDamageEvent& damage : combat_.ConsumeDamageEvents())
    {
        debugger_.RecordDamage(damage);
        if (damage.target.index < zombieRigVisuals_.size() && zombieRigVisuals_[damage.target.index].IsBound())
        {
            zombieRigVisuals_[damage.target.index].AddHit(damage, config_.Zombie);
        }
        effects_.BloodHit(damage.worldPoint, damage.direction, -damage.direction, damage.hitZone, damage.killed);
        audio_.Hit(damage.hitZone, damage.killed);
        if (weapons_.ActiveIsMelee())
        {
            audio_.MeleeImpact();
        }
        if (damage.killed)
        {
            if (const NextDayz::FZombieRuntime* zombie = zombies_.Resolve(damage.target))
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
    const auto& slots = zombies_.Slots();
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
    if (const NextDayz::FZombieRuntime* zombie = zombies_.Resolve(validationZombie_))
    {
        return zombie;
    }
    for (const NextDayz::FZombieRuntime& zombie : zombies_.Slots())
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
        for (const NextDayz::FHitCapsule& capsule : combat_.Hitboxes(zombie.handle))
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
            std::optional<NextDayz::FHitboxHit> hit = combat_.TraceHitboxes(origin, direction, 400.0f);
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
            combat_.ProcessHit(event, rifle->damageFalloffDistance);
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
    systems.anchors = &worldAnchors_;
    systems.zombies = &zombies_;
    systems.combat = &combat_;
    systems.effects = &effects_;
    systems.audio = &audio_;
    systems.noise = &noise_;
    systems.spawns = &zombieSpawns_;
    systems.navGrid = &zombieNavGrid_;
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
    commands.syncZombieVisuals = [this]() { SyncZombieVisuals(); };
    commands.setCursorFree = [this](bool free)
    {
        // Leaving the debug cursor gives the mouse back to the game only if
        // nothing else (inventory, map, pause, death) still wants it free.
        SetMouseCaptured(!free && !showInventory_ && !showMap_ && !paused_ && survival_.IsAlive());
        if (free)
        {
            weapons_.SetTriggerDown(false);
            player_.SetAiming(false);
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
                  fmt::format("Teleported to ({:.1f}, {:.1f}, {:.1f})", feet.x, feet.y, feet.z), &feet);
}

NextDayz::FZombieHandle NextDayzGameInstance::SpawnZombieAt(NextDayz::EZombieProfile profile, const glm::vec3& feet)
{
    const NextDayz::FZombieHandle handle = zombies_.Spawn(profile, feet);
    if (NextDayz::FZombieRuntime* zombie = zombies_.Resolve(handle))
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
    if (combat_.ProcessMelee(++validationAttackSequence_, zombie, 1000.0f, NextDayz::EHitZone::Head))
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
        loot_.SpawnPile({{itemId, count - fits, 0}}, player_.Position(), 0.0f, GetEngine());
    }
    debugger_.Log(NextDayz::Debug::EChannel::Player, 0,
                  fits < count ? fmt::format("Debug give {} x{} ({} on the ground)", itemId, count, count - fits)
                               : fmt::format("Debug give {} x{}", itemId, count));
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
        for (const NextDayz::FZombieRuntime& zombie : zombies_.Slots())
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
        if (index >= 0 && index < static_cast<int>(zombies_.Slots().size()) && zombies_.Slots()[index].active)
        {
            debugger_.SelectZombie(zombies_.Slots()[index].handle);
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
    captured = captured && !debugger_.CursorMode();
    mouseCaptured_ = captured;
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
        player_.SetAiming(false);
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
        if (pressed && !player_.IsTraversing())
        {
            weapons_.RequestReload(inventory_);
            if (weapons_.IsReloading()) player_.SetAiming(false);
        }
        return true;
    case SDLK_1:
        if (pressed && !player_.IsTraversing())
        {
            weapons_.SwitchSlot(0);
            if (weapons_.IsSwitching()) player_.SetAiming(false);
        }
        return true;
    case SDLK_2:
        if (pressed && !player_.IsTraversing())
        {
            weapons_.SwitchSlot(1);
            if (weapons_.IsSwitching()) player_.SetAiming(false);
        }
        return true;
    case SDLK_Q:
        if (pressed && !player_.IsTraversing()) weapons_.SwitchPrevious();
        return true;
    case SDLK_3:
    case SDLK_4:
    case SDLK_5:
    case SDLK_6:
        if (pressed && !player_.IsTraversing())
        {
            UseQuickSlot(static_cast<int>(event.key.key - SDLK_3));
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
                player_.SetAiming(false);
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
                    player_.SetAiming(false);
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
    if (!mouseCaptured_ || showInventory_ || showMap_)
    {
        return false;
    }

    if (SDL_GetWindowRelativeMouseMode(GetEngine().GetWindow().Handle()))
    {
        player_.OnLook(static_cast<float>(xpos), static_cast<float>(ypos));
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
        player_.OnLook(static_cast<float>(delta.x), static_cast<float>(delta.y));
    }
    return true;
}

bool NextDayzGameInstance::OnMouseButton(SDL_Event& event)
{
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
            weapons_.SetTriggerDown(true);
            return true;
        }
        if (event.button.button == SDL_BUTTON_RIGHT)
        {
            player_.SetAiming(true);
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
            player_.SetAiming(false);
            return true;
        }
    }
    return false;
}

bool NextDayzGameInstance::OnScroll(double /*xoffset*/, double yoffset)
{
    if (!player_.IsFirstPerson())
    {
        player_.AdjustCameraDistance(static_cast<float>(yoffset));
        return true;
    }
    return false;
}

void NextDayzGameInstance::UpdateGrass(float dt)
{
    grassInteractors_.clear();
    grassInteractors_.reserve(zombies_.Capacity() + 1);
    const auto& locomotion = player_.LocomotionState();
    if (grassPlayerInteraction_ && survival_.IsAlive() && locomotion.onGround && !player_.IsTraversing())
        grassInteractors_.push_back({0, player_.Position(), 0.55f, 1.0f});
    if (grassZombieInteraction_)
    {
        for (const auto& zombie : zombies_.Slots())
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
    reg.Add("sceneReady", [this]() -> Runtime::Agent::FAgentQueryValue { return sceneReady_; });
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
    reg.Add("isAiming", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.IsAiming(); });
    reg.Add("cameraFov", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.CurrentFov()); });
    reg.Add("firstPerson", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.IsFirstPerson(); });
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
        return static_cast<int64_t>(worldAnchors_.Count(NextDayz::EWorldAnchorType::Poi));
    });
    reg.Add("zombieCap", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(zombieSpawns_.GlobalCap());
    });
    reg.Add("zombieSpawnPoints", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(zombieSpawns_.Points().size());
    });
    reg.Add("bleeding", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(survival_.Snapshot().bleedingWounds);
    });
    reg.Add("cold", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(survival_.Snapshot().cold); });
    reg.Add("armor", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(inventory_.Armor()); });
    reg.Add("weather", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::WeatherName(time_.Weather()));
    });
    reg.Add("lanternLit", [this]() -> Runtime::Agent::FAgentQueryValue { return lanternLit_; });
    reg.Add("cacheMarked", [this]() -> Runtime::Agent::FAgentQueryValue { return cacheMarker_.has_value(); });
    reg.Add("showMap", [this]() -> Runtime::Agent::FAgentQueryValue { return showMap_; });
    reg.Add("lastNotification", [this]() -> Runtime::Agent::FAgentQueryValue {
        return notifications_.empty() ? std::string() : notifications_.back().text;
    });
    reg.Add("itemCount", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(inventory_.Instances().size());
    });
    reg.Add("worldZombieAnchors", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(worldAnchors_.Count(NextDayz::EWorldAnchorType::Zombie));
    });
    reg.Add("worldLootAnchors", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(worldAnchors_.Count(NextDayz::EWorldAnchorType::Loot));
    });
    reg.Add("activeZombies", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(zombies_.ActiveCount());
    });
    reg.Add("alertedZombies", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(zombies_.AlertedCount());
    });
    reg.Add("zombieKills", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(zombies_.KillCount());
    });
    reg.Add("lastHitZombie", [this]() -> Runtime::Agent::FAgentQueryValue {
        const NextDayz::FZombieHandle handle = combat_.LastHitZombie();
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
        return debugger_.Warnings().empty() ? std::string() : debugger_.Warnings().front().text;
    });
    reg.Add("debugSelected", [this]() -> Runtime::Agent::FAgentQueryValue { return debugger_.SelectionLabel(); });
    reg.Add("debugSnapshot", [this]() -> Runtime::Agent::FAgentQueryValue { return debugger_.LastSnapshotPath(); });
    reg.Add("debugTickMs", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(debugger_.Timers().Stat(NextDayz::Debug::ETimer::Tick).averageMs);
    });
    reg.Add("spawnVerdict", [this]() -> Runtime::Agent::FAgentQueryValue {
        // The nearest spawn point's last verdict: why the closest anchor did or did not spawn.
        const NextDayz::FZombieSpawnPoint* nearest = nullptr;
        for (const NextDayz::FZombieSpawnPoint& point : zombieSpawns_.Points())
        {
            if (!nearest || point.playerDistance < nearest->playerDistance) nearest = &point;
        }
        return std::string(nearest ? NextDayz::SpawnVerdictName(nearest->verdict) : "none");
    });
    reg.Add("pathSolves", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(zombies_.PathSolvesLastUpdate());
    });
    // Infected with live skeleton hitboxes / their capsules (names kept from the proxy era).
    reg.Add("hitProxyRegistered", [this]() -> Runtime::Agent::FAgentQueryValue {
        int64_t bodies = 0;
        for (const NextDayz::FZombieRuntime& zombie : zombies_.Slots())
            bodies += zombie.active && !combat_.Hitboxes(zombie.handle).empty() ? 1 : 0;
        return bodies;
    });
    reg.Add("hitProxyCpuEligible", [this]() -> Runtime::Agent::FAgentQueryValue {
        int64_t capsules = 0;
        for (const NextDayz::FZombieRuntime& zombie : zombies_.Slots())
            capsules += zombie.active ? static_cast<int64_t>(combat_.Hitboxes(zombie.handle).size()) : 0;
        return capsules;
    });
    reg.Add("lastHitZone", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(combat_.LastHitZombie().IsValid() ? HitZoneName(combat_.LastHitZone()) : "none");
    });
    reg.Add("headshots", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(combat_.HeadshotCount());
    });
    reg.Add("zombieStaggers", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(zombies_.StaggerCount());
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
    reg.Add("fxParticles", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(effects_.ActiveParticles());
    });
    reg.Add("fxTracers", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(effects_.ActiveTracers());
    });
    reg.Add("fxMuzzleFlash", [this]() -> Runtime::Agent::FAgentQueryValue { return effects_.FlashVisible(); });
    reg.Add("zombiePathSegments", [this]() -> Runtime::Agent::FAgentQueryValue {
        int64_t segments = 0;
        for (const NextDayz::FZombieRuntime& zombie : zombies_.Slots())
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
                           combat_.Hitboxes(zombie->handle).size());
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
