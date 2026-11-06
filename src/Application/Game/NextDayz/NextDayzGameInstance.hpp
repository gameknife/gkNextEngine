#pragma once

// ============================================================================
// NextDayzGameInstance.hpp - Orchestrates NextDayz: loads the riverland map,
// owns the player/weapon/inventory/loot/survival/infected/time systems and
// routes input, camera and HUD. Business state lives in the sub-systems; this
// class only wires them together and turns their events into feedback (see
// docs/projects/nextdayz/nextdayz-productization-design.md).
// ============================================================================

#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Assets/Data/RigAsset.hpp"
#include "Engine/Runtime/Scene/TerrainGrass.hpp"

#include <glm/vec2.hpp>

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Data/DeterministicRng.hpp"
#include "Application/Game/NextDayz/Audio/NextDayzAudio.hpp"
#include "Application/Game/NextDayz/Combat/CombatEffects.hpp"
#include "Application/Game/NextDayz/Combat/CombatSystem.hpp"
#include "Application/Game/NextDayz/Combat/NoiseSystem.hpp"
#include "Application/Game/NextDayz/Data/LootTables.hpp"
#include "Application/Game/NextDayz/Debugger/NextDayzDebugger.hpp"
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
#include "Application/Game/NextDayz/UI/NextDayzHUD.hpp"
#include "Gameplay/AI/NavGrid.h"
#include "Gameplay/Rig/RigInstance.h"
#include "Application/Game/NextDayz/World/Campaign.hpp"
#include "Application/Game/NextDayz/World/ExpeditionMaps.hpp"
#include "Application/Game/NextDayz/World/RescueHelicopter.hpp"

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
    // One simulation frame (OnTick wraps it with the debugger's freeze/step/timers).
    void SimulateFrame(float dt);
    bool CampaignEnabled() const;
    void InitializeCampaign();
    NextDayz::FCampaignSave CaptureCampaign() const;
    bool SaveCampaign();
    bool RestoreCampaign(const NextDayz::FCampaignSave& saved);
    bool DrawSafehouse();
    void Deploy();
    void CompleteDeployment();
    std::string ActiveMapId() const;
    void RequestExpeditionMap(const NextDayz::FExpeditionMap& map, bool deploy);
    void DrawWorldOperations();
    void LoadWorldAtlas();
    void TickMapAuthoring();
    enum class ESafehouseReturn
    {
        Extraction,
        Death,
        Debug,
    };
    void ReturnToSafehouse(ESafehouseReturn reason);
    void DebugExpeditionAction(NextDayz::Debug::EExpeditionAction action);
    void AdvanceExtraction(float dt);
    void FireSignal(NextDayz::FItemInstanceId id);
    bool BoardHelicopter();
    std::string ExtractionPrompt() const;
    void ConfigureCampaignCVars(NextCVar::FCVarSystem& cvars);
    void CampaignValidationCommand();
    void UpdateGrass(float dt);
    glm::vec3 ResolveSpawnPosition() const;
    // Inventory requests (HUD, quickbar, keys). Each runs one inventory
    // transaction and reports the outcome in the message feed.
    void EquipWeaponInstance(NextDayz::FItemInstanceId instanceId, int slot);
    void InstallAttachment(NextDayz::FItemInstanceId weapon, NextDayz::FItemInstanceId attachment);
    void RemoveAttachment(NextDayz::FItemInstanceId weapon, NextDayz::EAttachmentSlot slot);
    void WearItem(NextDayz::FItemInstanceId instanceId);
    void TakeOffItem(NextDayz::FItemInstanceId instanceId);
    void DropItem(NextDayz::FItemInstanceId instanceId, int count);
    void MoveItem(NextDayz::FItemInstanceId instanceId, NextDayz::FContainerId containerId);
    void PlaceItem(NextDayz::FItemInstanceId instanceId, NextDayz::FContainerId containerId, NextDayz::FGridPlacement placement);
    void SplitItem(NextDayz::FItemInstanceId instanceId);
    void TakeLoot(int lootIndex, NextDayz::FContainerId destination = 0,
                  NextDayz::EEquipSlot equipSlot = NextDayz::EEquipSlot::None,
                  std::optional<NextDayz::FGridPlacement> placement = std::nullopt);
    void UseInventoryItem(NextDayz::FItemInstanceId instanceId);
    void UseQuickSlot(int slot);
    void QuickBandage();
    void ToggleLantern();
    void UseRadio();
    void ThrowFirecrackers(NextDayz::FItemInstanceId instanceId);
    void SyncClothingVisuals();
    void Notify(NextDayz::L10n::FText text, int severity = 0);
    void UpdateSurvivalFeedback(float deltaSeconds);
    void UpdateWorldPressure();
    void UpdateFirecrackers(float deltaSeconds);
    void InjectLootCatalog(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
    void InjectWeaponCatalog(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
    std::vector<NextDayz::FLootSpotSeed> CollectLootSpots() const;
    void StartNewLife();
    void ApplyValidationLoadout();
    void CreateZombieVisuals();
    void SyncZombieVisuals(float deltaSeconds = 0.0f);
    void ConfigureZombieSpawnPoints();
    void BuildZombieNavigation();
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
    // Debugger plumbing (Debug/NextDayzDebugger): its view of the systems,
    // the per-frame facts only this class holds, and what it asks us to do.
    void BindDebugger();
    NextDayz::Debug::FGameFacts DebugFacts() const;
    void TeleportPlayer(const glm::vec3& feet);
    NextDayz::FZombieHandle SpawnZombieAt(NextDayz::EZombieProfile profile, const glm::vec3& feet);
    void KillZombie(NextDayz::FZombieHandle zombie);
    void GiveItem(const std::string& itemId, int count);
    void ApplyDebugSelection(const std::string& selection);

    NextDayz::FConfig config_{};
    NextDayz::FCampaign campaign_;
    NextDayz::FExtractionConfig extractionConfig_;
    NextDayz::RescueHelicopter rescueHelicopter_;
    std::filesystem::path campaignPath_;
    std::string campaignMap_;
    std::vector<NextDayz::FExpeditionMap> expeditionMaps_;
    std::unordered_map<std::string, NextDayz::FMapWorldSave> mapStates_;
    std::optional<NextDayz::FCampaignSave> pendingCampaign_;
    std::string loadingMapPath_;
    bool pendingDeployment_ = false;
    bool pendingFreshMap_ = false;
    uint64_t geographySceneGeneration_ = 0;
    bool authoringMode_ = false;
    bool switchingCampaignContext_ = false;
    std::string authoringMapId_;
    std::string authoringWatchScene_;
    std::string authoringAppliedStamp_;
    std::string authoringPendingStamp_;
    std::vector<std::filesystem::path> authoringWatchFiles_;
    std::chrono::steady_clock::time_point authoringNextPoll_{};
    std::string validationDeployMap_;
    int safehouseTab_ = 0;
    int selectedWorldMap_ = 0;
    float atlasZoom_ = 1.0f;
    glm::vec2 atlasPan_{};
    std::vector<std::vector<glm::vec2>> atlasPolygons_;
    bool atlasLoaded_ = false;
    std::string saveError_;
    NextDayz::L10n::FText campaignMessage_;
    float autosaveElapsed_ = 0.0f;
    bool campaignSaveBlocked_ = false;
    bool validationCampaign_ = false;
    bool validationCampaignFresh_ = false;
    std::string validationCampaignPath_ = "out/nextdayz-validation/campaign.json";
    std::string validationCampaignCommand_;
    NextDayz::FDeterministicRng sessionRng_;
    NextDayz::FDeterministicRng survivalRng_;   // wounds, corpse loot, firecracker throws

    NextDayz::PlayerController player_;
    NextDayz::PlayerActionController actions_;
    NextDayz::SurvivalSystem survival_;
    NextDayz::PlayerRigVisual rig_;
    NextDayz::WeaponSystem weapons_;
    NextDayz::Inventory inventory_;
    NextDayz::LootSystem loot_;
    NextDayz::FContainerId lootDestination_ = 0;
    NextDayz::EEquipSlot lootEquipSlot_ = NextDayz::EEquipSlot::None;
    std::optional<NextDayz::FGridPlacement> lootPlacement_;
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
    Runtime::FTerrainGrassSystem grass_;
    std::vector<Runtime::FGrassInteractor> grassInteractors_;
    bool grassPlayerInteraction_ = true;
    bool grassZombieInteraction_ = true;

    // Held weapon models (nextdayz_weapons.scad) shared by FPS view model and TPS attachment.
    Assets::FRigAsset weaponCatalogAsset_{};
    bool weaponCatalogLoaded_ = false;
    Assets::FRigAsset attachmentCatalogAsset_{};
    bool attachmentCatalogLoaded_ = false;
    std::array<uint32_t, NextDayz::kWeapons.size()> weaponModelIds_{};
    std::array<NextDayz::FWeaponMaterialSlots, NextDayz::kWeapons.size()> weaponMaterialIds_{};
    Assets::FRigAsset zombieRigAsset_{};
    NextDayz::ZombieRigVisual::FClips zombieRigClips_{};
    std::vector<uint32_t> zombieRigPartModelIds_;
    std::vector<std::array<uint32_t, 16>> zombieRigPartMaterialIds_;
    std::array<uint32_t, 8> zombieTintMaterialIds_{};   // shirt colours, one per body slot
    bool zombieRigLoaded_ = false;
    std::vector<NextDayz::ZombieRigVisual> zombieRigVisuals_;
    // Item models for loot spots and piles (nextdayz_loot_items.scad).
    Assets::FRigAsset lootCatalogAsset_{};
    bool lootCatalogLoaded_ = false;

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

    // Survival feedback: messages, quickbar, map, lantern, radio cache, thrown firecrackers.
    std::vector<NextDayz::FHudNotification> notifications_;
    std::array<std::string, 4> quickSlots_{};
    bool showMap_ = false;
    bool lanternLit_ = false;
    float lanternSeconds_ = 0.0f;
    std::optional<glm::vec3> cacheMarker_;
    float cacheSeconds_ = 0.0f;
    struct FFirecracker
    {
        glm::vec3 position{0.0f};
        float timer = 0.0f;
        int bangs = 0;
    };
    std::vector<FFirecracker> firecrackers_;
    int lastWounds_ = 0;
    bool warnedHunger_ = false;
    bool warnedThirst_ = false;
    bool warnedCold_ = false;
    bool warnedWeak_ = false;
    bool lastShotMelee_ = false;

    // In-game debugger (F5). Owns the debug knobs: time scales, god mode, orbit camera.
    NextDayz::Debug::NextDayzDebugger debugger_;
    bool debugPanelCvar_ = false;
    bool debugDumpCvar_ = false;
    int32_t debugStepCvar_ = 0;
    std::string debugLayersCvar_;
    std::string debugTabCvar_;
    std::string debugSelectCvar_;
    std::string debugExpeditionCvar_;

    bool sceneReady_ = false;
    bool showInventory_ = false;
    bool validationLoadout_ = false;
    bool validationAddBackpack_ = false;
    bool validationAddSupplies_ = false;
    bool validationSpawnZombie_ = false;
    bool validationUseWater_ = false;
    bool validationFinishZombie_ = false;
    bool validationRestart_ = false;
    bool validationPreloadItemIcons_ = false;
    float validationLookYaw_ = 0.0f;
    float validationLookPitch_ = 0.0f;
    bool validationThirdPerson_ = false;
    float validationZombieDistance_ = 2.2f;
    int32_t validationHitZombieZone_ = 0;
    bool validationInvulnerable_ = false;
    NextDayz::FZombieHandle validationZombie_{};
    float validationDamagePlayer_ = 0.0f;
    float validationHunger_ = -1.0f;
    float validationHydration_ = -1.0f;
    int32_t validationBleed_ = 0;
    float debugHour_ = -1.0f;
    int32_t debugWeather_ = -1;
    float debugFogRadiance_ = -1.0f;
    float debugFogDensityScale_ = 1.0f;
    std::string validationGive_;
    std::string validationUse_;
    std::string validationEquip_;
    std::string validationAttachment_;
    std::string validationDrop_;
    std::string validationTeleport_;
    uint64_t validationAttackSequence_ = 0;
    bool paused_ = false;
    double survivalSeconds_ = 0.0;
    NextDayz::FWorldAnchorHandle hoveredWell_{};
    bool mouseCaptured_ = false;
    bool leftAltHeld_ = false;
    bool rightAltHeld_ = false;
    bool resetMouse_ = true;
    glm::dvec2 mousePos_{0.0, 0.0};
};
