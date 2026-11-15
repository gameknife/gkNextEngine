#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"

// ============================================================================
// Agent validation hooks: cvars that drive the game into a state worth looking
// at (`gnb validate --script assets/agentscripts/brotato3d-*.agentscript.json`)
// and `game.*` queries to assert on. None of this is reachable from the menus.
// ============================================================================

using namespace Brotato3DUtil;

void Brotato3DGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    using NextCVar::ECVarFlags;
    // Triggers: set to true to fire once; they reset themselves.
    cvars.RegisterBool("brotato3d.validation.start", false, &validation_.start, ECVarFlags::None,
                       "Agent-only: start a run with the selected character", [this]()
                       {
                           if (std::exchange(validation_.start, false) && sceneReady_)
                           {
                               StartNewRun();
                           }
                       });
    cvars.RegisterBool("brotato3d.validation.lineup", false, &validation_.lineup, ECVarFlags::None,
                       "Agent-only: spawn one zombie of every type in an arc in front of the player", [this]()
                       {
                           if (!std::exchange(validation_.lineup, false) || appState_ != Brotato3D::EAppState::Playing)
                           {
                               return;
                           }
                           const float count = static_cast<float>(std::max<size_t>(1, enemyDefs_.size()));
                           float index = 0.0f;
                           for (const auto& [enemyId, def] : enemyDefs_)
                           {
                               const float x = (index - (count - 1.0f) * 0.5f) * 1.9f;
                               SpawnEnemy(enemyId, player_.worldPos + glm::vec3(x, 0.0f, -5.0f - def.rigScale));
                               index += 1.0f;
                           }
                       });
    cvars.RegisterBool("brotato3d.validation.horde", false, &validation_.horde, ECVarFlags::None,
                       "Agent-only: spawn a ring of walkers around the player", [this]()
                       {
                           if (!std::exchange(validation_.horde, false) || appState_ != Brotato3D::EAppState::Playing)
                           {
                               return;
                           }
                           for (int index = 0; index < 36; ++index)
                           {
                               const float angle = static_cast<float>(index) / 36.0f * glm::two_pi<float>();
                               const float distance = 6.0f + static_cast<float>(index % 4) * 1.6f;
                               SpawnEnemy(index % 9 == 0 ? "charger" : "rat",
                                          player_.worldPos + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * distance);
                           }
                       });
    cvars.RegisterBool("brotato3d.validation.sever", false, &validation_.sever, ECVarFlags::None,
                       "Agent-only: take a limb (legs first) off every living zombie", [this]()
                       {
                           if (!std::exchange(validation_.sever, false))
                           {
                               return;
                           }
                           for (auto& enemy : enemies_)
                           {
                               if (enemy.alive)
                               {
                                   glm::vec3 dir = enemy.worldPos - player_.worldPos;
                                   dir.y = 0.0f;
                                   SeverRandomEnemyLimb(enemy, glm::length(dir) > 0.01f ? glm::normalize(dir) : glm::vec3(0, 0, 1), true);
                               }
                           }
                       });
    cvars.RegisterBool("brotato3d.validation.killAll", false, &validation_.killAll, ECVarFlags::None,
                       "Agent-only: kill every living zombie as if shot", [this]()
                       {
                           if (std::exchange(validation_.killAll, false))
                           {
                               int index = 0;
                               for (auto& enemy : enemies_)
                               {
                                   if (enemy.alive)
                                   {
                                       glm::vec3 dir = enemy.worldPos - player_.worldPos;
                                       dir.y = 0.0f;
                                       enemy.lastHitDebrisDir = glm::length(dir) > 0.01f ? glm::normalize(dir) : glm::vec3(0, 0, 1);
                                       // Cycle the three ways a zombie goes down: shot, headshot, blown apart.
                                       enemy.lastHitDamage = 1;
                                       enemy.lastHitCrit = index % 3 == 1;
                                       enemy.lastHitExplosive = index % 3 == 2;
                                       enemy.currentHp = 0;
                                       KillEnemy(enemy, true);
                                       ++index;
                                   }
                               }
                           }
                       });
    cvars.RegisterBool("brotato3d.validation.clear", false, &validation_.clear, ECVarFlags::None,
                       "Agent-only: remove every zombie and corpse without killing them", [this]()
                       {
                           if (std::exchange(validation_.clear, false))
                           {
                               for (auto& enemy : enemies_)
                               {
                                   CancelGroundIndicatorsForEnemy(enemy.runtimeTag);
                                   DeactivateEnemyKinematicBody(enemy);
                                   enemy.alive = false;
                                   HideEnemyVisual(enemy);
                               }
                           }
                       });
    cvars.RegisterBool("brotato3d.validation.dusk", false, &validation_.dusk, ECVarFlags::None,
                       "Agent-only: end the wave clock now, so night falls and the bus comes", [this]()
                       {
                           if (std::exchange(validation_.dusk, false))
                           {
                               waveSystem_.SkipWaveClock();
                           }
                       });
    cvars.RegisterBool("brotato3d.validation.board", false, &validation_.board, ECVarFlags::None,
                       "Agent-only: put the player at the bus door", [this]()
                       {
                           if (std::exchange(validation_.board, false) && extractionVehicleVisible_)
                           {
                               const glm::vec3 door = GetExtractionZonePos();
                               player_.worldPos = glm::vec3(door.x, player_.radius, door.z);
                           }
                       });
    cvars.RegisterString("brotato3d.validation.arena", std::string{}, &validation_.arena, ECVarFlags::None,
                         "Agent-only: load an arena by id (arenas.json)", [this]()
                         {
                             if (!validation_.arena.empty() && validation_.arena != selectedArenaId_)
                             {
                                 SelectArena(validation_.arena);
                             }
                         });
    // Held switches.
    cvars.RegisterBool("brotato3d.validation.god", false, &validation_.god, ECVarFlags::None,
                       "Agent-only: the player takes no damage");
    cvars.RegisterBool("brotato3d.validation.freeze", false, &validation_.freeze, ECVarFlags::None,
                       "Agent-only: zombies animate but do not move or attack");
    cvars.RegisterBool("brotato3d.validation.holdFire", false, &validation_.holdFire, ECVarFlags::None,
                       "Agent-only: weapons do not fire");
    cvars.RegisterBool("brotato3d.validation.pauseWaves", false, &validation_.pauseWaves, ECVarFlags::None,
                       "Agent-only: the wave director stops spawning and its clock stops");
    cvars.RegisterBool("brotato3d.validation.autoUpgrade", false, &validation_.autoUpgrade, ECVarFlags::None,
                       "Agent-only: take the first card of every level-up so a scripted fight never pauses");
    cvars.RegisterFloat("brotato3d.validation.cameraHeight", 0.0f, &validation_.cameraHeight, ECVarFlags::None,
                        "Agent-only: camera height above the target (0 = gameplay camera)", nullptr, 0.0, 80.0);
    cvars.RegisterFloat("brotato3d.validation.cameraBack", 0.0f, &validation_.cameraBack, ECVarFlags::None,
                        "Agent-only: camera distance behind the target (used with cameraHeight)", nullptr, -80.0, 80.0);
}

void Brotato3DGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg)
{
    using Value = Runtime::Agent::FAgentQueryValue;
    reg.Add("sceneReady", [this]() -> Value { return sceneReady_ && playerRig_.IsBound(); });
    reg.Add("state", [this]() -> Value
    {
        switch (appState_)
        {
        case Brotato3D::EAppState::MainMenu: return std::string("menu");
        case Brotato3D::EAppState::CharacterSelect: return std::string("character_select");
        case Brotato3D::EAppState::Playing: return std::string("playing");
        case Brotato3D::EAppState::Hitstop: return std::string("hitstop");
        case Brotato3D::EAppState::Paused: return std::string("paused");
        case Brotato3D::EAppState::LevelUpPicking: return std::string("level_up");
        case Brotato3D::EAppState::Shopping: return std::string("shopping");
        case Brotato3D::EAppState::Result: return std::string("result");
        }
        return std::string("unknown");
    });
    const auto countEnemies = [this](auto&& predicate)
    {
        return static_cast<int64_t>(std::count_if(enemies_.begin(), enemies_.end(), predicate));
    };
    reg.Add("enemiesAlive", [countEnemies]() -> Value
    {
        return countEnemies([](const Brotato3D::FEnemyRuntime& enemy) { return enemy.alive; });
    });
    reg.Add("corpses", [countEnemies]() -> Value
    {
        return countEnemies([](const Brotato3D::FEnemyRuntime& enemy) { return !enemy.alive && enemy.fading; });
    });
    reg.Add("crawlers", [countEnemies]() -> Value
    {
        return countEnemies([](const Brotato3D::FEnemyRuntime& enemy) { return enemy.alive && enemy.crawling; });
    });
    reg.Add("zombieBodies", [countEnemies]() -> Value
    {
        return countEnemies([](const Brotato3D::FEnemyRuntime& enemy) { return enemy.visual != nullptr; });
    });
    reg.Add("severedLimbs", [this]() -> Value
    {
        int64_t total = 0;
        for (const auto& enemy : enemies_)
        {
            if (enemy.visual && (enemy.alive || enemy.fading))
            {
                total += enemy.visual->SeveredCount();
            }
        }
        return total;
    });
    reg.Add("kills", [this]() -> Value { return static_cast<int64_t>(killCount_); });
    reg.Add("playerHp", [this]() -> Value { return static_cast<int64_t>(player_.currentHp); });
    reg.Add("playerX", [this]() -> Value { return static_cast<double>(player_.worldPos.x); });
    reg.Add("playerZ", [this]() -> Value { return static_cast<double>(player_.worldPos.z); });
    reg.Add("playerSpeed", [this]() -> Value
    {
        return static_cast<double>(glm::length(glm::vec2(playerVelocity_.x, playerVelocity_.z)));
    });
    reg.Add("moveInput", [this]() -> Value
    {
        return fmt::format("events={} {}{}{}{} pad({:.2f},{:.2f}) vel({:.2f},{:.2f})", validation_.inputEvents, keyW_ ? "W" : "-", keyA_ ? "A" : "-",
                           keyS_ ? "S" : "-", keyD_ ? "D" : "-", gamepadMoveInput_.x, gamepadMoveInput_.y,
                           playerMoveVelocity_.x, playerMoveVelocity_.z);
    });
    reg.Add("playerAiming", [this]() -> Value { return playerHasAimTarget_; });
    reg.Add("waveState", [this]() -> Value
    {
        switch (waveSystem_.GetState())
        {
        case Brotato3D::EWaveState::Idle: return std::string("idle");
        case Brotato3D::EWaveState::Active: return std::string("active");
        case Brotato3D::EWaveState::DuskSurge: return std::string("dusk");
        case Brotato3D::EWaveState::Intermission: return std::string("intermission");
        case Brotato3D::EWaveState::AllCleared: return std::string("cleared");
        }
        return std::string("unknown");
    });
    reg.Add("busVisible", [this]() -> Value { return extractionVehicleVisible_; });
    reg.Add("busArrived", [this]() -> Value { return extractionVehicleVisible_ && extractionVehicleAnimMs_ <= 0.0f; });
    reg.Add("boarding", [this]() -> Value { return waveSystem_.IsPlayerInExtractionZone(); });
    reg.Add("arenaSolidPct", [this]() -> Value { return static_cast<double>(arenaNav_.BlockedFraction() * 100.0f); });
    reg.Add("playerInWall", [this]() -> Value { return arenaNav_.IsBlocked(player_.worldPos); });
    reg.Add("zombiesInWalls", [countEnemies, this]() -> Value
    {
        return countEnemies([this](const Brotato3D::FEnemyRuntime& enemy) { return enemy.alive && arenaNav_.IsBlocked(enemy.worldPos); });
    });
    reg.Add("zombiesNearPlayer", [countEnemies, this]() -> Value
    {
        return countEnemies([this](const Brotato3D::FEnemyRuntime& enemy)
        {
            return enemy.alive && DistanceXZ(enemy.worldPos, player_.worldPos) < 2.5f;
        });
    });
    reg.Add("occludersFaded", [this]() -> Value
    {
        return static_cast<int64_t>(std::count_if(occluders_.begin(), occluders_.end(),
                                                   [](const FOccluder& occluder) { return occluder.opacity < 0.9f; }));
    });
    reg.Add("arena", [this]() -> Value { return selectedArenaId_; });
    reg.Add("wave", [this]() -> Value { return static_cast<int64_t>(waveSystem_.GetCurrentWaveIndex() + 1); });
}
