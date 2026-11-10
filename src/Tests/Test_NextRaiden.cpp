#include <catch2/catch_all.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Application/Game/NextRaiden/Combat/BulletPattern.hpp"
#include "Application/Game/NextRaiden/Combat/RaidenRandom.hpp"
#include "Application/Game/NextRaiden/Level/StageFlow.hpp"
#include "Application/Game/NextRaiden/Level/StageIndex.hpp"
#include "Application/Game/NextRaiden/Level/WaveDirector.hpp"
#include "Application/Game/NextRaiden/World/BossSystem.hpp"
#include "Application/Game/NextRaiden/NextRaidenConfig.hpp"
#include "Application/Game/NextRaiden/NextRaidenTypes.hpp"

// ============================================================================
// Test_NextRaiden.cpp - The parts of NextRaiden that can be pinned down without
// an engine: the bullet pattern maths, the formation geometry, the strictness of
// the wave parser, and the determinism of the random source.
//
// These are the pieces that are hardest to debug through a renderer and easiest
// to check here - a pattern that is off by one bullet or mirrored on the wrong
// axis looks like a tuning decision in motion.
// ============================================================================

using namespace NextRaiden;

namespace
{
    FPatternContext MakeContext(const glm::vec3& origin = glm::vec3(0.0f, 7.0f, -40.0f),
                                const glm::vec3& target = glm::vec3(0.0f, 7.0f, 0.0f))
    {
        FPatternContext context;
        context.Origin = origin;
        context.TargetPosition = target;
        return context;
    }

    float NormaliseDegrees(float degrees)
    {
        while (degrees > 180.0f) degrees -= 360.0f;
        while (degrees < -180.0f) degrees += 360.0f;
        return degrees;
    }
}

TEST_CASE("Raiden angle helpers round-trip", "[Unit][NextRaiden]")
{
    // 0 degrees is engine +Z, which is "towards the player". Everything in the kit is
    // authored facing that way, so this is the convention every pattern is measured in.
    const glm::vec3 forward = DirectionFromAngle(0.0f);
    CHECK(forward.z == Catch::Approx(1.0f).margin(1e-5));
    CHECK(forward.x == Catch::Approx(0.0f).margin(1e-5));

    const glm::vec3 right = DirectionFromAngle(90.0f);
    CHECK(right.x == Catch::Approx(1.0f).margin(1e-5));
    CHECK(right.z == Catch::Approx(0.0f).margin(1e-5));

    for (float degrees : {-179.0f, -90.0f, -12.5f, 0.0f, 37.0f, 90.0f, 179.0f})
    {
        CHECK(NormaliseDegrees(AngleFromDirection(DirectionFromAngle(degrees)) - degrees) ==
              Catch::Approx(0.0f).margin(1e-3));
    }
}

TEST_CASE("Aimed pattern points at the target and leads it", "[Unit][NextRaiden]")
{
    FBulletSeed seeds[8];
    FPatternSpec spec;
    spec.Kind = EPatternKind::Aimed;
    spec.Speed = 20.0f;

    // Shooter above the player, player directly below: straight down the screen.
    FPatternContext context = MakeContext();
    REQUIRE(EvaluatePattern(spec, context, seeds) == 1);
    CHECK(seeds[0].Velocity.z == Catch::Approx(20.0f).margin(1e-3));
    CHECK(seeds[0].Velocity.x == Catch::Approx(0.0f).margin(1e-3));

    // A player sliding right should be led to the right, not shot behind.
    spec.LeadSeconds = 0.5f;
    context.TargetVelocity = glm::vec3(10.0f, 0.0f, 0.0f);
    REQUIRE(EvaluatePattern(spec, context, seeds) == 1);
    CHECK(seeds[0].Velocity.x > 0.0f);
}

TEST_CASE("Spread fans symmetrically across the full arc", "[Unit][NextRaiden]")
{
    FBulletSeed seeds[16];
    FPatternSpec spec;
    spec.Kind = EPatternKind::Spread;
    spec.Count = 5;
    spec.ArcDegrees = 40.0f;
    spec.Speed = 18.0f;

    REQUIRE(EvaluatePattern(spec, MakeContext(), seeds) == 5);

    std::vector<float> angles;
    for (int i = 0; i < 5; ++i)
    {
        angles.push_back(AngleFromDirection(seeds[i].Velocity));
        CHECK(glm::length(seeds[i].Velocity) == Catch::Approx(18.0f).margin(1e-3));
    }

    // The outermost shots sit on the edges of the arc, and the middle one is dead centre.
    CHECK(angles.front() == Catch::Approx(-20.0f).margin(1e-2));
    CHECK(angles.back() == Catch::Approx(20.0f).margin(1e-2));
    CHECK(angles[2] == Catch::Approx(0.0f).margin(1e-2));

    // Even spacing, which is what makes a wall of bullets readable as a wall.
    for (size_t i = 1; i < angles.size(); ++i)
    {
        CHECK(angles[i] - angles[i - 1] == Catch::Approx(10.0f).margin(1e-2));
    }

    // A one-bullet spread must not divide by zero; it degenerates to an aimed shot.
    spec.Count = 1;
    REQUIRE(EvaluatePattern(spec, MakeContext(), seeds) == 1);
    CHECK(AngleFromDirection(seeds[0].Velocity) == Catch::Approx(0.0f).margin(1e-2));
}

TEST_CASE("Ring covers the circle and spins between volleys", "[Unit][NextRaiden]")
{
    FBulletSeed seeds[16];
    FPatternSpec spec;
    spec.Kind = EPatternKind::Ring;
    spec.Count = 8;
    spec.SpinDegrees = 5.0f;
    spec.Speed = 12.0f;

    FPatternContext context = MakeContext();
    REQUIRE(EvaluatePattern(spec, context, seeds) == 8);

    // Evenly spaced and all the same speed: a ring that is not is a ring the player can
    // read as a gap that is not there.
    for (int i = 0; i < 8; ++i)
    {
        const float expected = static_cast<float>(i) * 45.0f;
        CHECK(NormaliseDegrees(AngleFromDirection(seeds[i].Velocity) - expected) ==
              Catch::Approx(0.0f).margin(1e-2));
        CHECK(glm::length(seeds[i].Velocity) == Catch::Approx(12.0f).margin(1e-3));
    }

    context.VolleyIndex = 3;
    REQUIRE(EvaluatePattern(spec, context, seeds) == 8);
    CHECK(NormaliseDegrees(AngleFromDirection(seeds[0].Velocity) - 15.0f) == Catch::Approx(0.0f).margin(1e-2));
}

TEST_CASE("Spiral arms follow the phase", "[Unit][NextRaiden]")
{
    FBulletSeed seeds[8];
    FPatternSpec spec;
    spec.Kind = EPatternKind::Spiral;
    spec.Count = 3;
    spec.Speed = 15.0f;

    FPatternContext context = MakeContext();
    context.PhaseDegrees = 30.0f;
    REQUIRE(EvaluatePattern(spec, context, seeds) == 3);
    for (int i = 0; i < 3; ++i)
    {
        const float expected = 30.0f + static_cast<float>(i) * 120.0f;
        CHECK(NormaliseDegrees(AngleFromDirection(seeds[i].Velocity) - expected) ==
              Catch::Approx(0.0f).margin(1e-2));
    }
}

TEST_CASE("Stream is one aimed shot; the burst lives in the timing", "[Unit][NextRaiden]")
{
    FBulletSeed seeds[8];
    FPatternSpec spec;
    spec.Kind = EPatternKind::Stream;
    spec.Burst = 3;
    spec.Speed = 24.0f;

    // The shape of one shot is identical to Aimed - the shooter is what repeats it.
    REQUIRE(EvaluatePattern(spec, MakeContext(), seeds) == 1);
    CHECK(seeds[0].Velocity.z == Catch::Approx(24.0f).margin(1e-3));
    CHECK(PatternBulletCount(spec) == 1);
}

TEST_CASE("Sweep oscillates around the aim direction", "[Unit][NextRaiden]")
{
    FBulletSeed seeds[4];
    FPatternSpec spec;
    spec.Kind = EPatternKind::Sweep;
    spec.SweepDegrees = 40.0f;
    spec.Speed = 20.0f;

    FPatternContext context = MakeContext();
    context.PhaseDegrees = 90.0f; // sin = 1, full swing one way
    REQUIRE(EvaluatePattern(spec, context, seeds) == 1);
    CHECK(AngleFromDirection(seeds[0].Velocity) == Catch::Approx(40.0f).margin(1e-2));

    context.PhaseDegrees = 270.0f; // sin = -1, full swing the other
    REQUIRE(EvaluatePattern(spec, context, seeds) == 1);
    CHECK(AngleFromDirection(seeds[0].Velocity) == Catch::Approx(-40.0f).margin(1e-2));
}

TEST_CASE("Patterns truncate rather than overrun the caller's buffer", "[Unit][NextRaiden]")
{
    FBulletSeed seeds[4];
    FPatternSpec spec;
    spec.Kind = EPatternKind::Ring;
    spec.Count = 16;
    CHECK(EvaluatePattern(spec, MakeContext(), seeds) == 4);
    CHECK(PatternBulletCount(spec) == 16);
}

TEST_CASE("Formations are centred and ordered leader-first", "[Unit][NextRaiden]")
{
    std::vector<glm::vec2> offsets;

    BuildFormation(EFormation::Line, 4, 5.0f, offsets);
    REQUIRE(offsets.size() == 4);
    float sumX = 0.0f;
    for (const glm::vec2& offset : offsets)
    {
        sumX += offset.x;
        CHECK(offset.y == Catch::Approx(0.0f).margin(1e-4));
    }
    CHECK(sumX == Catch::Approx(0.0f).margin(1e-4)); // centred on the wave's x

    // A vee's wingmen trail the point; depth is positive going backwards up the screen.
    BuildFormation(EFormation::Vee, 5, 4.0f, offsets);
    REQUIRE(offsets.size() == 5);
    CHECK(offsets[2].y == Catch::Approx(0.0f).margin(1e-4));
    CHECK(offsets[0].y > offsets[1].y);
    CHECK(offsets[4].y > offsets[3].y);

    // A column is single file.
    BuildFormation(EFormation::Column, 3, 6.0f, offsets);
    REQUIRE(offsets.size() == 3);
    for (const glm::vec2& offset : offsets)
    {
        CHECK(offset.x == Catch::Approx(0.0f).margin(1e-4));
    }
    CHECK(offsets[2].y == Catch::Approx(12.0f).margin(1e-4));

    // A count of zero must still produce one aircraft rather than an empty wave.
    BuildFormation(EFormation::Line, 0, 5.0f, offsets);
    CHECK(offsets.size() == 1);
}

TEST_CASE("Enum names parse exactly, and unknown names fail loudly", "[Unit][NextRaiden]")
{
    EPatternKind pattern = EPatternKind::Ring;
    CHECK(ParsePatternKind("aimed", pattern));
    CHECK(pattern == EPatternKind::Aimed);
    CHECK_FALSE(ParsePatternKind("aim", pattern));
    CHECK_FALSE(ParsePatternKind("Aimed", pattern)); // case matters: the JSON is the contract
    CHECK(pattern == EPatternKind::Aimed);           // a failed parse leaves the output alone

    EBulletVisual visual = EBulletVisual::Bolt;
    CHECK(ParseBulletVisual("boltHostile", visual));
    CHECK(visual == EBulletVisual::BoltHostile);
    CHECK_FALSE(ParseBulletVisual("rocket", visual));

    EEnemyKind kind = EEnemyKind::Scout;
    CHECK(ParseEnemyKind("bomber", kind));
    CHECK(EnemyKindName(kind) == "bomber");

    EPathKind path = EPathKind::Straight;
    CHECK(ParsePathKind("arcInOut", path));
    CHECK_FALSE(ParsePathKind("arcinout", path));

    EFormation formation = EFormation::Line;
    CHECK(ParseFormation("vee", formation));
    CHECK_FALSE(ParseFormation("wedge", formation));

    EItemKind item = EItemKind::None;
    CHECK(ParseItemKind("power", item));
    CHECK_FALSE(ParseItemKind("shield", item));
}

TEST_CASE("Stage 1's shipped wave table parses with no errors", "[Unit][NextRaiden]")
{
    // The real file, not a fixture: a typo in the shipped table is exactly the failure this
    // test exists to catch, and it would otherwise show up as "that wave doesn't shoot".
    FWaveDirector director;
    std::vector<std::string> errors;
    const bool loaded = director.Load("assets/configs/nextraiden/stage1_waves.json", errors);

    for (const std::string& error : errors)
    {
        UNSCOPED_INFO("wave table error: " << error);
    }
    REQUIRE(errors.empty());
    REQUIRE(loaded);
    // Deliberately not an exact count: the table grows every time the stage is tuned, and a
    // test that has to be edited for every tuning pass stops being read. The properties that
    // matter (coverage, no silent gaps, reaching the boss) are checked further down.
    CHECK(director.EventCount() > 0);

    // Sorted by time, because the director only ever looks at the front of the list.
    const std::vector<FWaveEvent>& events = director.Events();
    for (size_t i = 1; i < events.size(); ++i)
    {
        CHECK(events[i - 1].Time <= events[i].Time);
    }
    CHECK(director.Cursor() == 0);
    CHECK_FALSE(director.Exhausted());
}

TEST_CASE("Random source is deterministic and bounded", "[Unit][NextRaiden]")
{
    FRaidenRandom a(12345u);
    FRaidenRandom b(12345u);
    for (int i = 0; i < 64; ++i)
    {
        CHECK(a.Next() == b.Next());
    }

    FRaidenRandom other(99u);
    CHECK(a.Next() != other.Next());

    FRaidenRandom values(7u);
    for (int i = 0; i < 512; ++i)
    {
        const float unit = values.Unit();
        CHECK(unit >= 0.0f);
        CHECK(unit < 1.0f);
        const float signed_ = values.Signed();
        CHECK(signed_ >= -1.0f);
        CHECK(signed_ < 1.0f);
        CHECK(values.Index(4) < 4);
    }

    // A zero seed would lock xorshift at zero forever; the constructor has to reject it.
    FRaidenRandom zero(0u);
    CHECK(zero.Next() != 0u);
}

// ============================================================================
// P2: the stage flow state machine, and the index built from the real stage.
// ============================================================================

TEST_CASE("Stage flow walks Ready to StageClear and gates the world", "[Unit][NextRaiden]")
{
    FStageFlowConfig config;
    config.ReadySeconds = 1.0f;
    config.BossIntroSeconds = 2.0f;
    config.StageClearSeconds = 3.0f;
    config.BossScrollScale = 0.0f;

    FStageFlow flow;
    flow.Configure(config);
    flow.Reset();

    CHECK(flow.State() == EStageState::Ready);
    CHECK_FALSE(flow.CombatRunning());
    CHECK_FALSE(flow.StageClockRunning());

    flow.Update(1.1f);
    CHECK(flow.State() == EStageState::Playing);
    CHECK(flow.CombatRunning());
    CHECK(flow.StageClockRunning());
    CHECK(flow.ScrollScale() == Catch::Approx(1.0f));

    // The boss brakes the world to a stop across the intro rather than cutting it.
    flow.NotifyBossTriggered();
    CHECK(flow.State() == EStageState::BossIntro);
    CHECK(flow.CombatRunning());
    CHECK_FALSE(flow.StageClockRunning()); // wave cues must not run past during the fight
    flow.Update(1.0f);
    CHECK(flow.ScrollScale() == Catch::Approx(0.5f).margin(0.01));
    flow.Update(1.1f);
    CHECK(flow.State() == EStageState::Boss);
    CHECK(flow.ScrollScale() == Catch::Approx(0.0f));

    // A midboss hands the stage back; only the stage boss ends it.
    flow.NotifyMidBossDefeated();
    CHECK(flow.State() == EStageState::Playing);

    flow.NotifyBossTriggered();
    flow.Update(2.1f);
    REQUIRE(flow.State() == EStageState::Boss);
    flow.NotifyBossDefeated();
    CHECK(flow.State() == EStageState::StageClear);
    CHECK_FALSE(flow.CombatRunning());
    CHECK_FALSE(flow.WantsNextStage());
    flow.Update(3.1f);
    CHECK(flow.WantsNextStage());
}

TEST_CASE("Dying during a boss returns to the boss, not to the waves", "[Unit][NextRaiden]")
{
    FStageFlow flow;
    flow.Configure(FStageFlowConfig{});
    flow.Reset();
    flow.Update(2.0f);
    REQUIRE(flow.State() == EStageState::Playing);

    flow.NotifyBossTriggered();
    flow.Update(5.0f);
    REQUIRE(flow.State() == EStageState::Boss);

    flow.NotifyPlayerDied();
    CHECK(flow.State() == EStageState::Dead);
    // Scroll keeps running while dead so the wreck is not left behind the screen edge.
    CHECK(flow.ScrollScale() == Catch::Approx(1.0f));
    flow.NotifyPlayerRespawned();
    CHECK(flow.State() == EStageState::Boss);

    flow.NotifyGameOver();
    CHECK(flow.State() == EStageState::GameOver);
    // Game over is terminal: a late respawn notification must not resurrect the run.
    flow.NotifyPlayerRespawned();
    CHECK(flow.State() == EStageState::GameOver);
}

TEST_CASE("Stage flow events are ignored in the wrong state", "[Unit][NextRaiden]")
{
    FStageFlow flow;
    flow.Configure(FStageFlowConfig{});
    flow.Reset();

    // Ready is not Playing: a boss cue that arrives early must not skip the countdown.
    flow.NotifyBossTriggered();
    CHECK(flow.State() == EStageState::Ready);
    flow.NotifyStageEndReached();
    CHECK(flow.State() == EStageState::Ready);
    // Nothing to advance from Ready either.
    CHECK_FALSE(flow.RequestAdvance());
}

TEST_CASE("Ground target names parse and round-trip", "[Unit][NextRaiden]")
{
    EGroundKind kind = EGroundKind::Tank;
    CHECK(ParseGroundKind("nr_prop_turret", kind));
    CHECK(kind == EGroundKind::Turret);
    CHECK(GroundKindName(kind) == "nr_prop_turret");

    CHECK(ParseGroundKind("nr_bldg_fueldepot", kind));
    CHECK(kind == EGroundKind::FuelDepot);

    // Scenery is deliberately not a target: hangars and towers are backdrop.
    CHECK_FALSE(ParseGroundKind("nr_bldg_hangar", kind));
    CHECK_FALSE(ParseGroundKind("nr_bldg_controltower", kind));
    CHECK_FALSE(ParseGroundKind("nr_prop_radar", kind));

    EBossMove move = EBossMove::Hold;
    CHECK(ParseBossMove("charge", move));
    CHECK(move == EBossMove::Charge);
    CHECK_FALSE(ParseBossMove("rush", move));
}

TEST_CASE("Stage 1's shipped wave table covers the whole stage", "[Unit][NextRaiden]")
{
    FWaveDirector director;
    std::vector<std::string> errors;
    REQUIRE(director.Load("assets/configs/nextraiden/stage1_waves.json", errors));
    for (const std::string& error : errors)
    {
        UNSCOPED_INFO("wave table error: " << error);
    }
    REQUIRE(errors.empty());

    const std::vector<FWaveEvent>& events = director.Events();
    REQUIRE(events.size() >= 25);
    // The stage is 2:40 and the boss cue is at 152 s; waves have to reach the boss, or the
    // last half-minute is an empty corridor.
    CHECK(events.back().Time > 140.0f);
    CHECK(events.front().Time < 6.0f);

    // No silent gap longer than 12 s anywhere in the run.
    for (size_t i = 1; i < events.size(); ++i)
    {
        UNSCOPED_INFO("gap before wave at t=" << events[i].Time);
        CHECK(events[i].Time - events[i - 1].Time <= 12.0f);
    }

    // SkipTo is what makes "jump to the boss" usable; it must move the cursor without
    // spawning, and must be idempotent.
    director.SkipTo(72.0f);
    const size_t cursor = director.Cursor();
    CHECK(cursor > 0);
    CHECK(cursor < events.size());
    director.SkipTo(72.0f);
    CHECK(director.Cursor() == cursor);
    director.SkipTo(1000.0f);
    CHECK(director.Exhausted());
}

TEST_CASE("Stage 2's shipped wave table parses and covers the harbour run", "[Unit][NextRaiden]")
{
    FWaveDirector director;
    std::vector<std::string> errors;
    const bool loaded = director.Load("assets/configs/nextraiden/stage2_waves.json", errors);
    for (const std::string& error : errors)
    {
        UNSCOPED_INFO("wave table error: " << error);
    }
    REQUIRE(errors.empty());
    REQUIRE(loaded);

    const std::vector<FWaveEvent>& events = director.Events();
    REQUIRE(events.size() >= 30);
    for (size_t i = 1; i < events.size(); ++i)
    {
        CHECK(events[i - 1].Time <= events[i].Time);
        UNSCOPED_INFO("gap before wave at t=" << events[i].Time);
        CHECK(events[i].Time - events[i - 1].Time <= 12.0f);
    }
    // The stage boss cue is at 180 s; waves have to still be arriving on the run up to it.
    CHECK(events.back().Time > 168.0f);
    CHECK(events.front().Time < 6.0f);

    // Stage 2 is the one that introduces the two patterns whose angle moves on its own. If
    // they ever fall out of the table the stage quietly becomes stage 1 again, only longer.
    const auto uses = [&events](EPatternKind kind)
    {
        return std::any_of(events.begin(), events.end(),
                           [kind](const FWaveEvent& e) { return e.Fire.Kind == kind; });
    };
    CHECK(uses(EPatternKind::Spiral));
    CHECK(uses(EPatternKind::Sweep));
}

TEST_CASE("Weak point trades aim for damage", "[Unit][NextRaiden]")
{
    const glm::vec3 core(0.0f, 7.0f, -60.0f);

    FBossPhase armoured;   // no weakPoint block: every hit is worth exactly what it says
    CHECK(WeakPointDamageScale(armoured, core, core) == Catch::Approx(1.0f));
    CHECK(WeakPointDamageScale(armoured, core, core + glm::vec3(20.0f, 0.0f, 0.0f)) == Catch::Approx(1.0f));

    FBossPhase exposed;
    exposed.WeakPointBone = "bone_reactor";
    exposed.WeakPointRadius = 3.0f;
    exposed.WeakPointMultiplier = 3.0f;
    exposed.BodyMultiplier = 0.35f;

    CHECK(WeakPointDamageScale(exposed, core, core) == Catch::Approx(3.0f));
    // On the rim counts; a hair outside does not. Measured on XZ only, so a shot at the same
    // spot but a different altitude is still a core hit - bullets and boss share one plane.
    CHECK(WeakPointDamageScale(exposed, core, core + glm::vec3(0.0f, 4.0f, 2.99f)) == Catch::Approx(3.0f));
    CHECK(WeakPointDamageScale(exposed, core, core + glm::vec3(2.2f, 0.0f, 2.2f)) == Catch::Approx(0.35f));
    CHECK(WeakPointDamageScale(exposed, core, core + glm::vec3(6.0f, 0.0f, 0.0f)) == Catch::Approx(0.35f));

    // The point of the mechanic: aimed fire has to be worth meaningfully more than spray.
    CHECK(exposed.WeakPointMultiplier / exposed.BodyMultiplier > 4.0f);
}

TEST_CASE("The shipped stage table chains two stages into one run", "[Unit][NextRaiden]")
{
    FConfig config;
    REQUIRE(config.Load("assets/configs/nextraiden/gameplay.json", "assets/configs/nextraiden/stages.json"));
    REQUIRE(config.Stages.size() == 2);

    for (const FStageDesc& stage : config.Stages)
    {
        UNSCOPED_INFO("stage " << stage.Id);
        CHECK_FALSE(stage.Scene.empty());
        CHECK_FALSE(stage.WaveFile.empty());
        // The stage runs towards -Z, and the scenery has to outlast the camera.
        CHECK(stage.EndZ < stage.StartZ);
        REQUIRE(stage.Bosses.size() >= 2);

        for (size_t i = 0; i < stage.Bosses.size(); ++i)
        {
            const FBossDesc& boss = stage.Bosses[i];
            UNSCOPED_INFO("  boss " << boss.Id);
            // Exactly one boss per stage ends it, and it is the last one: anything else
            // either strands the run or cuts the stage short.
            CHECK(boss.EndsStage == (i + 1 == stage.Bosses.size()));
            if (i > 0)
            {
                CHECK(stage.Bosses[i - 1].TriggerTime < boss.TriggerTime);
            }
            REQUIRE_FALSE(boss.Phases.empty());
            for (size_t p = 0; p < boss.Phases.size(); ++p)
            {
                const FBossPhase& phase = boss.Phases[p];
                UNSCOPED_INFO("    phase " << p);
                if (p > 0)
                {
                    // Phases are picked by falling health, so they have to be written that way.
                    CHECK(boss.Phases[p - 1].HpFraction > phase.HpFraction);
                }
                for (const std::string& bone : phase.ShedBones)
                {
                    // FScadRigLoader keeps a bone's module name verbatim and only strips the
                    // anim_ prefix from clips, so "pod_l" silently sheds nothing. That mistake
                    // cost an afternoon once; this is it written down.
                    UNSCOPED_INFO("    shedBone " << bone);
                    CHECK(bone.rfind("bone_", 0) == 0);
                }
                if (!phase.WeakPointBone.empty())
                {
                    CHECK(phase.WeakPointBone.rfind("bone_", 0) == 0);
                    CHECK(phase.WeakPointMultiplier > 1.0f);
                    CHECK(phase.BodyMultiplier < 1.0f);
                    CHECK(phase.WeakPointRadius > 0.0f);
                    // A weak point smaller than the hull is the whole mechanic.
                    CHECK(phase.WeakPointRadius < boss.Radius);
                }
            }
        }
    }

    // Stage 2 is the longer, harder one, and its boss is the four-phase fortress with two
    // windows where the core is out.
    const FStageDesc& harbour = config.Stages[1];
    CHECK(harbour.EndZ < config.Stages[0].EndZ);
    const FBossDesc& fortress = harbour.Bosses.back();
    CHECK(fortress.Phases.size() == 4);
    CHECK(fortress.HitPoints > config.Stages[0].Bosses.back().HitPoints);
    const auto exposedPhases = std::count_if(fortress.Phases.begin(), fortress.Phases.end(),
                                             [](const FBossPhase& p) { return !p.WeakPointBone.empty(); });
    CHECK(exposedPhases == 2);
}
