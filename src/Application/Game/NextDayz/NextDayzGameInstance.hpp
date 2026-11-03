#pragma once

// ============================================================================
// NextDayzGameInstance.hpp - Orchestrates the NextDayz MVP: loads the coldwar
// map, owns the player/weapon/inventory/loot/time systems and routes input,
// camera and HUD. Business state lives in the sub-systems; this class only
// wires them together (see docs/projects/nextdayz/nextdayz-mvp-design.md).
// ============================================================================

#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Assets/Data/RigAsset.hpp"

#include <glm/vec2.hpp>

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Data/DeterministicRng.hpp"
#include "Application/Game/NextDayz/Audio/NextDayzAudio.hpp"
#include "Application/Game/NextDayz/Combat/CombatEffects.hpp"
#include "Application/Game/NextDayz/Combat/CombatSystem.hpp"
#include "Application/Game/NextDayz/Combat/NoiseSystem.hpp"
#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Inventory/LootSystem.hpp"
#include "Application/Game/NextDayz/Player/PlayerController.hpp"
#include "Application/Game/NextDayz/Player/PlayerActionController.hpp"
#include "Application/Game/NextDayz/Player/PlayerRigVisual.hpp"
#include "Application/Game/NextDayz/Player/SurvivalSystem.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponSystem.hpp"
#include "Application/Game/NextDayz/World/TimeSystem.hpp"
#include "Application/Game/NextDayz/World/WorldAnchorRegistry.hpp"
#include "Application/Game/NextDayz/World/ZombieSpawnDirector.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieRigVisual.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieVisualPool.hpp"
#include "Gameplay/AI/NavGrid.h"
#include "Gameplay/Rig/RigInstance.h"

class NextDayzGameInstance : public NextGameInstanceBase
{
public:
    NextDayzGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~NextDayzGameInstance() override = default;

    // lifecycle
    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
    void RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg) override;

    // scene
    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;
    void OnSceneUnloaded() override;

    // camera + ui
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;
    bool OnRenderUI() override;
    bool ShouldRenderUiDuringScreenshot() const override { return true; }

    // input
    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;

private:
    glm::vec3 ResolveSpawnPosition() const;
    void EquipFromInventory(const std::string& weaponId, int slot);
    void ToggleClothing(const std::string& clothingId, bool on);
    void UseInventoryItem(NextDayz::FItemInstanceId instanceId);
    void ApplyValidationLoadout();
    void CreateZombieVisuals();
    void SyncZombieVisuals(float deltaSeconds = 0.0f);
    void ConfigureZombieSpawnPoints();
    void BuildZombieNavigation();
    void DrawDebugWorldOverlay() const;
    void HandleDamageEvents();
    void UpdateZombieAudio(float deltaSeconds);
    void ValidationHitZombie(NextDayz::EHitZone zone);
    // The infected agent fixtures talk about: the last one spawnZombie made,
    // else the first active one.
    const NextDayz::FZombieRuntime* FocusZombie() const;
    void ResetSession();
    std::string CurrentObjective() const;
    void SetInventoryOpen(bool open);
    void SetMouseCaptured(bool captured);

    NextDayz::FConfig config_{};
    NextDayz::FDeterministicRng sessionRng_;

    NextDayz::PlayerController player_;
    NextDayz::PlayerActionController actions_;
    NextDayz::SurvivalSystem survival_;
    NextDayz::PlayerRigVisual rig_;
    NextDayz::WeaponSystem weapons_;
    NextDayz::Inventory inventory_;
    NextDayz::LootSystem loot_;
    NextDayz::WorldAnchorRegistry worldAnchors_;
    NextDayz::ZombieSystem zombies_;
    NextDayz::CombatSystem combat_{zombies_};
    NextDayz::CombatEffects effects_;
    NextDayz::NextDayzAudio audio_;
    NextDayz::NoiseSystem noise_;
    NextDayz::ZombieSpawnDirector zombieSpawns_;
    NextDayz::ZombieVisualPool zombieVisuals_;
    NextGameplay::FNavGrid zombieNavGrid_;
    NextDayz::TimeSystem time_;

    // Injected weapon silhouettes shared by FPS view model and TPS attachment.
    std::array<uint32_t, NextDayz::kWeapons.size()> weaponModelIds_{};
    std::array<uint32_t, NextDayz::kWeapons.size()> weaponMaterialIds_{};
    Assets::FRigAsset zombieRigAsset_{};
    NextDayz::ZombieRigVisual::FClips zombieRigClips_{};
    std::vector<uint32_t> zombieRigPartModelIds_;
    std::vector<std::array<uint32_t, 16>> zombieRigPartMaterialIds_;
    std::array<uint32_t, 8> zombieTintMaterialIds_{};   // shirt colours, one per body slot
    bool zombieRigLoaded_ = false;
    std::vector<NextDayz::ZombieRigVisual> zombieRigVisuals_;

    struct FRecentWeaponTrace
    {
        NextDayz::FWeaponTraceEvent trace;
        float remainingSeconds = 0.0f;
    };
    std::vector<FRecentWeaponTrace> recentWeaponTraces_;
    std::vector<NextDayz::FZombieHandle> zombieVisualOwners_;

    // What each infected last sounded like, so state changes cue their voice once.
    struct FZombieAudio
    {
        NextDayz::FZombieHandle handle{};
        NextDayz::EZombieState state = NextDayz::EZombieState::Dormant;
        float groanSeconds = 0.0f;
        float fallSeconds = -1.0f;
        uint32_t groans = 0;
    };
    std::vector<FZombieAudio> zombieAudio_;
    NextDayz::EWeaponPresentationAction lastWeaponAction_ = NextDayz::EWeaponPresentationAction::None;

    // Combat feedback: hit marker on the crosshair, hurt vignette + direction.
    float hitMarkerSeconds_ = 0.0f;
    bool hitMarkerKill_ = false;
    bool hitMarkerHead_ = false;
    float hurtSeconds_ = 0.0f;
    float hurtYawRadians_ = 0.0f;

    bool sceneReady_ = false;
    bool showInventory_ = false;
    bool showDebugPanel_ = false;
    bool debugZombieOverlay_ = true;
    bool debugLootOverlay_ = true;
    bool debugHitProxyOverlay_ = true;
    bool validationLoadout_ = false;
    bool validationAddBackpack_ = false;
    bool validationAddSupplies_ = false;
    bool validationSpawnZombie_ = false;
    bool validationUseWater_ = false;
    bool validationFinishZombie_ = false;
    bool validationRestart_ = false;
    // Debug orbit camera around the third-person rig (agent screenshots, tuning).
    float validationLookYaw_ = 0.0f;
    float validationLookPitch_ = 0.0f;
    bool validationThirdPerson_ = false;
    bool debugOrbit_ = false;
    float debugOrbitYaw_ = 90.0f;      // degrees from behind the body, + = toward its right
    float debugOrbitDistance_ = 3.0f;
    float debugOrbitHeight_ = 1.05f;
    bool debugOrbitZombie_ = false;    // orbit the nearest infected instead
    float debugZombieTimeScale_ = 1.0f;
    float debugFxTimeScale_ = 1.0f;
    float validationZombieDistance_ = 2.2f;
    int32_t validationHitZombieZone_ = 0;
    bool validationInvulnerable_ = false;
    NextDayz::FZombieHandle validationZombie_{};
    float validationDamagePlayer_ = 0.0f;
    float validationHunger_ = -1.0f;
    float validationHydration_ = -1.0f;
    uint64_t validationAttackSequence_ = 0;
    bool paused_ = false;
    double survivalSeconds_ = 0.0;
    NextDayz::FWorldAnchorHandle hoveredWell_{};
    bool mouseCaptured_ = false;
    bool resetMouse_ = true;
    glm::dvec2 mousePos_{0.0, 0.0};
};
