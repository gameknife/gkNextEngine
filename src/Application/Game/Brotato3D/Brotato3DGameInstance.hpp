#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Brotato3DDataLoader.hpp"
#include "Brotato3DDebris.hpp"
#include "Brotato3DEnemy.hpp"
#include "Brotato3DPlayer.hpp"
#include "Brotato3DPlayerRigVisual.hpp"
#include "Brotato3DProjectile.hpp"
#include "Brotato3DShop.hpp"
#include "Brotato3DWaveSystem.hpp"
#include "Brotato3DWeapon.hpp"
#include "Brotato3DZombieVisual.hpp"
#include "Brotato3DArenaNav.hpp"

#include <glm/ext.hpp>
#include <random>

struct ImVec2;
struct ImFont;
namespace Runtime { class LightComponent; class RenderComponent; }
namespace NextCVar { class FCVarSystem; }

namespace Brotato3D
{
    enum class EAppState : uint8_t
    {
        MainMenu,
        CharacterSelect,
        Playing,
        Hitstop,
        Paused,
        LevelUpPicking,
        Shopping,
        Result,
    };

    struct FFloatingText
    {
        glm::vec3 worldPos = glm::vec3(0.0f);
        std::string text;
        glm::vec4 color = glm::vec4(1.0f);
        float lifeMs = 0.0f;
        float remainingMs = 0.0f;
        float fontScale = 1.0f;
    };

    struct FMuzzleFlash
    {
        glm::vec3 worldPos = glm::vec3(0.0f);
        glm::vec3 color = glm::vec3(1.0f);
        float lifeMs = 80.0f;
        float remainingMs = 0.0f;
    };

    struct FBestRecord
    {
        int totalWins = 0;
        int totalKills = 0;
        float fastestCompletionSec = 0.0f;
        std::map<std::string, int> characterWins;
    };
}

class Brotato3DGameInstance : public NextGameInstanceBase
{
public:
    Brotato3DGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~Brotato3DGameInstance() override = default;

    void OnInit() override;
    void OnInitUI() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    bool OnKey(SDL_Event& event) override;
    void OnSceneLoaded() override;
    void OnSceneUnloaded() override;
    bool OnGamepadInput(int16_t leftStickX,
                        int16_t leftStickY,
                        int16_t rightStickX,
                        int16_t rightStickY,
                        int16_t leftTrigger,
                        int16_t rightTrigger) override;
    void BeforeSceneRebuild(std::vector<std::shared_ptr<::Assets::Node>>& nodes,
                            std::vector<::Assets::Model>& models,
                            std::vector<::Assets::FMaterial>& materials,
                            std::vector<::Assets::LightObject>& lights,
                            std::vector<::Assets::AnimationTrack>& tracks) override;
    bool OverrideRenderCamera(::Assets::Camera& outRenderCamera) const override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
    void RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg) override;

    Brotato3D::FPlayerRuntime& GetPlayer() { return player_; }
    const Brotato3D::FPlayerRuntime& GetPlayer() const { return player_; }
    const std::vector<Brotato3D::FEnemyRuntime>& GetEnemies() const { return enemies_; }
    const std::vector<Brotato3D::FWeaponRuntime>& GetWeapons() const { return equippedWeapons_; }
    const std::vector<Brotato3D::FProjectileRuntime>& GetProjectiles() const { return projectilePool_; }
    const std::vector<Brotato3D::FFloatingText>& GetFloatingTexts() const { return floatingTexts_; }
    const std::vector<Brotato3D::FMuzzleFlash>& GetMuzzleFlashes() const { return muzzleFlashes_; }
    const std::vector<Brotato3D::FExpandingRing>& GetExplosionRings() const { return explosionRings_; }
    const std::vector<Brotato3D::FLaserBeam>& GetLaserBeams() const { return laserBeams_; }
    const std::vector<Brotato3D::FGroundIndicator>& GetGroundIndicators() const { return groundIndicators_; }
    const Brotato3D::FWaveSystem& GetWaveSystem() const { return waveSystem_; }
    Brotato3D::EAppState GetAppState() const { return appState_; }
    int GetXpToNextLevel() const;
    int GetKillCount() const { return killCount_; }
    int GetTotalMaterialsGained() const { return totalMaterialsGained_; }
    float GetRunElapsedSec() const { return runElapsedSec_; }
    float GetDamageFlashMs() const { return damageFlashMs_; }
    float GetWaveBannerMs() const { return waveBannerMs_; }
    const std::string& GetWaveBannerText() const { return waveBannerText_; }
    float GetWeaponMergeBannerMs() const { return weaponMergeBannerMs_; }
    const std::string& GetWeaponMergeBannerText() const { return weaponMergeBannerText_; }
    bool IsExtractionVehicleVisible() const { return extractionVehicleVisible_; }
    glm::vec3 GetExtractionVehiclePos() const { return extractionVehiclePos_; }
    // Where the player has to stand: beside the bus door, not at its centre.
    glm::vec3 GetExtractionZonePos() const;
    float GetExtractionRadius() const;
    ImFont* GetBigFont() const { return bigFont_; }
    bool IsPlayerDead() const { return playerDead_; }
    const std::vector<Brotato3D::FUpgradeCardDef>& GetCurrentUpgradeChoices() const { return currentUpgradeChoices_; }
    const std::vector<Brotato3D::FShopItemDef>& GetShopOffers() const { return shopOffers_; }
    const std::vector<std::string>& GetOwnedItemIds() const { return player_.ownedItemIds; }
    int GetRerollCost() const { return shop_.GetRerollCost(); }
    const Brotato3D::FItemDef* GetItemDef(const std::string& itemId) const;
    const std::vector<Brotato3D::FCharacterDef>& GetCharacterDefs() const { return characterDefs_; }
    const Brotato3D::FCharacterDef* GetSelectedCharacterDef() const;
    const std::string& GetSelectedCharacterId() const { return selectedCharacterId_; }
    const std::vector<Brotato3D::FArenaDef>& GetArenaDefs() const { return arenaDefs_; }
    const std::string& GetSelectedArenaId() const { return selectedArenaId_; }
    glm::vec2 GetArenaHalfExtent() const { return arenaHalfExtent_; }
    const Brotato3D::FBestRecord& GetBestRecord() const { return bestRecord_; }
    Brotato3D::FPlayerStats GetEffectivePlayerStats() const;
    float GetDashCooldownRatio() const;
    int GetDashCharges() const { return player_.dashCharges; }
    int GetDashMaxCharges() const;
    bool IsPlayerDashing() const { return player_.dashRemainingMs > 0.0f; }
    bool CanBuyShopOffer(size_t slotIndex) const;
    std::string GetShopOfferUnavailableReason(size_t slotIndex) const;
    void SelectUpgrade(size_t choiceIndex);
    void BuyShopItem(size_t slotIndex);
    void RerollShop();
    void ContinueFromShop();
    void RestartGame();
    void StartNewRun();
    void GoToMainMenu();
    void GoToCharacterSelect();
    void SelectCharacter(const std::string& characterId);
    void SelectArena(const std::string& arenaId);
    void ResumeGame();
    void PauseGame();
    void ExitGame();

private:
    struct FEnemyVisualResource
    {
        uint32_t modelId = 0;
        uint32_t materialId = 0;
        uint32_t darkMaterialId = 0;
        uint32_t hitFlashMaterialId = 0;
        uint32_t warningMaterialId = 0;
        uint32_t phase2MaterialId = 0;
        glm::vec3 baseColor = glm::vec3(1.0f);
    };

    void SpawnEnemy(const std::string& enemyId, const glm::vec3& worldPos);
    void UpdatePlayer(double deltaSeconds);
    void TryStartDash();
    void FinishDash();
    void UpdateEnemies(double deltaSeconds);
    void UpdateWeapons(double deltaSeconds);
    void UpdateProjectiles(double deltaSeconds);
    void UpdateEnemyProjectiles(double deltaSeconds);
    void UpdateDebris(double deltaSeconds);
    void UpdateCombatEffects(double deltaSeconds);
    void UpdateGroundIndicators(double deltaSeconds);
    void UpdateWaveBanner(double deltaSeconds);
    void UpdateFloatingTexts(double deltaSeconds);
    void SpawnEnemyProjectile(const Brotato3D::FEnemyRuntime& enemy, const glm::vec3& dir);
    void KillEnemy(Brotato3D::FEnemyRuntime& enemy, bool dropLoot);
    int CalculateWeaponDamage(const Brotato3D::FWeaponDef& weaponDef, bool& outIsCrit);
    int ApplyDamageToEnemy(Brotato3D::FEnemyRuntime& enemy, int damage, bool isCrit);
    void ApplyWeaponKnockback(Brotato3D::FEnemyRuntime& enemy, const glm::vec3& direction, float knockbackMeters);
    // --- zombie bodies, dismemberment and gore (Brotato3DEnemySystem.cpp / Brotato3DGoreSystem.cpp)
    void LoadZombieRigs();
    std::shared_ptr<Brotato3D::FZombieRigVisual> CreateZombieVisual(const std::string& enemyId, size_t serial);
    void UpdateEnemyBehaviour(Brotato3D::FEnemyRuntime& enemy, double deltaSeconds, float deltaMs);
    void UpdateEnemyPresentation(Brotato3D::FEnemyRuntime& enemy, const glm::vec3& previousPos, double deltaSeconds);
    void SeparateEnemies(double deltaSeconds);
    void UpdateEnemyCorpse(Brotato3D::FEnemyRuntime& enemy, double deltaSeconds);
    void BeginEnemyCorpse(Brotato3D::FEnemyRuntime& enemy, bool gib);
    void HideEnemyVisual(Brotato3D::FEnemyRuntime& enemy);
    void PlayEnemyAction(Brotato3D::FEnemyRuntime& enemy, Brotato3D::EZombieAction action, float playRate = 1.0f);
    // Called for every hit after HP was deducted: flinch, blood, and limbs lost at HP thresholds.
    void ReactEnemyToDamage(Brotato3D::FEnemyRuntime& enemy, int damage, bool isCrit);
    bool SeverEnemyLimb(Brotato3D::FEnemyRuntime& enemy, Brotato3D::EZombieLimb limb, const glm::vec3& direction, float force);
    bool SeverRandomEnemyLimb(Brotato3D::FEnemyRuntime& enemy, const glm::vec3& direction, bool preferLeg);
    void BuildGorePool(std::vector<::Assets::Model>& models,
                       std::vector<::Assets::FMaterial>& materials,
                       std::vector<std::shared_ptr<::Assets::Node>>& nodes);
    void SpawnBloodBurst(const glm::vec3& worldPos, const glm::vec3& direction, int count, float speed = 5.0f);
    void SpawnBloodDecal(const glm::vec3& worldPos, float radius);
    void ClearGore();
    // --- arena obstacles, navigation and sight lines (Brotato3DArenaSystem.cpp)
    void BuildArenaObstacles();
    void ClearArenaObstacles();
    void UpdateArenaNavigation(double deltaSeconds);
    void UpdateOccluderFade(double deltaSeconds);
    glm::vec3 ChaseDirection(Brotato3D::FEnemyRuntime& enemy, const glm::vec3& toPlayerDir, double deltaSeconds);
    void DamagePlayer(int damage, float shakeMs, float flashMs);
    void BuildDebrisPool(std::vector<::Assets::Model>& models,
                         std::vector<::Assets::FMaterial>& materials,
                         std::vector<std::shared_ptr<::Assets::Node>>& nodes);
    void BuildKinematicCollisionBodies(std::vector<::Assets::Model>& models,
                                       std::vector<::Assets::FMaterial>& materials,
                                       std::vector<std::shared_ptr<::Assets::Node>>& nodes);
    void SpawnDebris(Brotato3D::EDebrisKind kind,
                     const glm::vec3& worldPos,
                     const glm::vec3& impulseDir,
                     float speed,
                     uint32_t materialId,
                     int count,
                     float angleConeRad,
                     Brotato3D::EDebrisPayload payload = Brotato3D::EDebrisPayload::None,
                     int payloadValuePerSlot = 0,
                     float lifetimeMs = 0.0f);
    void ClearAllDebris(bool keepPickable);
    void SpawnHitXpDebris(const glm::vec3& worldPos, const glm::vec3& projectileDir, int damage, uint32_t materialId);
    void SpawnKillMaterialDebris(const Brotato3D::FEnemyRuntime& enemy,
                                 int countMultiplier = 1,
                                 float spawnRadius = 0.45f,
                                 int minCount = 0);
    void SpawnPlayerDamageDebris(int damage);
    void PushMuzzleFlash(const glm::vec3& worldPos, const glm::vec3& color);
    void SpawnTempLight(const glm::vec3& worldPos, const glm::vec3& color, float radiusMeters, float durationMs);
    void UpdateLightArea(Runtime::LightComponent* lightComponent, const glm::vec3& worldPos,
                         float radiusMeters, float intensityScale);
    uint32_t EnsureLightMaterial(const glm::vec3& color);
    void StartScreenShake(float durationMs, float intensity);
    void PushExplosionRing(const glm::vec3& worldPos, const glm::vec4& color, float maxRadius);
    void PushLaserBeam(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, float durationMs, float width);
    void PushGroundIndicator(const Brotato3D::FGroundIndicator& indicator);
    void CancelGroundIndicatorsForEnemy(uint32_t enemyTag);
    bool UpdateMortarTank(Brotato3D::FEnemyRuntime& enemy,
                          double deltaSeconds,
                          float deltaMs,
                          const glm::vec3& toPlayerDir,
                          float playerDistance);
    bool UpdateLanceCharger(Brotato3D::FEnemyRuntime& enemy,
                            double deltaSeconds,
                            float deltaMs,
                            const glm::vec3& toPlayerDir,
                            float playerDistance);
    void ClearAliveEnemies(bool dropLoot);
    void PushFloatingText(const glm::vec3& worldPos,
                          std::string text,
                          const glm::vec4& color,
                          float lifeMs,
                          float fontScale = 1.0f);
    void BeginLevelUp();
    void RollUpgradeChoices();
    void ApplyUpgrade(const std::string& stat, float delta);
    void ApplyShopItem(const Brotato3D::FShopItemDef& item);
    bool BuyPassiveItem(const Brotato3D::FShopItemDef& item);
    void ApplyPassiveItemStats(const Brotato3D::FItemDef& item);
    Brotato3D::FPlayerStats GetEffectiveStats() const;
    Brotato3D::FWeaponRuntime CreateWeaponRuntime(const std::string& weaponId, int tier) const;
    Brotato3D::FWeaponDef CreateTier2Weapon(const Brotato3D::FWeaponDef& base) const;
    void NormalizeWeaponDefPointers();
    bool CanBuyWeaponCard(const std::string& weaponId) const;
    bool BuyWeaponCard(const Brotato3D::FShopItemDef& item);
    void TryMergeWeapons(const std::string& weaponId);
    void ProcessItemTriggers(double deltaSeconds);
    void ProcessTickItemTriggers(float deltaMs);
    void ProcessLowHpItemBuffs();
    void ProcessOnKillTriggers(const glm::vec3& worldPos);
    void ProcessDashEndItemTriggers();
    void ApplyDashKnockbackItem(const Brotato3D::FItemDef& item);
    void ApplyItemExplosionDamage(const glm::vec3& worldPos, float radius, int damage);
    void StartShopping();
    void EnterResult(bool playerDead);
    void ResetRuntimeState();
    void ApplySelectedCharacter();
    void ApplySelectedArena();
    void LoadBestRecord();
    void SaveBestRecord() const;
    void UpdateBestRecord(bool playerDead);
    const Brotato3D::FCharacterDef* FindCharacterDef(const std::string& characterId) const;
    void BeginWaveBanner();
    void SetDebugSingleWeapon(const std::string& weaponId);
    void ApplyLightingSettings();
    void SetSkyIntensityTarget(float target, float transitionMs);
    void UpdateSkyTransition(double deltaSeconds);
    void UpdateSunLighting(double deltaSeconds);
    void BeginDuskSurge();
    void UpdateExtractionVehicle(double deltaSeconds);
    void ResetExtractionVehicle();
    void SetExtractionVehicleVisible(bool visible);
    bool IsExtractionVehicleObstacleActive() const;
    glm::vec3 ResolveExtractionVehicleCollision(const glm::vec3& pos, float radius) const;
    glm::vec3 ResolvePlayerObstacleCollision(const glm::vec3& pos, float radius);
    // True when the extraction bus or a building stands between the two points.
    bool IsSegmentBlocked(const glm::vec3& from, const glm::vec3& to, float radius) const;
    // Clamps a candidate enemy position to the arena, pushes it out of the extraction truck,
    // re-clamps, then snaps it onto the flat gameplay plane shared by the fixed SCAD scenes.
    glm::vec3 ResolveEnemyGroundedPosition(const Brotato3D::FEnemyRuntime& enemy, glm::vec3 candidate) const;
    bool IsDuskSurgeActive() const;
    void ClearMovementInput();
    bool ShouldPauseWorldPhysics() const;
    void SetWorldPhysicsPaused(bool paused);
    void BuildArenaWallBodies();
    void ClearArenaWallBodies();
    void UpdateCameraTracking(double deltaSeconds);
    glm::vec3 RandomDebugSpawnPosition();
    NextBodyID AcquireEnemyKinematicBody(const std::string& enemyId) const;
    void SyncPlayerKinematicBody(double deltaSeconds);
    void SyncEnemyKinematicBody(Brotato3D::FEnemyRuntime& enemy, double deltaSeconds);
    void DeactivateEnemyKinematicBody(Brotato3D::FEnemyRuntime& enemy);
    const Brotato3D::FArenaDef* FindArenaDef(const std::string& arenaId) const;

    Brotato3D::EAppState appState_ = Brotato3D::EAppState::MainMenu;
    glm::vec2 arenaHalfExtent_ = glm::vec2(12.0f, 8.0f);
    Brotato3D::FPlayerRuntime player_{};
    Brotato3D::FPlayerRigVisual playerRig_{};
    std::map<std::string, Brotato3D::FEnemyDef> enemyDefs_;
    std::map<std::string, Brotato3D::FWeaponDef> weaponDefs_;
    std::vector<Brotato3D::FUpgradeCardDef> upgradeCards_;
    std::vector<Brotato3D::FShopItemDef> shopItems_;
    std::vector<Brotato3D::FItemDef> itemDefs_;
    std::map<std::string, Brotato3D::FItemDef> itemDefsById_;
    std::vector<Brotato3D::FCharacterDef> characterDefs_;
    std::vector<Brotato3D::FArenaDef> arenaDefs_;
    std::string selectedCharacterId_ = "soldier";
    std::string selectedArenaId_ = "deadly_town";
    Brotato3D::FBestRecord bestRecord_{};
    std::vector<Brotato3D::FWaveDef> waveDefs_;
    std::map<std::string, FEnemyVisualResource> enemyVisuals_;
    // One rig resource per enemy id; unique_ptr keeps FRigAsset addresses stable for the animators.
    std::map<std::string, std::unique_ptr<Brotato3D::FZombieRigResource>> zombieRigs_;
    struct FBloodDecal
    {
        std::shared_ptr<::Assets::Node> node;
    };
    std::vector<FBloodDecal> bloodDecals_;
    size_t nextBloodDecal_ = 0;
    uint32_t bloodMatId_ = 0;
    uint32_t bloodDarkMatId_ = 0;
    int corpseCount_ = 0;
    Brotato3D::FArenaNav arenaNav_;
    float flowRefreshMs_ = 0.0f;
    struct FOccluder
    {
        std::vector<Runtime::RenderComponent*> renders;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        float opacity = 1.0f;
    };
    std::vector<FOccluder> occluders_;
    // Agent validation switches (Brotato3DValidation.cpp).
    struct FValidation
    {
        bool start = false;
        bool lineup = false;
        bool horde = false;
        bool sever = false;
        bool killAll = false;
        bool clear = false;
        bool dusk = false;
        bool board = false;
        bool god = false;
        bool freeze = false;
        bool holdFire = false;
        bool pauseWaves = false;
        bool autoUpgrade = false;
        std::string arena;
        float cameraHeight = 0.0f;
        int inputEvents = 0;          // movement / dash key and pad events received (diagnostics)
        float cameraBack = 0.0f;
    } validation_{};
    size_t zombieVisualSerial_ = 0;
    std::vector<Brotato3D::FEnemyRuntime> enemies_;
    std::vector<Brotato3D::FWeaponRuntime> equippedWeapons_;
    std::vector<Brotato3D::FProjectileRuntime> projectilePool_;
    std::vector<Brotato3D::FEnemyProjectileRuntime> enemyProjectilePool_;
    std::vector<Brotato3D::FDebrisRuntime> debrisPool_;
    std::vector<Brotato3D::FFloatingText> floatingTexts_;
    std::vector<Brotato3D::FMuzzleFlash> muzzleFlashes_;
    std::vector<Brotato3D::FExpandingRing> explosionRings_;
    std::vector<Brotato3D::FLaserBeam> laserBeams_;
    std::vector<Brotato3D::FGroundIndicator> groundIndicators_;
    Brotato3D::FWaveSystem waveSystem_;
    Brotato3D::FShop shop_;
    std::vector<Brotato3D::FUpgradeCardDef> currentUpgradeChoices_;
    std::vector<Brotato3D::FShopItemDef> shopOffers_;
    uint32_t projectileModelId_ = 0;
    uint32_t projectileMaterialId_ = 0;
    uint32_t enemyProjectileModelId_ = 0;
    uint32_t enemyProjectileMaterialId_ = 0;
    std::map<const Brotato3D::FEnemyDef*, uint32_t> enemyProjectileMaterialIds_;
    std::map<std::string, uint32_t> lightMaterialIds_;
    struct FTempLightRuntime
    {
        std::shared_ptr<Runtime::LightComponent> lightComponent;
        glm::vec3 worldPos = glm::vec3(0.0f);
        float radiusMeters = 3.0f;
        float durationMs = 120.0f;
        float remainingMs = 0.0f;
        std::shared_ptr<::Assets::Node> node;
        bool active = false;
    };
    std::shared_ptr<Runtime::LightComponent> playerLightComponent_;
    std::shared_ptr<::Assets::Node> playerLightNode_;
    std::shared_ptr<Runtime::LightComponent> busLightComponent_;   // the bus floodlight over the door
    std::shared_ptr<::Assets::Node> busLightNode_;
    std::vector<FTempLightRuntime> tempLightPool_;
    uint32_t debrisTinyModelId_ = 0;
    uint32_t debrisChunkModelId_ = 0;
    uint32_t debrisBossChunkModelId_ = 0;
    uint32_t debrisFallbackTinyMatId_ = 0;
    uint32_t debrisFallbackChunkMatId_ = 0;
    uint32_t materialDebrisMatId_ = 0;
    uint32_t xpDebrisMatId_ = 0;
    uint32_t playerDebrisMatId_ = 0;
    uint64_t debrisTickCounter_ = 0;
    std::array<NextBodyID, 4> arenaWallBodyIds_{};
    NextBodyID playerKinematicBodyId_{};
    bool playerKinematicBodyActive_ = false;
    std::map<std::string, std::vector<NextBodyID>> enemyKinematicBodyPools_;
    std::map<std::string, uint32_t> characterMaterialIds_;
    std::vector<std::shared_ptr<::Assets::Node>> extractionVehicleNodes_;
    std::shared_ptr<::Assets::Node> extractionVehicleRootNode_;
    std::shared_ptr<::Assets::Node> extractionBeaconNode_;
    ::Assets::FRigAsset vehicleCatalog_{};   // brotato_vehicles.scad (static models packed as a rig)
    bool vehicleCatalogLoaded_ = false;
    glm::vec3 extractionVehicleForward_ = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec2 extractionVehicleHalfExtent_ = glm::vec2(1.45f, 0.78f);   // world XZ half size of the obstacle
    uint32_t extractionVehicleMaterialId_ = 0;
    uint32_t extractionVehicleActiveMaterialId_ = 0;
    bool keyW_ = false;
    bool keyA_ = false;
    bool keyS_ = false;
    bool keyD_ = false;
    glm::vec2 gamepadMoveInput_ = glm::vec2(0.0f);
    bool sceneReady_ = false;
    std::mt19937 rng_{std::random_device{}()};
    glm::vec3 cameraSmoothedTarget_ = glm::vec3(0.0f);
    glm::vec3 playerVelocity_ = glm::vec3(0.0f);
    glm::vec3 playerMoveVelocity_ = glm::vec3(0.0f);   // commanded, accelerated toward the input
    bool playerHasAimTarget_ = false;
    glm::vec3 extractionVehiclePos_ = glm::vec3(0.0f, -100.0f, 0.0f);
    glm::vec3 extractionVehicleStartPos_ = glm::vec3(0.0f, -100.0f, 0.0f);
    glm::vec3 extractionVehicleTargetPos_ = glm::vec3(0.0f, -100.0f, 0.0f);
    float extractionVehicleAnimMs_ = 0.0f;
    float extractionVehicleAnimTotalMs_ = 0.0f;
    bool extractionVehicleVisible_ = false;
    float currentSkyIntensity_ = 30.0f;
    float skyTransitionStartIntensity_ = 30.0f;
    float targetSkyIntensity_ = 30.0f;
    float skyTransitionTotalMs_ = 0.0f;
    float skyTransitionRemainingMs_ = 0.0f;
    float sunRotation_ = 0.28f;
    float sunLightingUpdateAccumMs_ = 0.0f;
    float screenShakeMs_ = 0.0f;
    float screenShakeIntensity_ = 0.0f;
    float damageFlashMs_ = 0.0f;
    float hitStopMs_ = 0.0f;
    float globalTimeScale_ = 1.0f;
    float bossKillFlashMs_ = 0.0f;
    float timeScaleRecoveryMs_ = 0.0f;
    float bossVictoryDelayMs_ = 0.0f;
    float waveBannerMs_ = 0.0f;
    std::string waveBannerText_;
    float runElapsedSec_ = 0.0f;
    int killCount_ = 0;
    int totalMaterialsGained_ = 0;
    bool playerDead_ = false;
    Brotato3D::FPlayerStats dynamicStatBuffs_{};
    float itemTickAccumMs_ = 0.0f;
    float itemHealRemainder_ = 0.0f;
    float weaponMergeBannerMs_ = 0.0f;
    std::string weaponMergeBannerText_;
    ImFont* bigFont_ = nullptr;
};
