#pragma once

// ============================================================================
// NextRaidenGameInstance.hpp - Wires NextRaiden together and owns the frame
// order. Rules live in the sub-systems; this class only sequences them.
//
// Design: docs/projects/nextraiden/nextraiden-mvp-design.md
// Plan:   docs/projects/nextraiden/nextraiden-mvp-plan.md
// P2 scope: a full stage - waves, ground targets shed from their own chunks,
// drops and wingmen, two rig bosses with scripted phases, and the flow that
// strings Ready -> Playing -> Boss -> StageClear together.
// ============================================================================

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Runtime/GameInstance.hpp"

#include "Application/Game/NextRaiden/Combat/BulletPool.hpp"
#include "Application/Game/NextRaiden/Combat/DebrisSystem.hpp"
#include "Application/Game/NextRaiden/Combat/RaidenRandom.hpp"
#include "Application/Game/NextRaiden/Level/PrefabRack.hpp"
#include "Application/Game/NextRaiden/Level/StageFlow.hpp"
#include "Application/Game/NextRaiden/Level/StageIndex.hpp"
#include "Application/Game/NextRaiden/Level/WaveDirector.hpp"
#include "Application/Game/NextRaiden/NextRaidenConfig.hpp"
#include "Application/Game/NextRaiden/Player/PlayerShip.hpp"
#include "Application/Game/NextRaiden/Player/ScrollCamera.hpp"
#include "Application/Game/NextRaiden/World/BossSystem.hpp"
#include "Application/Game/NextRaiden/World/EnemyPool.hpp"
#include "Application/Game/NextRaiden/World/GroundTargetSystem.hpp"
#include "Application/Game/NextRaiden/World/ItemSystem.hpp"

class NextRaidenGameInstance : public NextGameInstanceBase
{
public:
    NextRaidenGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~NextRaidenGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override {}
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
    void RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg) override;

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;
    void OnSceneUnloaded() override;

    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;
    bool OnRenderUI() override;
    /// The HUD columns are what make the portrait playfield readable, so every screenshot
    /// should include them - `gnb shot` without --ui would otherwise show the wrong framing.
    bool ShouldRenderUiDuringScreenshot() const override { return true; }

    bool OnKey(SDL_Event& event) override;
    bool OnGamepadInput(int16_t leftStickX, int16_t leftStickY, int16_t rightStickX, int16_t rightStickY,
                        int16_t leftTrigger, int16_t rightTrigger) override;

private:
    void FixedStep(float deltaSeconds);
    void ResolveCollisions();
    void HandleDeaths();
    void HandlePickups();
    void UpdateBossTrigger();
    void FireBomb();
    void AddScore(int amount);
    void LoadStage(size_t index, bool restartRun);
    void AdvanceStage();
    NextRaiden::FPlayerInput CollectInput();
    const NextRaiden::FStageDesc& CurrentStage() const;

    NextRaiden::FConfig config_{};
    NextRaiden::FPrefabRack rack_{};
    NextRaiden::FScrollCamera camera_{};
    NextRaiden::FPlayerShip player_{};
    NextRaiden::FBulletPool bullets_{};
    NextRaiden::FEnemyPool enemies_{};
    NextRaiden::FDebrisSystem debris_{};
    NextRaiden::FWaveDirector waves_{};
    NextRaiden::FStageIndex index_{};
    NextRaiden::FGroundTargetSystem grounds_{};
    NextRaiden::FItemSystem items_{};
    NextRaiden::FBossSystem bosses_{};
    NextRaiden::FStageFlow flow_{};
    NextRaiden::FRaidenRandom random_{};

    /// Reused across steps so a frame of combat allocates nothing.
    std::vector<NextRaiden::FEnemyDeath> deaths_;
    std::vector<NextRaiden::FGroundDeath> groundDeaths_;
    std::vector<NextRaiden::EItemKind> pickups_;

    std::vector<std::shared_ptr<Assets::Node>> playerPool_;
    std::vector<std::shared_ptr<Assets::Node>> optionPool_;

    size_t stageCursor_ = 0;
    bool sceneReady_ = false;
    float stageTime_ = 0.0f;
    float fixedAccumulator_ = 0.0f;

    int score_ = 0;
    int nextExtend_ = 0;
    int enemiesKilled_ = 0;
    int medalStreak_ = 0;
    /// High-water mark of live bullets since the stage loaded. The per-frame count is
    /// useless for a budget - it is whatever the sampling instant happened to catch -
    /// whereas the peak is the number the pools and the frame time have to survive.
    int bulletsPeak_ = 0;
    /// Index into the current stage's boss list; everything before it has been fought.
    size_t bossCursor_ = 0;
    bool stageEnded_ = false;
    /// RequestLoadScene is deferred to the end of the frame, so WantsNextStage() keeps
    /// answering yes for every remaining fixed step. Without this latch a stage clear queues
    /// the same load a dozen times over.
    bool stageAdvanceIssued_ = false;
    std::string stageRequest_;

    // CVar-backed overrides.
    float scrollSpeedOverride_ = 0.0f;  // 0 = use the stage's own speed
    bool freezeScroll_ = false;
    bool invincible_ = false;
    bool showHitboxes_ = false;
    int debrisBudget_ = 64;
    int forcedPower_ = 0;               // 0 = leave the player's own power alone
    bool spawnBossRequest_ = false;
    float bossHpRequest_ = -1.0f;   // <0 = leave the fight alone

    // Input state. Held keys are polled state, not events: an event stream would deliver
    // the OS key-repeat and the ship would stutter instead of flying.
    bool keyMove_[4] = {false, false, false, false};
    bool keySlow_ = false;
    bool keyFire_ = false;
    /// Held-trigger fire is still there; this just means the trigger is always considered
    /// held. Toggled with C, and surfaced in the HUD so its state is never a mystery.
    bool autoFire_ = true;
    bool bombPressed_ = false;
    glm::vec2 stickMove_{0.0f};
    bool padFire_ = false;
};
