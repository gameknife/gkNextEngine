#include "NextRaidenGameInstance.hpp"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keycode.h>
#include <fmt/format.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Gameplay/Rig/RigSubsystem.h"
#include "Modules/ScadLoader/ScadModule.hpp"

#include "Application/Game/NextRaiden/RaidenAudio.hpp"

using namespace NextRaiden;

namespace
{
    constexpr const char* kGameplayConfig = "assets/configs/nextraiden/gameplay.json";
    constexpr const char* kStagesConfig = "assets/configs/nextraiden/stages.json";

    /// Bullets need a step the frame rate cannot smear; 120 Hz is the cheapest rate at which
    /// a 60 m/s shot cannot skip past a 3 m enemy.
    constexpr float kFixedStep = 1.0f / 120.0f;
    constexpr float kMaxFrameSeconds = 0.25f;
    constexpr float kStickDeadzone = 0.22f;
    constexpr int kEnemiesPerKind = 16;
    constexpr int kMaxOptions = 2;

    float StickAxis(int16_t raw)
    {
        const float value = static_cast<float>(raw) / 32767.0f;
        if (std::abs(value) < kStickDeadzone)
        {
            return 0.0f;
        }
        const float sign = value > 0.0f ? 1.0f : -1.0f;
        return sign * (std::abs(value) - kStickDeadzone) / (1.0f - kStickDeadzone);
    }

    /// Gameplay collision is a circle test on the flight plane, nothing more. A broadphase
    /// would be premature here: the worst case is 64 player shots against 16 live enemies,
    /// about a thousand distance checks per fixed step, which does not register next to the
    /// scene-graph work that follows it.
    bool OverlapsXZ(const glm::vec3& a, float radiusA, const glm::vec3& b, float radiusB)
    {
        const float dx = a.x - b.x;
        const float dz = a.z - b.z;
        const float radius = radiusA + radiusB;
        return dx * dx + dz * dz <= radius * radius;
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                         Runtime::Config::Options& options, NextEngine* engine)
{
    Modules::Scad::Register();                 // .scad scene + rig loader
    NextGameplay::Rig::Install(*engine);       // ScadRig pools, for the bosses
    return std::make_unique<NextRaidenGameInstance>(config, options, engine);
}

NextRaidenGameInstance::NextRaidenGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                               NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "NextRaiden", 1600, 900, false);
}

const FStageDesc& NextRaidenGameInstance::CurrentStage() const
{
    const size_t cursor = std::min(stageCursor_, config_.Stages.size() - 1);
    return config_.Stages[cursor];
}

void NextRaidenGameInstance::OnInit()
{
    config_.Load(kGameplayConfig, kStagesConfig);
    camera_.Configure(config_.Screen, config_.Playfield.FlightAltitude);
    player_.Configure(config_.Player, config_.Playfield, config_.Weapon, config_.Survival);
    flow_.Configure(config_.Flow);
    for (size_t k = 0; k < config_.Enemies.size(); ++k)
    {
        enemies_.SetStats(static_cast<EEnemyKind>(k), config_.Enemies[k]);
    }
    for (size_t k = 0; k < config_.Grounds.size(); ++k)
    {
        grounds_.Configure(static_cast<EGroundKind>(k), config_.Grounds[k]);
    }
    debrisBudget_ = config_.Debris.Budget;
    autoFire_ = config_.Weapon.AutoFire;

    std::string scene = CurrentStage().Scene;
    if (!GOption->SceneName.empty())
    {
        scene = GOption->SceneName;
        for (size_t i = 0; i < config_.Stages.size(); ++i)
        {
            if (config_.Stages[i].Scene == scene)
            {
                stageCursor_ = i;
                break;
            }
        }
    }
    SPDLOG_INFO("[NextRaiden] loading stage '{}'", scene);
    GetEngine().RequestLoadScene({.filename = scene});
}

void NextRaidenGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    // Flat-shaded low-poly seen from 47 m up: the rasteriser with software GI is the cheapest
    // path that still gives the kit its shadows and bounce.
    cvars.SetDefaultFromString("r.rendererType", "0", &error);
    cvars.SetDefaultFromString("r.upscaler.type", "2", &error);
    // The statistics overlay lives exactly where the right HUD column goes, and this is a
    // game rather than a renderer sample: off unless someone asks for it.
    cvars.SetDefaultFromString("ui.showOverlay", "false", &error);

    cvars.RegisterFloat("nr.scrollSpeed", 0.0f, &scrollSpeedOverride_, NextCVar::ECVarFlags::None,
                        "Override the stage scroll speed in m/s (0 = use the stage's own)", nullptr, 0.0, 60.0);
    cvars.RegisterBool("nr.freeze", false, &freezeScroll_, NextCVar::ECVarFlags::None,
                       "Stop the stage scrolling, for looking at one spot");
    cvars.RegisterBool("nr.invincible", false, &invincible_, NextCVar::ECVarFlags::None,
                       "The player cannot be hit", [this]() { player_.SetGodMode(invincible_); });
    cvars.RegisterBool("nr.showHitboxes", false, &showHitboxes_, NextCVar::ECVarFlags::None,
                       "Draw the gameplay collision circles over the HUD");
    cvars.RegisterBool("nr.autoFire", true, &autoFire_, NextCVar::ECVarFlags::None,
                       "Fire without holding the trigger (C toggles it in game)");
    cvars.RegisterInt("nr.debrisBudget", 64, &debrisBudget_, NextCVar::ECVarFlags::None,
                      "Live physics debris pieces (0 disables them)", [this]() { debris_.SetBudget(debrisBudget_); }, 0,
                      256);
    cvars.RegisterInt("nr.power", 0, &forcedPower_, NextCVar::ECVarFlags::None,
                      "Force the vulcan power level 1-4 (0 = leave it alone)",
                      [this]()
                      {
                          if (forcedPower_ > 0)
                          {
                              player_.SetPower(forcedPower_);
                          }
                      },
                      0, 4);
    // Skipping straight to the fight is the difference between a two-minute boss iteration
    // and a five-second one.
    cvars.RegisterBool("nr.spawnBoss", false, &spawnBossRequest_, NextCVar::ECVarFlags::None,
                       "Jump straight to the next boss of this stage",
                       [this]()
                       {
                           if (spawnBossRequest_ && sceneReady_)
                           {
                               const FStageDesc& stage = CurrentStage();
                               if (bossCursor_ < stage.Bosses.size())
                               {
                                   stageTime_ = std::max(stageTime_, stage.Bosses[bossCursor_].TriggerTime);
                                   // Skipped, not released: letting the clock jump would dump
                                   // every wave up to the boss into one step.
                                   waves_.SkipTo(stageTime_);
                               }
                           }
                           spawnBossRequest_ = false;
                       });
    // Phases are picked by remaining health, so this is "jump to phase N" - the same argument
    // that justified nr.spawnBoss, one level down. 0 ends the fight.
    cvars.RegisterFloat("nr.bossHp", -1.0f, &bossHpRequest_, NextCVar::ECVarFlags::None,
                        "Set the live boss's remaining health as a fraction of its maximum (0 ends it)",
                        [this]()
                        {
                            if (bossHpRequest_ >= 0.0f && sceneReady_)
                            {
                                bosses_.SetHitPointsFraction(bossHpRequest_);
                            }
                        },
                        -1.0, 1.0);
    cvars.RegisterString("nr.stage", "", &stageRequest_, NextCVar::ECVarFlags::None,
                         "Load a stage by id or 1-based index, restarting the run",
                         [this]()
                         {
                             if (stageRequest_.empty())
                             {
                                 return;
                             }
                             size_t target = config_.Stages.size();
                             for (size_t i = 0; i < config_.Stages.size(); ++i)
                             {
                                 if (config_.Stages[i].Id == stageRequest_)
                                 {
                                     target = i;
                                     break;
                                 }
                             }
                             if (target == config_.Stages.size())
                             {
                                 const int index = std::atoi(stageRequest_.c_str());
                                 if (index >= 1 && static_cast<size_t>(index) <= config_.Stages.size())
                                 {
                                     target = static_cast<size_t>(index - 1);
                                 }
                             }
                             if (target < config_.Stages.size())
                             {
                                 LoadStage(target, true);
                             }
                             else
                             {
                                 SPDLOG_WARN("[NextRaiden] nr.stage: no stage '{}'", stageRequest_);
                             }
                             stageRequest_.clear();
                         });
}

void NextRaidenGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                                std::vector<Assets::Model>& models,
                                                std::vector<Assets::FMaterial>& materials,
                                                std::vector<Assets::LightObject>& /*lights*/,
                                                std::vector<Assets::AnimationTrack>& /*tracks*/)
{
    // Dropping the old world's handles belongs *here*, not in OnSceneUnloaded: the engine
    // calls that hook after this one, so anything this function builds would be thrown away
    // again before the scene ever went live. (The same ordering is why NextRig pools are
    // declared from inside this hook.)
    player_.Unbind();
    playerPool_.clear();
    optionPool_.clear();
    bullets_.Clear();
    enemies_.Clear();
    items_.Clear();
    debris_.Unbind();
    grounds_.Unbind();
    bosses_.Unbind();

    // Nodes are not addressable yet - no world transforms, no scene. Indexing the rack,
    // cloning pools into this same array and clearing visibility flags is all that is safe
    // here, and it is also everything the pools need (design section 5.3).
    rack_.Index(nodes);
    playerPool_ = rack_.Clone(Prefabs::kPlayer, 1, nodes, FPrefabRack::ParkPosition(0));
    optionPool_ = rack_.Clone(Prefabs::kWingman, kMaxOptions, nodes, FPrefabRack::ParkPosition(50));
    enemies_.Create(rack_, nodes, kEnemiesPerKind);
    bullets_.Create(rack_, nodes, config_.Bullets);
    items_.Create(rack_, nodes, config_.Items);
    debris_.Create(rack_, nodes, config_.Debris);
    bosses_.Declare(GetEngine().GetRig(), CurrentStage().Bosses, models, materials);
}

void NextRaidenGameInstance::OnSceneLoaded()
{
    const FStageDesc& stage = CurrentStage();
    Assets::Scene& scene = GetEngine().GetScene();

    camera_.Reset(stage.StartZ);
    random_.Reseed(stage.Seed);

    std::vector<std::string> warnings;
    index_ = FStageIndex::Build(scene, &warnings);
    for (const std::string& warning : warnings)
    {
        SPDLOG_WARN("[NextRaiden] stage: {}", warning);
    }
    grounds_.Bind(&index_);

    if (!playerPool_.empty())
    {
        player_.Bind(playerPool_.front());
    }
    else
    {
        SPDLOG_WARN("[NextRaiden] stage '{}' has no {} in its rack; flying blind", stage.Id, Prefabs::kPlayer);
    }
    player_.BindOptions(optionPool_);
    player_.Reset(camera_.CentreZ());
    player_.SetGodMode(invincible_);

    debris_.Bind(scene, GetEngine().GetPhysicsEngine());
    debris_.SetBudget(debrisBudget_);
    bosses_.Bind(GetEngine().GetRig(), &scene);
    items_.Reset();
    flow_.Reset();

    std::vector<std::string> waveErrors;
    if (!stage.WaveFile.empty() && !waves_.Load(stage.WaveFile, waveErrors))
    {
        SPDLOG_WARN("[NextRaiden] stage '{}' has no usable wave table; it will be empty", stage.Id);
    }
    for (const std::string& error : waveErrors)
    {
        SPDLOG_ERROR("[NextRaiden] {}: {}", stage.WaveFile, error);
    }
    waves_.Reset();

    enemiesKilled_ = 0;
    medalStreak_ = 0;
    bulletsPeak_ = 0;
    bossCursor_ = 0;
    stageEnded_ = false;
    stageAdvanceIssued_ = false;
    stageTime_ = 0.0f;
    fixedAccumulator_ = 0.0f;
    deaths_.clear();
    groundDeaths_.clear();
    pickups_.clear();
    sceneReady_ = true;
    scene.MarkTransformDirty();
    SPDLOG_INFO("[NextRaiden] stage ready: '{}' ({} prefabs, {} waves, {} ground targets, {} bosses, {} rows)",
                stage.Id, rack_.PrefabCount(), waves_.EventCount(), index_.Targets.size(), stage.Bosses.size(),
                index_.SceneryRows);
}

void NextRaidenGameInstance::OnSceneUnloaded()
{
    // Only "the live scene is gone" belongs here. *Every* pool teardown - debris included -
    // is in BeforeSceneRebuild, because the engine calls that hook before this one. Putting
    // debris_.Unbind() here cost an hour: the pool was built, then silently emptied again
    // before the scene went live, and the only symptom was that nothing ever exploded.
    sceneReady_ = false;
}

void NextRaidenGameInstance::LoadStage(size_t index, bool restartRun)
{
    if (config_.Stages.empty())
    {
        return;
    }
    stageCursor_ = std::min(index, config_.Stages.size() - 1);
    if (restartRun)
    {
        score_ = 0;
        nextExtend_ = config_.Score.ExtendEvery;
    }
    SPDLOG_INFO("[NextRaiden] loading stage {} '{}'", stageCursor_ + 1, CurrentStage().Id);
    GetEngine().RequestLoadScene({.filename = CurrentStage().Scene});
}

void NextRaidenGameInstance::AdvanceStage()
{
    if (stageCursor_ + 1 < config_.Stages.size())
    {
        LoadStage(stageCursor_ + 1, false);
    }
    else
    {
        flow_.NotifyAllClear();
    }
}

FPlayerInput NextRaidenGameInstance::CollectInput()
{
    FPlayerInput input;
    // Screen up is -Z, so "forward" on the stick has to come out negative here.
    input.Move.x = (keyMove_[3] ? 1.0f : 0.0f) - (keyMove_[2] ? 1.0f : 0.0f) + stickMove_.x;
    input.Move.y = (keyMove_[1] ? 1.0f : 0.0f) - (keyMove_[0] ? 1.0f : 0.0f) + stickMove_.y;
    input.Slow = keySlow_;
    input.Fire = autoFire_ || keyFire_ || padFire_;
    input.BombPressed = bombPressed_;
    bombPressed_ = false; // edge-triggered: consumed by the first fixed step that sees it
    return input;
}

void NextRaidenGameInstance::AddScore(int amount)
{
    score_ += amount;
    while (config_.Score.ExtendEvery > 0 && score_ >= nextExtend_)
    {
        player_.AddLife(1);
        nextExtend_ += config_.Score.ExtendEvery;
    }
}

void NextRaidenGameInstance::FireBomb()
{
    if (!player_.ConsumeBomb())
    {
        return;
    }
    bullets_.ClearHostile();
    enemies_.DamageAll(config_.Survival.BombDamage, deaths_);
    // The bomb hits everything on screen, ground and boss included: a screen-clearing panic
    // button that leaves the thing actually shooting at you untouched is not a panic button.
    for (size_t target : grounds_.ActiveTargets())
    {
        grounds_.ApplyDamage(target, config_.Survival.BombDamage, debris_, random_, groundDeaths_);
    }
    // The boss takes it on the hull, not on the core: a bomb covers the whole screen, so
    // there is no aim in it to reward.
    if (bosses_.IsVulnerable() && bosses_.ApplyAreaDamage(config_.Survival.BombDamage, debris_, random_))
    {
        // Handled next step by the usual defeat path.
    }
    Audio::PlayExplosion(GetEngine());
}

void NextRaidenGameInstance::ResolveCollisions()
{
    std::vector<FBullet>& shots = bullets_.Bullets();
    std::vector<FEnemy>& live = enemies_.Enemies();

    for (FBullet& shot : shots)
    {
        if (!shot.Active)
        {
            continue;
        }

        if (shot.Hostile)
        {
            if (player_.IsVulnerable() &&
                OverlapsXZ(shot.Position, shot.Radius, player_.Position(), config_.Player.HitRadius))
            {
                bullets_.Kill(shot);
                if (player_.TakeHit())
                {
                    debris_.Spawn(player_.Position(), glm::vec3(0.0f), 4, random_);
                    Audio::PlayPlayerHit(GetEngine());
                }
                else
                {
                    Audio::PlayEnemyHit(GetEngine());
                }
            }
            continue;
        }

        bool consumed = false;
        for (FEnemy& enemy : live)
        {
            if (!enemy.Active || !OverlapsXZ(shot.Position, shot.Radius, enemy.Position, enemy.Radius))
            {
                continue;
            }
            bullets_.Kill(shot);
            if (!enemies_.ApplyDamage(enemy, shot.Damage, deaths_))
            {
                Audio::PlayEnemyHit(GetEngine());
            }
            consumed = true;
            break;
        }
        if (consumed)
        {
            continue;
        }

        if (bosses_.IsVulnerable() &&
            OverlapsXZ(shot.Position, shot.Radius, bosses_.Position(), bosses_.Radius()))
        {
            bullets_.Kill(shot);
            if (!bosses_.ApplyDamage(shot.Damage, shot.Position, debris_, random_))
            {
                Audio::PlayEnemyHit(GetEngine());
            }
            continue;
        }

        // Ground last: an air target in front of a tank should eat the shot first, which is
        // also the order the player sees it.
        for (size_t targetIndex : grounds_.ActiveTargets())
        {
            const FStageTarget& target = grounds_.Target(targetIndex);
            const float radius = grounds_.Stats(target.Kind).Radius;
            if (!OverlapsXZ(shot.Position, shot.Radius, target.Position, radius))
            {
                continue;
            }
            bullets_.Kill(shot);
            if (!grounds_.ApplyDamage(targetIndex, shot.Damage, debris_, random_, groundDeaths_))
            {
                Audio::PlayEnemyHit(GetEngine());
            }
            break;
        }
    }

    // Ramming. Checked after the bullets so a shot that would have killed the enemy this step
    // already has, which is the reading a player expects when they fly into something they
    // were shooting.
    //
    // The collision kills the enemy whether or not the player is currently vulnerable: a 5.5 m
    // airframe hitting a 3 m one is fatal for the smaller aircraft either way, and gating it
    // on the player's own state let an enemy park inside the player and sit there during
    // invulnerability - which is exactly how it looked on screen with nr.invincible on.
    if (player_.State() == EPlayerState::Flying)
    {
        for (FEnemy& enemy : live)
        {
            if (!enemy.Active ||
                !OverlapsXZ(player_.Position(), config_.Player.HitRadius, enemy.Position, enemy.Radius))
            {
                continue;
            }
            const bool playerWasVulnerable = player_.IsVulnerable();
            enemies_.ApplyDamage(enemy, 1000.0f, deaths_);
            if (playerWasVulnerable && player_.TakeHit())
            {
                debris_.Spawn(player_.Position(), glm::vec3(0.0f), 4, random_);
                Audio::PlayPlayerHit(GetEngine());
            }
            break;
        }

        // Flying into a boss is simply fatal; it does not lose health for it.
        if (player_.IsVulnerable() && bosses_.IsActive() &&
            OverlapsXZ(player_.Position(), config_.Player.HitRadius, bosses_.Position(), bosses_.Radius()) &&
            player_.TakeHit())
        {
            debris_.Spawn(player_.Position(), glm::vec3(0.0f), 4, random_);
            Audio::PlayPlayerHit(GetEngine());
        }
    }
}

void NextRaidenGameInstance::HandleDeaths()
{
    for (const FEnemyDeath& death : deaths_)
    {
        AddScore(death.Score);
        ++enemiesKilled_;
        // The debris inherits a fraction of the victim's momentum rather than all of it:
        // at full speed the pieces track the wreck so exactly that the explosion reads as a
        // texture swap instead of a break-up.
        debris_.Spawn(death.Position, death.Velocity * 0.35f, death.DebrisPieces, random_);
        if (death.Drop != EItemKind::None)
        {
            items_.Spawn(death.Drop, death.Position);
        }
        Audio::PlayExplosion(GetEngine());
    }
    deaths_.clear();

    for (const FGroundDeath& death : groundDeaths_)
    {
        AddScore(death.Score);
        if (death.Drop != EItemKind::None)
        {
            items_.Spawn(death.Drop, death.Position + glm::vec3(0.0f, config_.Playfield.FlightAltitude, 0.0f));
        }
        Audio::PlayExplosion(GetEngine());
    }
    groundDeaths_.clear();
}

void NextRaidenGameInstance::HandlePickups()
{
    for (EItemKind kind : pickups_)
    {
        switch (kind)
        {
        case EItemKind::Power:
            if (player_.Power() >= 4)
            {
                AddScore(config_.Items.PowerFullScore);
            }
            else
            {
                player_.AddPower(1);
            }
            break;
        case EItemKind::Option:
            if (!player_.AddOption())
            {
                AddScore(config_.Items.OptionFullScore);
            }
            break;
        case EItemKind::Bomb:
            player_.AddBomb(1);
            break;
        case EItemKind::Repair:
            player_.AddShield(1);
            break;
        case EItemKind::Medal:
        {
            // Worth more the longer the chain runs. The streak is what turns a medal from
            // loose change into a reason to fly somewhere dangerous.
            const int index = std::min(medalStreak_, static_cast<int>(config_.Items.MedalValues.size()) - 1);
            AddScore(config_.Items.MedalValues[static_cast<size_t>(index)]);
            ++medalStreak_;
            break;
        }
        default:
            break;
        }
        Audio::PlayPickup(GetEngine());
    }
    pickups_.clear();
}

void NextRaidenGameInstance::UpdateBossTrigger()
{
    const FStageDesc& stage = CurrentStage();

    if (bosses_.IsActive())
    {
        if (bosses_.HitPoints() > 0.0f)
        {
            return;
        }
        // Defeated: score it, then either hand the stage back or end it.
        const FBossDesc* desc = bosses_.Current();
        const bool endsStage = desc && desc->EndsStage;
        if (desc)
        {
            AddScore(desc->Score);
            ++enemiesKilled_;
            if (desc->Drop != EItemKind::None)
            {
                items_.Spawn(desc->Drop, bosses_.Position());
            }
        }
        bosses_.Despawn();
        bullets_.ClearHostile();
        ++bossCursor_;
        if (endsStage)
        {
            flow_.NotifyBossDefeated();
        }
        else
        {
            flow_.NotifyMidBossDefeated();
        }
        Audio::PlayExplosion(GetEngine());
        return;
    }

    if (flow_.State() != EStageState::Playing || bossCursor_ >= stage.Bosses.size())
    {
        return;
    }
    if (stageTime_ < stage.Bosses[bossCursor_].TriggerTime)
    {
        return;
    }
    if (!bosses_.Begin(bossCursor_, camera_))
    {
        // No rig, no fight. Skipping is better than stalling the stage on an encounter that
        // cannot happen - a missing asset should cost the boss, not the run.
        SPDLOG_WARN("[NextRaiden] boss {} of stage '{}' unavailable; skipping it", bossCursor_, stage.Id);
        ++bossCursor_;
        return;
    }
    flow_.NotifyBossTriggered();
}

void NextRaidenGameInstance::FixedStep(float deltaSeconds)
{
    const FStageDesc& stage = CurrentStage();

    flow_.Update(deltaSeconds);

    const float speed = scrollSpeedOverride_ > 0.0f ? scrollSpeedOverride_ : stage.ScrollSpeed;
    if (!freezeScroll_ && camera_.CentreZ() > stage.EndZ)
    {
        camera_.Advance(speed * flow_.ScrollScale(), deltaSeconds);
    }

    FPlayerInput input = CollectInput();
    // Auto-fire holds the trigger down forever, including through Game Over and All Clear.
    // The ship keeps flying on those screens - a frozen sprite reads as a crash - but it has
    // nothing left to shoot at.
    input.Fire = input.Fire && !flow_.RunEnded();
    if (input.BombPressed)
    {
        if (!flow_.RequestAdvance())
        {
            FireBomb();
        }
    }

    const bool wasFlying = player_.State() == EPlayerState::Flying;
    player_.Step(input, camera_.CentreZ(), deltaSeconds, bullets_);
    if (wasFlying && input.Fire && player_.State() == EPlayerState::Flying)
    {
        Audio::PlayShot(GetEngine());
    }

    if (flow_.CombatRunning())
    {
        if (flow_.StageClockRunning())
        {
            waves_.Update(stageTime_, camera_, enemies_);
        }
        enemies_.Step(deltaSeconds, camera_, player_.Position(), player_.Velocity(), bullets_, deaths_);
        grounds_.Step(deltaSeconds, camera_, player_.Position(), player_.Velocity(), bullets_, debris_, random_,
                      groundDeaths_);
        bosses_.Step(deltaSeconds, camera_, player_.Position(), player_.Velocity(), bullets_, debris_, random_);
    }

    bullets_.Step(deltaSeconds, camera_);
    bulletsPeak_ = std::max(bulletsPeak_, bullets_.AliveCount());
    items_.Step(deltaSeconds, camera_, player_.Position(), pickups_);

    ResolveCollisions();
    HandleDeaths();
    HandlePickups();
    UpdateBossTrigger();

    // Debris only needs its recycling checked; the bodies themselves are simulated by the
    // engine on its own tick (design section 7.4).
    debris_.Step(deltaSeconds, camera_.GroundNearZ());

    if (player_.State() == EPlayerState::Dead && player_.RespawnCountdown() <= 0.0f)
    {
        player_.Respawn(camera_.CentreZ());
        flow_.NotifyPlayerRespawned();
    }
    else if (player_.State() == EPlayerState::GameOver)
    {
        flow_.NotifyGameOver();
    }
    else if (player_.State() == EPlayerState::Dead)
    {
        flow_.NotifyPlayerDied();
    }

    // Out of waves, out of bosses, out of stage: the run moves on.
    if (!stageEnded_ && flow_.State() == EStageState::Playing && waves_.Exhausted() &&
        bossCursor_ >= stage.Bosses.size() && camera_.CentreZ() <= stage.EndZ)
    {
        stageEnded_ = true;
        flow_.NotifyStageEndReached();
    }
    if (flow_.WantsNextStage() && !stageAdvanceIssued_)
    {
        stageAdvanceIssued_ = true;
        AdvanceStage();
    }

    if (flow_.StageClockRunning())
    {
        stageTime_ += deltaSeconds;
    }
}

void NextRaidenGameInstance::OnTick(double deltaSeconds)
{
    if (!sceneReady_)
    {
        return;
    }

    fixedAccumulator_ += std::min(static_cast<float>(deltaSeconds), kMaxFrameSeconds);
    while (fixedAccumulator_ >= kFixedStep)
    {
        fixedAccumulator_ -= kFixedStep;
        FixedStep(kFixedStep);
    }

    // One scene-graph write pass and one dirty flag per frame, not per fixed step.
    player_.Apply();
    enemies_.Apply();
    bullets_.Apply();
    items_.Apply();
    GetEngine().GetScene().MarkTransformDirty();
}

bool NextRaidenGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    if (!sceneReady_)
    {
        return false;
    }
    camera_.Fill(outRenderCamera);
    return true;
}

bool NextRaidenGameInstance::OnRenderUI()
{
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList)
    {
        return false;
    }
    const ImVec2 screen = ImGui::GetIO().DisplaySize;

    // The window is landscape and the battlefield is portrait: these opaque columns are what
    // turn one into the other. P4 fills them with the real readouts; this is the minimum that
    // makes the state legible while tuning.
    const float column = screen.x * std::clamp(config_.Screen.HudColumnFraction, 0.0f, 0.45f);
    constexpr ImU32 kPanel = IM_COL32(16, 20, 26, 255);
    constexpr ImU32 kEdge = IM_COL32(92, 120, 140, 255);
    constexpr ImU32 kInk = IM_COL32(236, 240, 245, 240);
    constexpr ImU32 kMuted = IM_COL32(150, 170, 190, 235);
    constexpr ImU32 kWarn = IM_COL32(236, 148, 96, 245);
    drawList->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(column, screen.y), kPanel);
    drawList->AddRectFilled(ImVec2(screen.x - column, 0.0f), ImVec2(screen.x, screen.y), kPanel);
    drawList->AddLine(ImVec2(column, 0.0f), ImVec2(column, screen.y), kEdge, 2.0f);
    drawList->AddLine(ImVec2(screen.x - column, 0.0f), ImVec2(screen.x - column, screen.y), kEdge, 2.0f);

    float y = 28.0f;
    drawList->AddText(ImVec2(24.0f, y), kInk, fmt::format("STAGE  {}", CurrentStage().DisplayName).c_str());
    y += 24.0f;
    drawList->AddText(ImVec2(24.0f, y), kMuted,
                      fmt::format("{:05.1f}s  {}", stageTime_, StageStateName(flow_.State())).c_str());
    y += 32.0f;
    drawList->AddText(ImVec2(24.0f, y), kInk, fmt::format("SCORE  {}", score_).c_str());
    y += 24.0f;
    drawList->AddText(ImVec2(24.0f, y), kMuted, fmt::format("KILLS  {}", enemiesKilled_).c_str());
    y += 24.0f;
    drawList->AddText(ImVec2(24.0f, y), kMuted, fmt::format("LIVES  {}", player_.Lives()).c_str());
    y += 24.0f;
    drawList->AddText(ImVec2(24.0f, y), player_.Shield() > 1 ? kMuted : kWarn,
                      fmt::format("SHIELD {}", player_.Shield()).c_str());

    const float rightX = screen.x - column + 24.0f;
    y = 28.0f;
    drawList->AddText(ImVec2(rightX, y), kInk, fmt::format("POWER  {}", player_.Power()).c_str());
    y += 24.0f;
    drawList->AddText(ImVec2(rightX, y), kInk, fmt::format("OPTION {}", player_.Options()).c_str());
    y += 24.0f;
    drawList->AddText(ImVec2(rightX, y), kInk, fmt::format("BOMB   {}", player_.Bombs()).c_str());
    y += 24.0f;
    drawList->AddText(ImVec2(rightX, y), autoFire_ ? kInk : kMuted,
                      autoFire_ ? "FIRE   AUTO" : "FIRE   HOLD");
    y += 32.0f;
    drawList->AddText(ImVec2(rightX, y), kMuted, fmt::format("ENEMY  {}", enemies_.AliveCount()).c_str());
    y += 24.0f;
    drawList->AddText(ImVec2(rightX, y), kMuted, fmt::format("GROUND {}", grounds_.AliveCount()).c_str());
    y += 24.0f;
    drawList->AddText(ImVec2(rightX, y), kMuted, fmt::format("SHOTS  {}", bullets_.AliveCount()).c_str());
    y += 24.0f;
    drawList->AddText(ImVec2(rightX, y), kMuted, fmt::format("ITEMS  {}", items_.AliveCount()).c_str());
    y += 24.0f;
    drawList->AddText(ImVec2(rightX, y), kMuted, fmt::format("DEBRIS {}", debris_.AliveCount()).c_str());

    if (bosses_.IsActive())
    {
        // Across the top of the play column, where the thing it belongs to is.
        const float barX = column + 20.0f;
        const float barW = screen.x - 2.0f * column - 40.0f;
        const float fraction = std::clamp(bosses_.HitPointsFraction(), 0.0f, 1.0f);
        drawList->AddRectFilled(ImVec2(barX, 18.0f), ImVec2(barX + barW, 32.0f), IM_COL32(30, 14, 16, 220));
        drawList->AddRectFilled(ImVec2(barX, 18.0f), ImVec2(barX + barW * fraction, 32.0f),
                                IM_COL32(214, 72, 66, 245));
        drawList->AddRect(ImVec2(barX, 18.0f), ImVec2(barX + barW, 32.0f), kEdge);
        if (bosses_.WeakPointExposed())
        {
            // The doors swinging open is the real tell, but it happens 70 m away at a 15 degree
            // pitch. A word under the bar is what turns "the boss looks different" into
            // "shoot the middle".
            drawList->AddText(ImVec2(barX, 38.0f), kWarn, "CORE EXPOSED");
        }
    }

    const char* banner = nullptr;
    switch (flow_.State())
    {
    case EStageState::Ready: banner = "READY"; break;
    case EStageState::StageClear: banner = "STAGE CLEAR"; break;
    case EStageState::GameOver: banner = "GAME OVER"; break;
    case EStageState::AllClear: banner = "ALL CLEAR"; break;
    default: break;
    }
    if (banner)
    {
        drawList->AddText(ImVec2(screen.x * 0.5f - 44.0f, screen.y * 0.42f), kWarn, banner);
    }
    return false;
}

bool NextRaidenGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
    {
        return false;
    }
    const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
    switch (event.key.key)
    {
    case SDLK_W: case SDLK_UP: keyMove_[0] = pressed; return true;
    case SDLK_S: case SDLK_DOWN: keyMove_[1] = pressed; return true;
    case SDLK_A: case SDLK_LEFT: keyMove_[2] = pressed; return true;
    case SDLK_D: case SDLK_RIGHT: keyMove_[3] = pressed; return true;
    case SDLK_LSHIFT: case SDLK_RSHIFT: keySlow_ = pressed; return true;
    case SDLK_Z: case SDLK_J: case SDLK_SPACE: keyFire_ = pressed; return true;
    case SDLK_C:
        if (pressed && !event.key.repeat)
        {
            autoFire_ = !autoFire_;
        }
        return true;
    case SDLK_X: case SDLK_K:
        if (pressed && !event.key.repeat)
        {
            bombPressed_ = true;
        }
        return true;
    default: return false;
    }
}

bool NextRaidenGameInstance::OnGamepadInput(int16_t leftStickX, int16_t leftStickY, int16_t /*rightStickX*/,
                                            int16_t /*rightStickY*/, int16_t /*leftTrigger*/, int16_t rightTrigger)
{
    stickMove_.x = StickAxis(leftStickX);
    // SDL's stick Y is positive downwards, which is also "towards the bottom of the screen"
    // here, so it maps straight through.
    stickMove_.y = StickAxis(leftStickY);
    padFire_ = rightTrigger > 8000;
    return true;
}

void NextRaidenGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg)
{
    using Value = Runtime::Agent::FAgentQueryValue;
    reg.Add("state", [this]() -> Value
            { return std::string(sceneReady_ ? StageStateName(flow_.State()) : std::string_view("loading")); });
    reg.Add("stage", [this]() -> Value { return CurrentStage().Id; });
    reg.Add("stageIndex", [this]() -> Value { return static_cast<int64_t>(stageCursor_ + 1); });
    reg.Add("stageCount", [this]() -> Value { return static_cast<int64_t>(config_.Stages.size()); });
    reg.Add("stageTime", [this]() -> Value { return static_cast<double>(stageTime_); });
    reg.Add("cameraZ", [this]() -> Value { return static_cast<double>(camera_.CentreZ()); });
    reg.Add("groundNearZ", [this]() -> Value { return static_cast<double>(camera_.GroundNearZ()); });
    reg.Add("groundFarZ", [this]() -> Value { return static_cast<double>(camera_.GroundFarZ()); });
    // Relative to the camera centre: these are the numbers that express "the visible band
    // straddles the centre", and unlike the absolute Z they do not move when the world scale
    // or the Ready countdown changes.
    reg.Add("groundNearLead",
            [this]() -> Value { return static_cast<double>(camera_.GroundNearZ() - camera_.CentreZ()); });
    reg.Add("groundFarLead",
            [this]() -> Value { return static_cast<double>(camera_.GroundFarZ() - camera_.CentreZ()); });
    reg.Add("playerX", [this]() -> Value { return static_cast<double>(player_.Position().x); });
    reg.Add("playerZ", [this]() -> Value { return static_cast<double>(player_.Position().z); });
    reg.Add("playerLeadZ",
            [this]() -> Value { return static_cast<double>(player_.Position().z - camera_.CentreZ()); });
    reg.Add("prefabs", [this]() -> Value { return static_cast<int64_t>(rack_.PrefabCount()); });

    reg.Add("score", [this]() -> Value { return static_cast<int64_t>(score_); });
    reg.Add("kills", [this]() -> Value { return static_cast<int64_t>(enemiesKilled_); });
    reg.Add("lives", [this]() -> Value { return static_cast<int64_t>(player_.Lives()); });
    reg.Add("shield", [this]() -> Value { return static_cast<int64_t>(player_.Shield()); });
    reg.Add("bombs", [this]() -> Value { return static_cast<int64_t>(player_.Bombs()); });
    reg.Add("power", [this]() -> Value { return static_cast<int64_t>(player_.Power()); });
    reg.Add("options", [this]() -> Value { return static_cast<int64_t>(player_.Options()); });
    reg.Add("autoFire", [this]() -> Value { return autoFire_; });
    reg.Add("enemiesAlive", [this]() -> Value { return static_cast<int64_t>(enemies_.AliveCount()); });
    reg.Add("bulletsAlive", [this]() -> Value { return static_cast<int64_t>(bullets_.AliveCount()); });
    reg.Add("bulletsPeak", [this]() -> Value { return static_cast<int64_t>(bulletsPeak_); });
    reg.Add("hostileBullets", [this]() -> Value { return static_cast<int64_t>(bullets_.HostileCount()); });
    reg.Add("debrisAlive", [this]() -> Value { return static_cast<int64_t>(debris_.AliveCount()); });
    reg.Add("itemsAlive", [this]() -> Value { return static_cast<int64_t>(items_.AliveCount()); });
    reg.Add("wavesFired", [this]() -> Value { return static_cast<int64_t>(waves_.Cursor()); });
    reg.Add("wavesTotal", [this]() -> Value { return static_cast<int64_t>(waves_.EventCount()); });

    reg.Add("groundTotal", [this]() -> Value { return static_cast<int64_t>(index_.Targets.size()); });
    reg.Add("groundDestroyed", [this]() -> Value { return static_cast<int64_t>(grounds_.DestroyedCount()); });
    reg.Add("sceneryRows", [this]() -> Value { return static_cast<int64_t>(index_.SceneryRows); });
    reg.Add("bossActive", [this]() -> Value { return bosses_.IsActive(); });
    reg.Add("bossHp", [this]() -> Value { return static_cast<double>(bosses_.HitPoints()); });
    reg.Add("bossHpFraction", [this]() -> Value { return static_cast<double>(bosses_.HitPointsFraction()); });
    reg.Add("bossPhase", [this]() -> Value { return static_cast<int64_t>(bosses_.PhaseIndex()); });
    reg.Add("bossWeakPoint", [this]() -> Value { return bosses_.WeakPointExposed(); });
    /// False during the arrival slide, when the boss cannot be hurt yet - which is what a
    /// script has to wait for before it asserts anything about the fight.
    reg.Add("bossVulnerable", [this]() -> Value { return bosses_.IsVulnerable(); });
    reg.Add("bossesFought", [this]() -> Value { return static_cast<int64_t>(bossCursor_); });
    reg.Add("bossesTotal", [this]() -> Value { return static_cast<int64_t>(CurrentStage().Bosses.size()); });
}
