#include "Application/Game/NextRaiden/NextRaidenConfig.hpp"

#include "Application/Game/NextRaiden/Level/PatternJson.hpp"
#include "Engine/Runtime/Utilities/JsonHelpers.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace NextRaiden
{
    namespace
    {
        void ReadScreen(const json& object, FScreenConfig& screen)
        {
            screen.CameraHeight = object.value("cameraHeight", screen.CameraHeight);
            screen.CameraPitchDegrees = object.value("cameraPitchDegrees", screen.CameraPitchDegrees);
            screen.FieldOfView = object.value("fieldOfView", screen.FieldOfView);
            screen.NearPlane = object.value("nearPlane", screen.NearPlane);
            screen.FarPlane = object.value("farPlane", screen.FarPlane);
            screen.HudColumnFraction = object.value("hudColumnFraction", screen.HudColumnFraction);
        }

        void ReadPlayfield(const json& object, FPlayfieldConfig& field)
        {
            field.FlightAltitude = object.value("flightAltitude", field.FlightAltitude);
            field.HalfWidth = object.value("halfWidth", field.HalfWidth);
            field.AheadLimit = object.value("aheadLimit", field.AheadLimit);
            field.BehindLimit = object.value("behindLimit", field.BehindLimit);
        }

        void ReadPlayer(const json& object, FPlayerConfig& player)
        {
            player.Speed = object.value("speed", player.Speed);
            player.SlowScale = object.value("slowScale", player.SlowScale);
            player.HitRadius = object.value("hitRadius", player.HitRadius);
            player.BankDegrees = object.value("bankDegrees", player.BankDegrees);
            player.BankRateDegrees = object.value("bankRateDegrees", player.BankRateDegrees);
            player.SpawnBehind = object.value("spawnBehind", player.SpawnBehind);
        }

        void ReadWeapon(const json& object, FWeaponConfig& weapon)
        {
            weapon.IntervalSeconds = object.value("intervalSeconds", weapon.IntervalSeconds);
            weapon.BulletSpeed = object.value("bulletSpeed", weapon.BulletSpeed);
            weapon.BulletRadius = object.value("bulletRadius", weapon.BulletRadius);
            weapon.MuzzleForward = object.value("muzzleForward", weapon.MuzzleForward);
            weapon.SpreadOffset = object.value("spreadOffset", weapon.SpreadOffset);
            weapon.OptionIntervalSeconds = object.value("optionIntervalSeconds", weapon.OptionIntervalSeconds);
            weapon.OptionBulletSpeed = object.value("optionBulletSpeed", weapon.OptionBulletSpeed);
            weapon.OptionDamage = object.value("optionDamage", weapon.OptionDamage);
            weapon.OptionOffsetX = object.value("optionOffsetX", weapon.OptionOffsetX);
            weapon.OptionOffsetZ = object.value("optionOffsetZ", weapon.OptionOffsetZ);
            weapon.OptionFollowRate = object.value("optionFollowRate", weapon.OptionFollowRate);
            weapon.AutoFire = object.value("autoFire", weapon.AutoFire);
            if (object.contains("levels") && object.at("levels").is_array())
            {
                const json& levels = object.at("levels");
                for (size_t i = 0; i < weapon.Levels.size() && i < levels.size(); ++i)
                {
                    const json& entry = levels[i];
                    if (!entry.is_object())
                    {
                        continue;
                    }
                    weapon.Levels[i].Shots = entry.value("shots", weapon.Levels[i].Shots);
                    weapon.Levels[i].ArcDegrees = entry.value("arcDegrees", weapon.Levels[i].ArcDegrees);
                    weapon.Levels[i].Damage = entry.value("damage", weapon.Levels[i].Damage);
                }
            }
        }

        void ReadSurvival(const json& object, FSurvivalConfig& survival)
        {
            survival.Lives = object.value("lives", survival.Lives);
            survival.Shield = object.value("shield", survival.Shield);
            survival.Bombs = object.value("bombs", survival.Bombs);
            survival.InvulnerableSeconds = object.value("invulnerableSeconds", survival.InvulnerableSeconds);
            survival.RespawnSeconds = object.value("respawnSeconds", survival.RespawnSeconds);
            survival.BombDamage = object.value("bombDamage", survival.BombDamage);
            survival.BombInvulnerableSeconds = object.value("bombInvulnerableSeconds", survival.BombInvulnerableSeconds);
            survival.PowerLossOnDeath = object.value("powerLossOnDeath", survival.PowerLossOnDeath);
        }

        void ReadDebris(const json& object, FDebrisConfig& debris)
        {
            debris.Budget = object.value("budget", debris.Budget);
            debris.PerPartPrefab = object.value("perPartPrefab", debris.PerPartPrefab);
            debris.MinSpeed = object.value("minSpeed", debris.MinSpeed);
            debris.MaxSpeed = object.value("maxSpeed", debris.MaxSpeed);
            debris.MinLift = object.value("minLift", debris.MinLift);
            debris.MaxLift = object.value("maxLift", debris.MaxLift);
            debris.SpinRadiansPerSec = object.value("spinRadiansPerSec", debris.SpinRadiansPerSec);
            debris.MaxAgeSeconds = object.value("maxAgeSeconds", debris.MaxAgeSeconds);
            debris.SleepSpeed = object.value("sleepSpeed", debris.SleepSpeed);
            debris.RecycleMargin = object.value("recycleMargin", debris.RecycleMargin);
            debris.KillPlaneY = object.value("killPlaneY", debris.KillPlaneY);
        }

        /// Both bullet tables are keyed by the same visual names the wave files use, so a
        /// typo names itself instead of silently leaving one visual at its default.
        void ReadBullets(const json& object, FBulletPoolSizes& bullets)
        {
            if (object.contains("pool") && object.at("pool").is_object())
            {
                for (const auto& entry : object.at("pool").items())
                {
                    if (!entry.key().empty() && entry.key().front() == '_')
                    {
                        continue;
                    }
                    EBulletVisual visual = EBulletVisual::Bolt;
                    if (!ParseBulletVisual(entry.key(), visual))
                    {
                        SPDLOG_WARN("[NextRaiden] gameplay.json bullets.pool: unknown visual '{}'", entry.key());
                        continue;
                    }
                    bullets.PerVisual[static_cast<size_t>(visual)] = entry.value().get<int>();
                }
            }
            if (object.contains("scale") && object.at("scale").is_object())
            {
                for (const auto& entry : object.at("scale").items())
                {
                    if (!entry.key().empty() && entry.key().front() == '_')
                    {
                        continue;
                    }
                    EBulletVisual visual = EBulletVisual::Bolt;
                    if (!ParseBulletVisual(entry.key(), visual))
                    {
                        SPDLOG_WARN("[NextRaiden] gameplay.json bullets.scale: unknown visual '{}'", entry.key());
                        continue;
                    }
                    bullets.Scale[static_cast<size_t>(visual)] = entry.value().get<float>();
                }
            }
        }

        void ReadItems(const json& object, FItemConfig& items)
        {
            items.PoolPerKind = object.value("poolPerKind", items.PoolPerKind);
            items.DriftSpeed = object.value("driftSpeed", items.DriftSpeed);
            items.MagnetRadius = object.value("magnetRadius", items.MagnetRadius);
            items.MagnetSpeed = object.value("magnetSpeed", items.MagnetSpeed);
            items.PickupRadius = object.value("pickupRadius", items.PickupRadius);
            items.SpinDegreesPerSec = object.value("spinDegreesPerSec", items.SpinDegreesPerSec);
            items.Scale = object.value("scale", items.Scale);
            items.PowerFullScore = object.value("powerFullScore", items.PowerFullScore);
            items.OptionFullScore = object.value("optionFullScore", items.OptionFullScore);
            if (object.contains("medalValues") && object.at("medalValues").is_array())
            {
                const json& values = object.at("medalValues");
                for (size_t i = 0; i < items.MedalValues.size() && i < values.size(); ++i)
                {
                    items.MedalValues[i] = values[i].get<int>();
                }
            }
        }

        void ReadFlow(const json& object, FStageFlowConfig& flow)
        {
            flow.ReadySeconds = object.value("readySeconds", flow.ReadySeconds);
            flow.BossIntroSeconds = object.value("bossIntroSeconds", flow.BossIntroSeconds);
            flow.StageClearSeconds = object.value("stageClearSeconds", flow.StageClearSeconds);
            flow.BossScrollScale = object.value("bossScrollScale", flow.BossScrollScale);
        }

        void ReadGround(const json& object, const std::string& context, FGroundStats& stats)
        {
            stats.HitPoints = object.value("hitPoints", stats.HitPoints);
            stats.Radius = object.value("radius", stats.Radius);
            stats.Score = object.value("score", stats.Score);
            stats.DebrisPieces = object.value("debrisPieces", stats.DebrisPieces);
            stats.BlastRadius = object.value("blastRadius", stats.BlastRadius);
            stats.BlastDamage = object.value("blastDamage", stats.BlastDamage);
            if (object.contains("fire"))
            {
                std::vector<std::string> errors;
                stats.CanFire = ReadPatternSpec(object.at("fire"), context, stats.Fire, errors);
                for (const std::string& error : errors)
                {
                    SPDLOG_ERROR("[NextRaiden] gameplay.json {}", error);
                }
            }
            else
            {
                stats.CanFire = false;
            }
        }

        bool ReadBoss(const json& object, const std::string& context, FBossDesc& boss)
        {
            std::vector<std::string> errors;
            boss.Id = object.value("id", context);
            boss.RigPath = object.value("rig", boss.RigPath);
            boss.EntryClip = object.value("entryClip", boss.EntryClip);
            boss.TriggerTime = object.value("triggerTime", boss.TriggerTime);
            boss.HitPoints = object.value("hitPoints", boss.HitPoints);
            boss.Radius = object.value("radius", boss.Radius);
            boss.Score = object.value("score", boss.Score);
            boss.DebrisPieces = object.value("debrisPieces", boss.DebrisPieces);
            boss.HoldDepth = object.value("holdDepth", boss.HoldDepth);
            boss.EntrySeconds = object.value("entrySeconds", boss.EntrySeconds);
            boss.MoveAmplitude = object.value("moveAmplitude", boss.MoveAmplitude);
            boss.MovePeriod = object.value("movePeriod", boss.MovePeriod);
            boss.EndsStage = object.value("endsStage", boss.EndsStage);
            if (object.contains("drop"))
            {
                const std::string name = object.at("drop").get<std::string>();
                if (!ParseItemKind(name, boss.Drop))
                {
                    SPDLOG_ERROR("[NextRaiden] {}: unknown drop '{}'", context, name);
                    return false;
                }
            }
            if (boss.RigPath.empty())
            {
                SPDLOG_ERROR("[NextRaiden] {}: no 'rig' path", context);
                return false;
            }

            boss.Phases.clear();
            if (object.contains("phases") && object.at("phases").is_array())
            {
                int index = 0;
                for (const json& entry : object.at("phases"))
                {
                    const std::string phaseContext = fmt::format("{}.phase[{}]", context, index++);
                    FBossPhase phase;
                    phase.HpFraction = entry.value("hpFraction", phase.HpFraction);
                    phase.Clip = entry.value("clip", phase.Clip);
                    phase.MoveSpeedScale = entry.value("moveSpeedScale", phase.MoveSpeedScale);
                    if (entry.contains("move"))
                    {
                        const std::string name = entry.at("move").get<std::string>();
                        if (!ParseBossMove(name, phase.Move))
                        {
                            SPDLOG_ERROR("[NextRaiden] {}: unknown move '{}'", phaseContext, name);
                            return false;
                        }
                    }
                    if (entry.contains("shedBones") && entry.at("shedBones").is_array())
                    {
                        for (const json& bone : entry.at("shedBones"))
                        {
                            phase.ShedBones.push_back(bone.get<std::string>());
                        }
                    }
                    if (entry.contains("weakPoint") && entry.at("weakPoint").is_object())
                    {
                        const json& weak = entry.at("weakPoint");
                        phase.WeakPointBone = weak.value("bone", phase.WeakPointBone);
                        phase.WeakPointRadius = weak.value("radius", phase.WeakPointRadius);
                        phase.WeakPointMultiplier = weak.value("multiplier", phase.WeakPointMultiplier);
                        phase.BodyMultiplier = weak.value("bodyMultiplier", phase.BodyMultiplier);
                        if (phase.WeakPointBone.empty())
                        {
                            // A weakPoint block with no bone is a phase that silently behaves
                            // like every other one - exactly the kind of tuning change nobody
                            // can see and everybody has to bisect for.
                            SPDLOG_ERROR("[NextRaiden] {}: weakPoint has no 'bone'", phaseContext);
                            return false;
                        }
                    }
                    if (entry.contains("fire"))
                    {
                        phase.HasFire = ReadPatternSpec(entry.at("fire"), fmt::format("{}.fire", phaseContext),
                                                        phase.Fire, errors);
                    }
                    if (entry.contains("fire2"))
                    {
                        phase.HasFire2 = ReadPatternSpec(entry.at("fire2"), fmt::format("{}.fire2", phaseContext),
                                                         phase.Fire2, errors);
                    }
                    boss.Phases.push_back(std::move(phase));
                }
            }

            for (const std::string& error : errors)
            {
                SPDLOG_ERROR("[NextRaiden] {}", error);
            }
            if (boss.Phases.empty())
            {
                SPDLOG_ERROR("[NextRaiden] {}: no phases", context);
                return false;
            }
            return errors.empty();
        }

        void ReadEnemy(const json& object, FEnemyStats& stats)
        {
            stats.HitPoints = object.value("hitPoints", stats.HitPoints);
            stats.Radius = object.value("radius", stats.Radius);
            stats.Speed = object.value("speed", stats.Speed);
            stats.Score = object.value("score", stats.Score);
            stats.DebrisPieces = object.value("debrisPieces", stats.DebrisPieces);
        }
    }

    bool FConfig::Load(const std::string& gameplayPath, const std::string& stagesPath)
    {
        json gameplay;
        if (NextJson::TryLoadFile(gameplayPath, gameplay) && gameplay.is_object())
        {
            if (gameplay.contains("screen")) ReadScreen(gameplay.at("screen"), Screen);
            if (gameplay.contains("playfield")) ReadPlayfield(gameplay.at("playfield"), Playfield);
            if (gameplay.contains("player")) ReadPlayer(gameplay.at("player"), Player);
            if (gameplay.contains("weapon")) ReadWeapon(gameplay.at("weapon"), Weapon);
            if (gameplay.contains("survival")) ReadSurvival(gameplay.at("survival"), Survival);
            if (gameplay.contains("debris")) ReadDebris(gameplay.at("debris"), Debris);
            if (gameplay.contains("bullets")) ReadBullets(gameplay.at("bullets"), Bullets);
            if (gameplay.contains("items")) ReadItems(gameplay.at("items"), Items);
            if (gameplay.contains("flow")) ReadFlow(gameplay.at("flow"), Flow);
            if (gameplay.contains("grounds") && gameplay.at("grounds").is_object())
            {
                const json& grounds = gameplay.at("grounds");
                for (const auto& entry : grounds.items())
                {
                    if (!entry.key().empty() && entry.key().front() == '_')
                    {
                        continue; // "_comment" and friends are documentation, not data
                    }
                    EGroundKind kind = EGroundKind::Tank;
                    if (!ParseGroundKind(entry.key(), kind))
                    {
                        SPDLOG_WARN("[NextRaiden] gameplay.json: unknown ground target '{}'", entry.key());
                        continue;
                    }
                    ReadGround(entry.value(), fmt::format("grounds.{}.fire", entry.key()),
                               Grounds[static_cast<size_t>(kind)]);
                }
            }
            if (gameplay.contains("score") && gameplay.at("score").is_object())
            {
                Score.ExtendEvery = gameplay.at("score").value("extendEvery", Score.ExtendEvery);
            }
            if (gameplay.contains("enemies") && gameplay.at("enemies").is_object())
            {
                const json& enemies = gameplay.at("enemies");
                for (const auto& entry : enemies.items())
                {
                    if (!entry.key().empty() && entry.key().front() == '_')
                    {
                        continue;
                    }
                    EEnemyKind kind = EEnemyKind::Scout;
                    if (!ParseEnemyKind(entry.key(), kind))
                    {
                        SPDLOG_WARN("[NextRaiden] gameplay.json: unknown enemy kind '{}'", entry.key());
                        continue;
                    }
                    ReadEnemy(entry.value(), Enemies[static_cast<size_t>(kind)]);
                }
            }
        }
        else
        {
            SPDLOG_WARN("[NextRaiden] '{}' unavailable; using built-in gameplay defaults", gameplayPath);
        }

        Stages.clear();
        json stages;
        if (NextJson::TryLoadFile(stagesPath, stages) && stages.is_object() && stages.contains("stages"))
        {
            for (const json& entry : stages.at("stages"))
            {
                if (!entry.is_object() || !entry.contains("scene"))
                {
                    continue;
                }
                FStageDesc desc;
                desc.Scene = entry.at("scene").get<std::string>();
                desc.Id = entry.value("id", desc.Scene);
                desc.DisplayName = entry.value("displayName", desc.Id);
                desc.WaveFile = entry.value("waves", desc.WaveFile);
                desc.Seed = entry.value("seed", desc.Seed);
                desc.ScrollSpeed = entry.value("scrollSpeed", desc.ScrollSpeed);
                desc.StartZ = entry.value("startZ", desc.StartZ);
                desc.EndZ = entry.value("endZ", desc.EndZ);
                if (entry.contains("bosses") && entry.at("bosses").is_array())
                {
                    int bossIndex = 0;
                    for (const json& bossEntry : entry.at("bosses"))
                    {
                        FBossDesc boss;
                        const std::string context = fmt::format("stage '{}' boss[{}]", desc.Id, bossIndex++);
                        if (ReadBoss(bossEntry, context, boss))
                        {
                            desc.Bosses.push_back(std::move(boss));
                        }
                    }
                }
                Stages.push_back(std::move(desc));
            }
        }

        if (Stages.empty())
        {
            SPDLOG_WARN("[NextRaiden] '{}' listed no stage; falling back to stage1_airfield", stagesPath);
            FStageDesc fallback;
            fallback.Id = "airfield";
            fallback.DisplayName = "Airfield Run";
            fallback.Scene = "assets/scad/source/next_raiden/stage1_airfield.scad";
            fallback.WaveFile = "assets/configs/nextraiden/stage1_waves.json";
            Stages.push_back(std::move(fallback));
            return false;
        }
        return true;
    }
}
