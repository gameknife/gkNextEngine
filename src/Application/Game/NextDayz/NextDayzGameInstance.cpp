#include "NextDayzGameInstance.hpp"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL_events.h>
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

    void AppendWeaponBox(std::vector<Assets::Vertex>& vertices, std::vector<uint32_t>& indices,
                         const glm::vec3& min, const glm::vec3& max)
    {
        Assets::Model box = Assets::FProcModel::CreateBox(min, max);
        const uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());
        vertices.insert(vertices.end(), box.CPUVertices().begin(), box.CPUVertices().end());
        for (uint32_t index : box.CPUIndices())
        {
            indices.push_back(vertexOffset + index);
        }
    }

    Assets::Model CreateWeaponVisual(std::string_view weaponId)
    {
        std::vector<Assets::Vertex> vertices;
        std::vector<uint32_t> indices;
        const auto box = [&](const glm::vec3& min, const glm::vec3& max) {
            AppendWeaponBox(vertices, indices, min, max);
        };

        if (weaponId == "pistol")
        {
            box({-0.040f, -0.015f, -0.08f}, {0.040f, 0.055f, 0.22f}); // slide
            box({-0.034f, -0.22f, -0.04f}, {0.034f, -0.015f, 0.07f}); // grip
            box({-0.018f, 0.005f, 0.22f}, {0.018f, 0.035f, 0.29f});   // barrel
            box({-0.035f, 0.055f, -0.06f}, {-0.016f, 0.095f, -0.035f});
            box({0.016f, 0.055f, -0.06f}, {0.035f, 0.095f, -0.035f});
            box({-0.007f, 0.055f, 0.255f}, {0.007f, 0.095f, 0.280f});
        }
        else if (weaponId == "shotgun")
        {
            box({-0.055f, -0.045f, -0.42f}, {0.055f, 0.045f, -0.10f});
            box({-0.045f, -0.050f, -0.10f}, {0.045f, 0.055f, 0.20f});
            box({-0.025f, 0.005f, 0.20f}, {0.025f, 0.050f, 0.78f});
            box({-0.040f, -0.065f, 0.22f}, {0.040f, -0.005f, 0.48f});
            box({-0.040f, 0.050f, -0.02f}, {-0.018f, 0.10f, 0.01f});
            box({0.018f, 0.050f, -0.02f}, {0.040f, 0.10f, 0.01f});
            box({-0.007f, 0.050f, 0.72f}, {0.007f, 0.10f, 0.75f});
        }
        else if (weaponId == "mosin")
        {
            box({-0.045f, -0.040f, -0.48f}, {0.045f, 0.045f, 0.12f});
            box({-0.020f, 0.005f, 0.12f}, {0.020f, 0.040f, 0.86f});
            box({-0.034f, -0.13f, -0.08f}, {0.034f, -0.04f, 0.03f});
            box({-0.038f, 0.045f, 0.00f}, {-0.017f, 0.10f, 0.03f});
            box({0.017f, 0.045f, 0.00f}, {0.038f, 0.10f, 0.03f});
            box({-0.007f, 0.040f, 0.80f}, {0.007f, 0.10f, 0.83f});
        }
        else if (weaponId == "svd")
        {
            box({-0.050f, -0.045f, -0.43f}, {0.050f, 0.050f, 0.20f});
            box({-0.021f, 0.000f, 0.20f}, {0.021f, 0.040f, 0.86f});
            // Four rails leave a visible sight channel through the scope.
            box({-0.035f, 0.055f, -0.02f}, {-0.022f, 0.115f, 0.25f});
            box({0.022f, 0.055f, -0.02f}, {0.035f, 0.115f, 0.25f});
            box({-0.022f, 0.055f, -0.02f}, {0.022f, 0.068f, 0.25f});
            box({-0.022f, 0.102f, -0.02f}, {0.022f, 0.115f, 0.25f});
            box({-0.035f, -0.18f, -0.02f}, {0.035f, -0.045f, 0.10f});
        }
        else // AK-74
        {
            box({-0.055f, -0.045f, -0.38f}, {0.055f, 0.045f, -0.12f});
            box({-0.050f, -0.050f, -0.12f}, {0.050f, 0.060f, 0.20f});
            box({-0.022f, 0.000f, 0.20f}, {0.022f, 0.045f, 0.66f});
            box({-0.038f, -0.20f, -0.02f}, {0.038f, -0.05f, 0.12f});
            box({-0.045f, 0.045f, -0.01f}, {-0.018f, 0.105f, 0.025f});
            box({0.018f, 0.045f, -0.01f}, {0.045f, 0.105f, 0.025f});
            box({-0.008f, 0.045f, 0.59f}, {0.008f, 0.105f, 0.625f});
        }

        return Assets::Model::CreateFromGeometry(fmt::format("nd_weapon_{}", weaponId),
                                                  std::move(vertices), std::move(indices));
    }

    glm::vec3 WeaponVisualColor(std::string_view weaponId)
    {
        if (weaponId == "ak") return {0.20f, 0.13f, 0.07f};
        if (weaponId == "svd") return {0.24f, 0.15f, 0.08f};
        if (weaponId == "mosin") return {0.31f, 0.20f, 0.10f};
        if (weaponId == "shotgun") return {0.14f, 0.12f, 0.10f};
        return {0.10f, 0.11f, 0.12f};
    }

    const char* ZombieStateName(NextDayz::EZombieState state)
    {
        using enum NextDayz::EZombieState;
        switch (state)
        {
        case Dormant: return "Dormant";
        case Wander: return "Wander";
        case Investigate: return "Investigate";
        case Alert: return "Alert";
        case Chase: return "Chase";
        case Attack: return "Attack";
        case Stagger: return "Stagger";
        case Dead: return "Dead";
        }
        return "Unknown";
    }

    ImU32 ZombieStateColor(NextDayz::EZombieState state)
    {
        using enum NextDayz::EZombieState;
        switch (state)
        {
        case Wander: return IM_COL32(90, 235, 120, 245);
        case Investigate: return IM_COL32(255, 220, 70, 245);
        case Alert: return IM_COL32(255, 180, 60, 250);
        case Chase: return IM_COL32(255, 145, 45, 250);
        case Attack: return IM_COL32(255, 55, 45, 255);
        case Stagger: return IM_COL32(230, 80, 255, 250);
        case Dead: return IM_COL32(115, 115, 115, 210);
        default: return IM_COL32(160, 185, 195, 230);
        }
    }

    const char* HitZoneName(NextDayz::EHitZone zone)
    {
        switch (zone)
        {
        case NextDayz::EHitZone::Head: return "head";
        case NextDayz::EHitZone::Torso: return "torso";
        case NextDayz::EHitZone::Limb: return "limb";
        case NextDayz::EHitZone::Leg: return "leg";
        }
        return "unknown";
    }

    ImU32 HitZoneColor(NextDayz::EHitZone zone)
    {
        switch (zone)
        {
        case NextDayz::EHitZone::Head: return IM_COL32(255, 90, 60, 255);
        case NextDayz::EHitZone::Torso: return IM_COL32(80, 255, 135, 255);
        case NextDayz::EHitZone::Limb: return IM_COL32(90, 200, 255, 255);
        case NextDayz::EHitZone::Leg: return IM_COL32(255, 210, 70, 255);
        }
        return IM_COL32(255, 255, 255, 255);
    }

    // Faded shirt colours for the infected; each body slot wears one.
    constexpr std::array<glm::vec3, 8> kInfectedShirts = {{
        {0.42f, 0.12f, 0.10f}, {0.20f, 0.28f, 0.40f}, {0.55f, 0.50f, 0.38f}, {0.30f, 0.36f, 0.24f},
        {0.62f, 0.60f, 0.56f}, {0.17f, 0.17f, 0.19f}, {0.45f, 0.30f, 0.18f}, {0.25f, 0.40f, 0.38f},
    }};

    constexpr float kHitMarkerSeconds = 0.22f;
    constexpr float kKillMarkerSeconds = 0.42f;
    constexpr float kHurtSeconds = 0.9f;

    const char* LootCategoryName(NextDayz::ELootCategory category)
    {
        using enum NextDayz::ELootCategory;
        switch (category)
        {
        case FoodWater: return "Food/Water";
        case Medical: return "Medical";
        case Ammo: return "Ammo";
        case Weapon: return "Weapon";
        case Clothing: return "Clothing";
        case Misc: return "Misc";
        }
        return "Unknown";
    }

    ImU32 LootCategoryColor(NextDayz::ELootCategory category)
    {
        using enum NextDayz::ELootCategory;
        switch (category)
        {
        case FoodWater: return IM_COL32(80, 220, 255, 240);
        case Medical: return IM_COL32(255, 85, 110, 240);
        case Ammo: return IM_COL32(255, 190, 55, 240);
        case Weapon: return IM_COL32(255, 90, 40, 245);
        case Clothing: return IM_COL32(170, 105, 255, 240);
        case Misc: return IM_COL32(205, 215, 220, 220);
        }
        return IM_COL32_WHITE;
    }
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
    // SoftwareModern rasteriser + software GI: lightest path for a 1km POI map.
    cvars.SetDefaultFromString("r.rendererType", "0", &error);
    cvars.SetDefaultFromString("r.upscaler.type", "2", &error);

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
                           const NextDayz::FZombieHandle handle = zombies_.Spawn(
                               NextDayz::EZombieProfile::Civilian,
                               player_.Position() + ahead * validationZombieDistance_);
                           validationZombie_ = handle;
                           // Face the player so the fixture starts from a known pose.
                           if (NextDayz::FZombieRuntime* zombie = zombies_.Resolve(handle))
                           {
                               zombie->forward = -ahead;
                           }
                           SyncZombieVisuals();
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
    cvars.RegisterBool("nextdayz.debug.orbit", false, &debugOrbit_, NextCVar::ECVarFlags::None,
                       "Orbit camera around the player rig instead of the gameplay camera");
    cvars.RegisterFloat("nextdayz.debug.orbitYaw", debugOrbitYaw_, &debugOrbitYaw_, NextCVar::ECVarFlags::None,
                        "Orbit camera angle in degrees from behind the body (+ toward its right)", nullptr,
                        -360.0, 360.0);
    cvars.RegisterFloat("nextdayz.debug.orbitDistance", debugOrbitDistance_, &debugOrbitDistance_,
                        NextCVar::ECVarFlags::None, "Orbit camera distance (m)", nullptr, 0.5, 20.0);
    cvars.RegisterFloat("nextdayz.debug.orbitHeight", debugOrbitHeight_, &debugOrbitHeight_,
                        NextCVar::ECVarFlags::None, "Orbit camera look-at height above the feet (m)", nullptr, 0.0,
                        3.0);
    cvars.RegisterBool("nextdayz.debug.orbitZombie", false, &debugOrbitZombie_, NextCVar::ECVarFlags::None,
                       "Orbit camera centres on the focus infected: the last validation spawn, else the first active");
    cvars.RegisterFloat("nextdayz.debug.zombieTimeScale", 1.0f, &debugZombieTimeScale_, NextCVar::ECVarFlags::None,
                        "Slow-motion for the infected simulation and animation (0 freezes them)", nullptr, 0.0,
                        2.0);
    cvars.RegisterFloat("nextdayz.debug.fxTimeScale", 1.0f, &debugFxTimeScale_, NextCVar::ECVarFlags::None,
                        "Slow-motion for combat effects (0 freezes flashes, tracers and blood in place)", nullptr,
                        0.0, 2.0);
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
    cvars.RegisterBool("nextdayz.validation.restart", false, &validationRestart_, NextCVar::ECVarFlags::None,
                       "Agent-only in-process restart", [this, validationReady]()
                       {
                           if (validationRestart_ && validationReady()) ResetSession();
                       });
}

void NextDayzGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& /*nodes*/,
                                              std::vector<Assets::Model>& models,
                                              std::vector<Assets::FMaterial>& materials,
                                              std::vector<Assets::LightObject>& /*lights*/,
                                              std::vector<Assets::AnimationTrack>& /*tracks*/)
{
    rig_.InjectAssets(models, materials);
    effects_.InjectAssets(models, materials);

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
                    sectionMaterials[section] = Assets::SceneBuilder::AddLambertianMaterial(
                        materials, glm::vec3(part.sectionColors[section]));
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

    for (size_t i = 0; i < NextDayz::kWeapons.size(); ++i)
    {
        const NextDayz::FWeaponDef& def = NextDayz::kWeapons[i];
        models.push_back(CreateWeaponVisual(def.id));
        weaponModelIds_[i] = static_cast<uint32_t>(models.size() - 1);
        weaponMaterialIds_[i] =
            Assets::SceneBuilder::AddLambertianMaterial(materials, WeaponVisualColor(def.id));
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
    worldAnchors_.Scan(GetEngine());
    BuildZombieNavigation();
    zombies_.Reset();
    combat_.Reset();
    noise_.Reset();
    zombieVisuals_.Reset();
    zombieSpawns_.Reset(sessionRng_.Derive(0x5A4F4D42ULL).Seed());
    ConfigureZombieSpawnPoints();
    // Loot scan first so the spawn can be derived from real supply ground height.
    loot_.OnSceneLoaded(GetEngine().GetScene());

    const glm::vec3 spawn = ResolveSpawnPosition();
    player_.Create(GetEngine().GetPhysicsEngine(), spawn, config_);

    rig_.OnSceneLoaded(GetEngine().GetScene());
    weapons_.OnSceneLoaded(GetEngine());
    effects_.OnSceneLoaded(GetEngine().GetScene());
    CreateZombieVisuals();
    time_.Reset(config_.Time);
    time_.Tick(0.0, GetEngine().GetScene());

    // Product start: pockets, one emergency bandage, and an empty bottle. Weapon
    // regression scripts opt into nextdayz.validation.loadout explicitly.
    inventory_.Clear();
    actions_.Reset();
    survival_.Reset();
    recentWeaponTraces_.clear();
    inventory_.TryAdd("bandage", "Bandage", NextDayz::EItemKind::Consumable, 1);
    inventory_.TryAdd("water_bottle_empty", "Empty Bottle", NextDayz::EItemKind::Misc, 1);
    if (validationLoadout_ && GOption->AgentValidation)
    {
        ApplyValidationLoadout();
    }

    GetEngine().GetScene().GetEnvSettings().SkyIdx = 2;

    showInventory_ = false;
    paused_ = false;
    survivalSeconds_ = 0.0;
    SetMouseCaptured(true);
    sceneReady_ = true;
    SPDLOG_INFO("[NextDayz] scene loaded ({} nodes)", GetEngine().GetScene().Nodes().size());
}

void NextDayzGameInstance::OnSceneUnloaded()
{
    sceneReady_ = false;
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
    recentWeaponTraces_.clear();
}

void NextDayzGameInstance::OnDestroy()
{
    player_.Destroy();
}

void NextDayzGameInstance::OnTick(double deltaSeconds)
{
    if (!sceneReady_ || !player_.IsValid())
    {
        return;
    }
    const float dt = static_cast<float>(deltaSeconds);
    if (paused_)
    {
        weapons_.SetTriggerDown(false);
        return;
    }
    if (survival_.IsAlive())
    {
        survivalSeconds_ += deltaSeconds;
    }
    audio_.SetListener(player_.EyePosition());
    hitMarkerSeconds_ = std::max(0.0f, hitMarkerSeconds_ - dt);
    hurtSeconds_ = std::max(0.0f, hurtSeconds_ - dt);
    for (FRecentWeaponTrace& trace : recentWeaponTraces_)
    {
        trace.remainingSeconds -= dt;
    }
    std::erase_if(recentWeaponTraces_, [](const FRecentWeaponTrace& trace)
    {
        return trace.remainingSeconds <= 0.0f;
    });

    actions_.Update(dt);
    if (const std::optional<NextDayz::FLootHandle> cancel = actions_.ConsumeCancelRequest())
    {
        loot_.Cancel(*cancel);
    }
    if (const std::optional<NextDayz::FLootHandle> commit = actions_.ConsumeCommitRequest())
    {
        loot_.Commit(*commit, inventory_, GetEngine());
    }
    if (const std::optional<NextDayz::FItemActionCommit> commit = actions_.ConsumeItemCommitRequest())
    {
        if (commit->action == NextDayz::EPlayerAction::DrinkFromWell)
        {
            survival_.DrinkFromWell();
        }
        else if (commit->action == NextDayz::EPlayerAction::FillBottle)
        {
            survival_.FillBottle(inventory_, commit->instanceId);
        }
        else
        {
            survival_.TryUseItem(inventory_, commit->instanceId);
        }
    }

    const bool actionLocked = actions_.IsActive();
    const bool traversalLocked = player_.IsTraversing();
    const bool dead = !survival_.IsAlive();
    player_.SetMovementLocked(actionLocked || traversalLocked || showInventory_ || dead);
    weapons_.SetPresentationSuppressed(actionLocked || traversalLocked || dead || debugOrbit_);
    if (actionLocked || traversalLocked || dead)
    {
        player_.SetAiming(false);
        weapons_.SetTriggerDown(false);
    }
    player_.SetSprintBlocked(weapons_.TriggerDown() && weapons_.HasActiveWeapon());
    player_.Update(dt);
    survival_.Update(dt, player_.LocomotionState().gait == NextDayz::EPlayerGait::Sprint);
    weapons_.Update(dt, player_, inventory_, GetEngine());
    if (weapons_.ConsumeDryFire())
    {
        audio_.DryFire();
    }
    if (const NextDayz::EWeaponPresentationAction action = weapons_.PresentationAction(); action != lastWeaponAction_)
    {
        if (action == NextDayz::EWeaponPresentationAction::Reload) audio_.Reload();
        if (action == NextDayz::EWeaponPresentationAction::Switch) audio_.Switch();
        lastWeaponAction_ = action;
    }
    if (weapons_.PresentationAction() != NextDayz::EWeaponPresentationAction::None)
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
    const bool firstPersonView = player_.IsFirstPerson() && !debugOrbit_;
    const bool hasVisualMuzzle = firstPersonView ? weapons_.ViewModelMuzzle(visualMuzzle, visualMuzzleForward)
                                                 : rig_.WeaponMuzzle(visualMuzzle, visualMuzzleForward);
    for (NextDayz::FWeaponTraceEvent& trace : weapons_.ConsumeTraceEvents())
    {
        // Every round of a burst gets a streak; a shotgun shows a few of its pellets.
        if (trace.pellet % 3 == 0)
        {
            effects_.Tracer(hasVisualMuzzle ? visualMuzzle : trace.origin, trace.endPoint);
        }
        if (trace.hit && !trace.hitZombie)
        {
            effects_.WorldImpact(trace.endPoint, trace.normal);
            audio_.Impact(trace.endPoint);
        }
        recentWeaponTraces_.push_back({std::move(trace), 4.0f});
    }
    if (recentWeaponTraces_.size() > 64)
    {
        recentWeaponTraces_.erase(recentWeaponTraces_.begin(),
                                  recentWeaponTraces_.begin() +
                                      static_cast<std::ptrdiff_t>(recentWeaponTraces_.size() - 64));
    }
    for (const NextDayz::FShotEvent& shot : weapons_.ConsumeShotEvents())
    {
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
        return zombieNavGrid_.FindPath(from, to, from.y);
    };
    const float zombieDt = dt * debugZombieTimeScale_;
    zombies_.Update(zombieDt, player_.Position(), player_.Forward(), survival_.IsAlive(),
                    lineOfSight, noise_.Events(), resolvePath);
    for (const NextDayz::FPlayerDamageRequest& damage : zombies_.ConsumePlayerDamageRequests())
    {
        const bool invulnerable = validationInvulnerable_ && GOption->AgentValidation;
        if (invulnerable || survival_.ApplyDamage(damage.amount, "infected"))
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
    SyncZombieVisuals(zombieDt);
    UpdateZombieAudio(zombieDt);
    effects_.Update(dt * debugFxTimeScale_);

    loot_.Update(dt, player_.EyePosition(), player_.Forward(), GetEngine());
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
    time_.Tick(deltaSeconds, GetEngine().GetScene());

    rig_.SetVisible(!player_.IsFirstPerson() || debugOrbit_);
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
}

bool NextDayzGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    if (!player_.IsValid())
    {
        return false;
    }
    float fov = config_.Camera.BaseFov;
    player_.FillCamera(outRenderCamera.ModelView, fov);
    if (debugOrbit_)
    {
        float yaw = rig_.BodyYaw();
        glm::vec3 focus = player_.Position();
        if (debugOrbitZombie_)
        {
            if (const NextDayz::FZombieRuntime* zombie = FocusZombie())
            {
                focus = zombie->position;
                yaw = std::atan2(zombie->forward.x, zombie->forward.z);
            }
        }
        const glm::vec3 behind(-std::sin(yaw), 0.0f, -std::cos(yaw));
        const glm::vec3 toEye =
            glm::angleAxis(glm::radians(-debugOrbitYaw_), glm::vec3(0.0f, 1.0f, 0.0f)) * behind;
        const glm::vec3 center = focus + glm::vec3(0.0f, debugOrbitHeight_, 0.0f);
        const glm::vec3 eye = center + glm::normalize(toEye + glm::vec3(0.0f, 0.12f, 0.0f)) * debugOrbitDistance_;
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
    ctx.weaponName = weapons_.ActiveDisplayName();
    ctx.ammoInMag = weapons_.AmmoInMag();
    ctx.ammoReserve = weapons_.AmmoReserve(inventory_);
    ctx.reloading = weapons_.IsReloading();
    ctx.activeSlot = weapons_.ActiveSlot();
    if (actions_.IsActive())
    {
        ctx.interactionPrompt = fmt::format("{}...", NextDayz::ActionName(actions_.Action()));
    }
    else if (loot_.HasHovered())
    {
        ctx.interactionPrompt = fmt::format("[E] Pick up {}", loot_.HoveredPrompt());
    }
    else if (hoveredWell_.IsValid())
    {
        ctx.interactionPrompt = "[E] Drink from well   [F] Fill bottle";
    }
    ctx.hour = time_.HourInt();
    ctx.minute = time_.MinuteInt();
    ctx.overcast = time_.Overcast();
    ctx.showInventory = showInventory_;
    ctx.showDebugPanel = showDebugPanel_;
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
    const NextDayz::FPlayerLocomotionState& locomotion = player_.LocomotionState();
    ctx.debug.position = player_.Position();
    ctx.debug.eyePosition = player_.EyePosition();
    ctx.debug.velocity = locomotion.worldVelocity;
    ctx.debug.localMove = locomotion.localMove;
    ctx.debug.cameraRecoilRadians = player_.CameraRecoil();
    ctx.debug.yawRadians = player_.Yaw();
    ctx.debug.pitchRadians = player_.Pitch();
    ctx.debug.fovDegrees = player_.CurrentFov();
    ctx.debug.horizontalSpeed = locomotion.horizontalSpeed;
    ctx.debug.controllerHeight = player_.ControllerHeight();
    ctx.debug.aimWeight = rig_.AimWeight();
    ctx.debug.actionTime = actions_.NormalizedTime();
    ctx.debug.stance = NextDayz::StanceName(locomotion.actualStance);
    ctx.debug.desiredStance = NextDayz::StanceName(locomotion.desiredStance);
    ctx.debug.gait = NextDayz::GaitName(locomotion.gait);
    ctx.debug.jumpPhase = NextDayz::JumpPhaseName(locomotion.jumpPhase);
    ctx.debug.jumpPhaseTime = locomotion.jumpPhaseTime01;
    ctx.debug.traversalAction = NextDayz::TraversalActionName(locomotion.traversalAction);
    ctx.debug.traversalProbeResult = player_.TraversalProbeResult();
    ctx.debug.traversalTime = locomotion.traversalTime01;
    ctx.debug.traversalHeight = locomotion.traversalHeight;
    ctx.debug.baseAnimation = rig_.CurrentBaseClipName();
    ctx.debug.action = NextDayz::ActionName(actions_.Action());
    ctx.debug.onGround = locomotion.onGround;
    ctx.debug.standBlocked = locomotion.standBlocked;
    ctx.debug.sprinting = player_.IsSprinting();
    ctx.debug.actionCommitted = actions_.IsCommitted();
    ctx.debug.cameraRecoilActive = player_.RecoilActive();
    ctx.debug.rigRecoilActive = rig_.RecoilActive();
    ctx.debug.viewModelRecoilActive = weapons_.ViewModelRecoilActive();
    ctx.debug.shotSequence = weapons_.LastShotSequence();
    ctx.debug.weaponAction = NextDayz::WeaponActionName(weapons_.PresentationAction());
    ctx.debug.weaponActionTime = weapons_.PresentationActionTime();
    ctx.debug.weaponActionClip = rig_.CurrentWeaponActionClipName();
    ctx.debug.weaponActionWeight = rig_.WeaponActionWeight();
    ctx.debug.switchingWeapon = weapons_.IsSwitching();
    ctx.debug.switchTargetSlot = weapons_.SwitchTargetSlot();
    ctx.debug.switchCommitted = weapons_.SwitchCommitted();
    ctx.debug.activeZombies = zombies_.ActiveCount();
    ctx.debug.alertedZombies = zombies_.AlertedCount();
    ctx.debug.zombieKills = zombies_.KillCount();
    ctx.debug.headshots = combat_.HeadshotCount();
    ctx.debug.zombieStaggers = zombies_.StaggerCount();
    ctx.debug.zombieOverlay = debugZombieOverlay_;
    ctx.debug.lootOverlay = debugLootOverlay_;
    ctx.debug.hitProxyOverlay = debugHitProxyOverlay_;
    for (const NextDayz::FZombieRuntime& zombie : zombies_.Slots())
    {
        if (zombie.active && zombie.pathWaypoint < zombie.path.size())
        {
            ctx.debug.zombiePathSegments += static_cast<int>(zombie.path.size() - zombie.pathWaypoint);
        }
    }
    for (const NextDayz::FZombieRuntime& zombie : zombies_.Slots())
    {
        const size_t capsules = zombie.active ? combat_.Hitboxes(zombie.handle).size() : 0;
        ctx.debug.hitBodies += capsules > 0 ? 1 : 0;
        ctx.debug.hitCapsules += static_cast<int>(capsules);
    }
    for (const NextDayz::FLootEntry& entry : loot_.Entries())
    {
        const NextDayz::FLootSlot* slot = loot_.ResolveSlot(entry.directorHandle);
        if (!slot) continue;
        const size_t category = static_cast<size_t>(slot->category);
        if (category < ctx.debug.lootTotalByCategory.size())
        {
            ++ctx.debug.lootTotalByCategory[category];
        }
        if (entry.state == NextDayz::FLootEntry::EState::Available)
        {
            ++ctx.debug.lootAvailable;
            if (category < ctx.debug.lootAvailableByCategory.size())
            {
                ++ctx.debug.lootAvailableByCategory[category];
            }
        }
        else if (entry.state == NextDayz::FLootEntry::EState::Reserved)
        {
            ++ctx.debug.lootReserved;
        }
        else
        {
            ++ctx.debug.lootCooldown;
        }
        if (!entry.def) continue;
        for (const NextDayz::FLootGrant& grant : entry.def->grants)
        {
            if (grant.id == "food_can") ctx.debug.criticalFood += grant.count;
            if (grant.id == "bandage" || grant.id == "medkit") ctx.debug.criticalMedical += grant.count;
            if (grant.id == "backpack") ctx.debug.criticalBackpack += grant.count;
            if (grant.kind == NextDayz::EItemKind::Weapon || grant.kind == NextDayz::EItemKind::Melee)
                ctx.debug.criticalWeapons += grant.count;
            if (grant.kind == NextDayz::EItemKind::Ammo) ctx.debug.criticalAmmo += grant.count;
        }
    }
    ctx.debug.criticalWaterSources = static_cast<int>(
        worldAnchors_.Count(NextDayz::EWorldAnchorType::Well));
    ctx.debug.recentWeaponTraces = static_cast<int>(recentWeaponTraces_.size());
    if (!recentWeaponTraces_.empty())
    {
        const NextDayz::FWeaponTraceEvent& trace = recentWeaponTraces_.back().trace;
        ctx.debug.lastTraceInstanceId = trace.hitInstanceId;
        if (!trace.hit)
        {
            ctx.debug.lastTraceResult = "MISS";
        }
        else if (trace.hitZombie)
        {
            ctx.debug.lastTraceResult = fmt::format("INFECTED {}", HitZoneName(combat_.LastHitZone()));
        }
        else
        {
            ctx.debug.lastTraceResult = "WORLD/BLOCKER";
        }
    }
    ctx.equipWeapon = [this](const std::string& weaponId, int slot) {
        const_cast<NextDayzGameInstance*>(this)->EquipFromInventory(weaponId, slot);
    };
    ctx.toggleClothing = [this](const std::string& clothingId, bool on) {
        const_cast<NextDayzGameInstance*>(this)->ToggleClothing(clothingId, on);
    };
    ctx.useItem = [this](NextDayz::FItemInstanceId instanceId) {
        const_cast<NextDayzGameInstance*>(this)->UseInventoryItem(instanceId);
    };
    ctx.restartSession = [this]() {
        const_cast<NextDayzGameInstance*>(this)->ResetSession();
    };
    if (showDebugPanel_)
    {
        DrawDebugWorldOverlay();
    }
    NextDayz::NextDayzHUD::Draw(ctx);
    return true;
}

void NextDayzGameInstance::EquipFromInventory(const std::string& weaponId, int slot)
{
    weapons_.Equip(slot, weaponId);
}

void NextDayzGameInstance::ToggleClothing(const std::string& clothingId, bool on)
{
    inventory_.SetClothingEquipped(clothingId, on);
    rig_.SetClothing(clothingId, on);
}

void NextDayzGameInstance::UseInventoryItem(NextDayz::FItemInstanceId instanceId)
{
    if (!survival_.IsAlive() || actions_.IsActive())
    {
        return;
    }
    const NextDayz::FItemInstance* instance = inventory_.FindInstance(instanceId);
    const NextDayz::FItemDef* definition = instance ? NextDayz::FindItemDef(instance->defId) : nullptr;
    if (!definition || definition->kind != NextDayz::EItemKind::Consumable)
    {
        return;
    }
    const NextDayz::EPlayerAction action = definition->hydrationDelta > 0.0f
        ? NextDayz::EPlayerAction::Drink
        : definition->hungerDelta > 0.0f ? NextDayz::EPlayerAction::Eat : NextDayz::EPlayerAction::Heal;
    actions_.BeginUse(action, instanceId);
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
    if (inventory_.CountOf("ak") == 0)
    {
        inventory_.TryAdd("ak", "AK-74", NextDayz::EItemKind::Weapon, 1);
    }
    if (inventory_.CountOf("ammo_545") < 60)
    {
        inventory_.TryAdd("ammo_545", "5.45x39", NextDayz::EItemKind::Ammo,
                          60 - inventory_.CountOf("ammo_545"));
    }
    weapons_.Equip(0, "ak");
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
    zombieNavGrid_.Build(scene.GetCPUAccelerationStructure(), settings);

    Runtime::TerrainComponent* terrain = nullptr;
    for (Runtime::TerrainComponent* candidate : scene.Components<Runtime::TerrainComponent>())
    {
        terrain = candidate;
        break;
    }
    if (terrain)
    {
        zombieNavGrid_.MaskUnwalkable([terrain](const glm::vec3& point)
        {
            return terrain->IsWater(point.x, point.z) &&
                   point.y < terrain->WaterSurface(point.x, point.z) + 0.05f;
        });
    }
    SPDLOG_INFO("[NextDayz] infected navigation ready: {}x{} cells at {:.1f} m",
                zombieNavGrid_.GetWidth(), zombieNavGrid_.GetHeight(), zombieNavGrid_.GetCellSize());
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
        zombiePoints.push_back({anchor->worldPos, profile});
    }
    zombieSpawns_.SetPoints(std::move(zombiePoints));
}

void NextDayzGameInstance::ResetSession()
{
    if (!sceneReady_)
    {
        return;
    }
    actions_.Reset();
    inventory_.Clear();
    inventory_.TryAdd("bandage", "Bandage", NextDayz::EItemKind::Consumable, 1);
    inventory_.TryAdd("water_bottle_empty", "Empty Bottle", NextDayz::EItemKind::Misc, 1);
    survival_.Reset();
    weapons_.ResetRuntime();
    zombies_.Reset();
    combat_.Reset();
    noise_.Reset();
    zombieSpawns_.Reset(sessionRng_.Derive(0x5A4F4D42ULL).Seed());
    ConfigureZombieSpawnPoints();
    zombieVisuals_.Reset();
    recentWeaponTraces_.clear();
    effects_.Reset();
    loot_.ResetSession(GetEngine());
    time_.Reset(config_.Time);
    player_.Destroy();
    player_.Create(GetEngine().GetPhysicsEngine(), ResolveSpawnPosition(), config_);
    if (validationLoadout_ && GOption->AgentValidation)
    {
        ApplyValidationLoadout();
    }
    showInventory_ = false;
    paused_ = false;
    survivalSeconds_ = 0.0;
    hoveredWell_ = {};
    SyncZombieVisuals();
    SetMouseCaptured(true);
}

std::string NextDayzGameInstance::CurrentObjective() const
{
    if (inventory_.CountOf("water_bottle") == 0)
    {
        return "Find drinking water or a well";
    }
    if (inventory_.CountOf("food_can") == 0)
    {
        return "Find food";
    }
    if (inventory_.TotalCapacity() <= 10)
    {
        return "Find a backpack or larger clothing";
    }
    if (!weapons_.HasActiveWeapon())
    {
        return "Find a melee weapon or firearm";
    }
    return "Travel to a high-risk POI for advanced supplies";
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
        if (damage.target.index < zombieRigVisuals_.size() && zombieRigVisuals_[damage.target.index].IsBound())
        {
            zombieRigVisuals_[damage.target.index].AddHit(damage, config_.Zombie);
        }
        effects_.BloodHit(damage.worldPoint, damage.direction, -damage.direction, damage.hitZone, damage.killed);
        audio_.Hit(damage.hitZone, damage.killed);
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

void NextDayzGameInstance::DrawDebugWorldOverlay() const
{
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (!draw) return;

    const auto project = [this](const glm::vec3& world, ImVec2& screen)
    {
        return Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, world, screen);
    };
    const auto line = [draw](const ImVec2& from, const ImVec2& to, ImU32 color, float thickness = 2.0f)
    {
        draw->AddLine(from, to, IM_COL32(5, 8, 12, 220), thickness + 2.5f);
        draw->AddLine(from, to, color, thickness);
    };
    const auto label = [draw](const ImVec2& position, ImU32 color, const std::string& text)
    {
        const ImVec2 size = ImGui::CalcTextSize(text.c_str());
        const ImVec2 min(position.x - 3.0f, position.y - 2.0f);
        const ImVec2 max(position.x + size.x + 3.0f, position.y + size.y + 2.0f);
        draw->AddRectFilled(min, max, IM_COL32(8, 11, 15, 205), 3.0f);
        draw->AddRect(min, max, color, 3.0f, 0, 1.0f);
        draw->AddText(position, color, text.c_str());
    };

    if (debugZombieOverlay_ || debugHitProxyOverlay_)
    {
        const auto& zombieSlots = zombies_.Slots();
        for (size_t index = 0; index < zombieSlots.size(); ++index)
        {
            const NextDayz::FZombieRuntime& zombie = zombieSlots[index];
            if (!zombie.active || glm::distance(zombie.position, player_.Position()) > 220.0f) continue;
            const ImU32 stateColor = ZombieStateColor(zombie.state);

            if (debugZombieOverlay_)
            {
                ImVec2 feet;
                ImVec2 head;
                if (project(zombie.position, feet) &&
                    project(zombie.position + glm::vec3(0.0f, 2.0f, 0.0f), head))
                {
                    line(feet, head, stateColor, 2.0f);
                    draw->AddCircle(head, 7.0f, stateColor, 16, 2.0f);
                    label({head.x + 10.0f, head.y - 9.0f}, stateColor,
                          fmt::format("Z{}:{} {} {} HP {:.0f} repath {:.2f}s stuck {:.2f}s",
                                      zombie.handle.index, zombie.handle.generation,
                                      NextDayz::ZombieDef(zombie.profile).id,
                                      ZombieStateName(zombie.state), zombie.health,
                                      zombie.repathSeconds, zombie.stuckSeconds));
                }

                ImVec2 forwardFrom;
                ImVec2 forwardTo;
                if (project(zombie.position + glm::vec3(0.0f, 1.0f, 0.0f), forwardFrom) &&
                    project(zombie.position + zombie.forward * 2.0f + glm::vec3(0.0f, 1.0f, 0.0f), forwardTo))
                {
                    line(forwardFrom, forwardTo, stateColor, 2.5f);
                    draw->AddCircleFilled(forwardTo, 3.5f, stateColor);
                }

                ImVec2 previousScreen;
                bool previousValid = project(zombie.position + glm::vec3(0.0f, 0.18f, 0.0f), previousScreen);
                for (size_t waypoint = zombie.pathWaypoint; waypoint < zombie.path.size(); ++waypoint)
                {
                    ImVec2 waypointScreen;
                    const bool waypointValid = project(
                        zombie.path[waypoint] + glm::vec3(0.0f, 0.18f, 0.0f), waypointScreen);
                    if (previousValid && waypointValid)
                    {
                        line(previousScreen, waypointScreen, IM_COL32(65, 225, 255, 245), 2.5f);
                        draw->AddCircleFilled(waypointScreen, 4.0f, IM_COL32(65, 225, 255, 245));
                    }
                    previousScreen = waypointScreen;
                    previousValid = waypointValid;
                }

                if (zombie.state == NextDayz::EZombieState::Investigate ||
                    zombie.state == NextDayz::EZombieState::Chase ||
                    zombie.state == NextDayz::EZombieState::Attack)
                {
                    ImVec2 known;
                    if (project(zombie.lastKnownPlayerPosition + glm::vec3(0.0f, 0.25f, 0.0f), known))
                    {
                        draw->AddCircle(known, 9.0f, IM_COL32(255, 210, 55, 240), 16, 2.0f);
                        draw->AddLine({known.x - 7.0f, known.y - 7.0f}, {known.x + 7.0f, known.y + 7.0f},
                                      IM_COL32(255, 210, 55, 240), 2.0f);
                        draw->AddLine({known.x - 7.0f, known.y + 7.0f}, {known.x + 7.0f, known.y - 7.0f},
                                      IM_COL32(255, 210, 55, 240), 2.0f);
                    }
                }
            }

            if (debugHitProxyOverlay_)
            {
                const std::span<const NextDayz::FHitCapsule> capsules = combat_.Hitboxes(zombie.handle);
                for (const NextDayz::FHitCapsule& capsule : capsules)
                {
                    const ImU32 color = HitZoneColor(capsule.zone);
                    ImVec2 a;
                    ImVec2 b;
                    ImVec2 rim;
                    if (!project(capsule.a, a) || !project(capsule.b, b)) continue;
                    line(a, b, color, 1.5f);
                    if (project(capsule.a + glm::vec3(0.0f, capsule.radius, 0.0f), rim))
                    {
                        const float radius = std::hypot(rim.x - a.x, rim.y - a.y);
                        draw->AddCircle(a, radius, color, 12, 1.2f);
                        draw->AddCircle(b, radius, color, 12, 1.2f);
                    }
                }
                ImVec2 bodyLabel;
                if (project(zombie.position + glm::vec3(0.0f, 2.2f, 0.0f), bodyLabel))
                {
                    const ImU32 color = capsules.empty() ? IM_COL32(255, 40, 170, 255) : IM_COL32(80, 255, 135, 255);
                    label({bodyLabel.x + 10.0f, bodyLabel.y + 10.0f}, color,
                          capsules.empty() ? std::string("NO HITBOXES")
                                           : fmt::format("HITBOX {} capsules{}", capsules.size(),
                                                         zombie.Lame() ? " (lame)" : ""));
                }
            }
        }
    }

    if (debugLootOverlay_)
    {
        for (size_t index = 0; index < loot_.Entries().size(); ++index)
        {
            const NextDayz::FLootEntry& entry = loot_.Entries()[index];
            const NextDayz::FLootSlot* slot = loot_.ResolveSlot(entry.directorHandle);
            if (!slot || glm::distance(entry.worldPos, player_.Position()) > 220.0f) continue;
            ImU32 color = LootCategoryColor(slot->category);
            const char* state = "Available";
            double cooldownRemaining = 0.0;
            if (entry.state == NextDayz::FLootEntry::EState::Reserved)
            {
                color = IM_COL32(255, 230, 70, 245);
                state = "Reserved";
            }
            else if (entry.state == NextDayz::FLootEntry::EState::Cooldown)
            {
                color = IM_COL32(130, 140, 150, 210);
                state = "Cooldown";
                cooldownRemaining = std::max(0.0, slot->cooldownUntil - loot_.WorldSeconds());
            }
            ImVec2 ground;
            ImVec2 marker;
            if (!project(entry.worldPos, ground) ||
                !project(entry.worldPos + glm::vec3(0.0f, 1.4f, 0.0f), marker)) continue;
            line(ground, marker, color, 1.5f);
            draw->AddQuadFilled({marker.x, marker.y - 6.0f}, {marker.x + 6.0f, marker.y},
                                {marker.x, marker.y + 6.0f}, {marker.x - 6.0f, marker.y}, color);
            if (glm::distance(entry.worldPos, player_.Position()) <= 100.0f)
            {
                const std::string suffix = entry.state == NextDayz::FLootEntry::EState::Cooldown
                    ? fmt::format(" {:.0f}s", cooldownRemaining) : std::string();
                label({marker.x + 9.0f, marker.y - 8.0f}, color,
                      fmt::format("L{} {} | {} | {}{} | node #{}", index,
                                  entry.def ? entry.def->displayName : "Unknown",
                                  LootCategoryName(slot->category), state, suffix,
                                  entry.nodeInstanceId));
            }
        }
    }

    if (debugHitProxyOverlay_)
    {
        for (const FRecentWeaponTrace& recent : recentWeaponTraces_)
        {
            const NextDayz::FWeaponTraceEvent& trace = recent.trace;
            const bool zombieHit = trace.hitZombie;
            const ImU32 color = zombieHit ? IM_COL32(70, 255, 115, 255)
                : trace.hit ? IM_COL32(255, 75, 55, 255) : IM_COL32(255, 220, 65, 245);
            ImVec2 from;
            ImVec2 to;
            if (project(trace.origin, from) && project(trace.endPoint, to))
            {
                line(from, to, color, 2.0f);
                draw->AddCircleFilled(to, trace.hit ? 5.0f : 3.0f, color);
                label({to.x + 7.0f, to.y + 4.0f}, color,
                      fmt::format("shot {} pellet {}: {}{}", trace.sequence, trace.pellet,
                                  zombieHit ? "INFECTED" : trace.hit ? "BLOCKER #" : "MISS",
                                  trace.hit && !zombieHit ? std::to_string(trace.hitInstanceId) : std::string()));
            }
            Runtime::EngineHelper::DrawAuxLine(
                trace.origin, trace.endPoint,
                zombieHit ? glm::vec4(0.2f, 1.0f, 0.35f, 1.0f)
                          : trace.hit ? glm::vec4(1.0f, 0.2f, 0.1f, 1.0f)
                                      : glm::vec4(1.0f, 0.8f, 0.1f, 1.0f),
                2.0f, false);
        }
    }
}

void NextDayzGameInstance::SetMouseCaptured(bool captured)
{
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
        if (pressed && !actions_.IsActive() &&
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
    case SDLK_E:
        if (pressed && !actions_.IsActive() && !player_.IsTraversing())
        {
            if (const std::optional<NextDayz::FLootHandle> handle = loot_.ReserveHovered())
            {
                if (actions_.BeginLoot(*handle))
                {
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
    case SDLK_F5:
        if (pressed) showDebugPanel_ = !showDebugPanel_;
        return true;
    case SDLK_F6:
        if (pressed) debugZombieOverlay_ = !debugZombieOverlay_;
        return true;
    case SDLK_F7:
        if (pressed) debugLootOverlay_ = !debugLootOverlay_;
        return true;
    case SDLK_F8:
        if (pressed) debugHitProxyOverlay_ = !debugHitProxyOverlay_;
        return true;
    case SDLK_G:
        if (pressed) time_.ToggleOvercast();
        return true;
    case SDLK_T:
        if (pressed) time_.Skip(120.0); // debug: jump 2 game hours
        return true;
    case SDLK_TAB:
    case SDLK_I:
        if (pressed && !player_.IsTraversing()) SetInventoryOpen(!showInventory_);
        return true;
    case SDLK_ESCAPE:
        if (pressed)
        {
            if (showInventory_)
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
    if (!mouseCaptured_ || showInventory_)
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
    if (showInventory_)
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

void NextDayzGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg)
{
    reg.Add("sceneReady", [this]() -> Runtime::Agent::FAgentQueryValue { return sceneReady_; });
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
    reg.Add("firstPerson", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.IsFirstPerson(); });
    reg.Add("hoveredLoot", [this]() -> Runtime::Agent::FAgentQueryValue { return loot_.HoveredPrompt(); });
    reg.Add("lootRemaining",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(loot_.RemainingCount()); });
    reg.Add("lootCooldown",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(loot_.CooldownCount()); });
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
    reg.Add("debugVisible", [this]() -> Runtime::Agent::FAgentQueryValue { return showDebugPanel_; });
    reg.Add("debugZombieOverlay", [this]() -> Runtime::Agent::FAgentQueryValue { return debugZombieOverlay_; });
    reg.Add("debugLootOverlay", [this]() -> Runtime::Agent::FAgentQueryValue { return debugLootOverlay_; });
    reg.Add("debugHitProxyOverlay", [this]() -> Runtime::Agent::FAgentQueryValue { return debugHitProxyOverlay_; });
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
        return std::string(zombie->death == NextDayz::EZombieDeath::Forward ? "forward"
                           : zombie->death == NextDayz::EZombieDeath::Crumple ? "crumple" : "back");
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
            if (const NextDayz::FLootSlot* slot = loot_.ResolveSlot(entry.directorHandle))
                present[static_cast<size_t>(slot->category)] = true;
        }
        return static_cast<int64_t>(std::count(present.begin(), present.end(), false));
    });
    reg.Add("recentWeaponTraces", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(recentWeaponTraces_.size());
    });
    reg.Add("lastTraceResult", [this]() -> Runtime::Agent::FAgentQueryValue {
        if (recentWeaponTraces_.empty()) return std::string("none");
        const NextDayz::FWeaponTraceEvent& trace = recentWeaponTraces_.back().trace;
        if (!trace.hit) return std::string("miss");
        return trace.hitZombie ? std::string("zombie-hitbox") : std::string("world-blocker");
    });
}
