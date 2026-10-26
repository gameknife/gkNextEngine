#pragma once

// ============================================================================
// StageFlow.hpp - What part of the run we are in, as pure logic.
//
// No scene, no engine, no rendering: it takes events in and hands states out, so
// the whole progression can be exercised in a unit test rather than by playing
// through two and a half minutes of stage.
//
// Scroll and combat are gated on the state, which is why the scroll speed factor
// lives here too - stopping the world for a boss is a property of the flow, not
// something the camera decides for itself.
// ============================================================================

#include <cstdint>
#include <string_view>

namespace NextRaiden
{
    enum class EStageState : uint8_t
    {
        Ready,        // brief "get set" before the stage starts moving
        Playing,      // waves and scenery
        BossIntro,    // scroll brakes, boss slides in
        Boss,         // the fight
        StageClear,   // hold, then next stage
        Dead,         // respawn delay
        GameOver,
        AllClear,
        Count
    };

    std::string_view StageStateName(EStageState state);

    struct FStageFlowConfig
    {
        float ReadySeconds = 1.6f;
        float BossIntroSeconds = 2.0f;
        float StageClearSeconds = 3.0f;
        /// The world slows to a stop over the intro rather than stopping dead, because a
        /// hard stop reads as a hitch.
        float BossScrollScale = 0.0f;
    };

    class FStageFlow
    {
    public:
        void Configure(const FStageFlowConfig& config) { config_ = config; }
        void Reset();

        void Update(float deltaSeconds);

        /// Events from the game. Each is idempotent: calling it in the wrong state does nothing.
        void NotifyBossTriggered();
        void NotifyBossDefeated();
        void NotifyMidBossDefeated();
        void NotifyStageEndReached();
        void NotifyPlayerDied();
        void NotifyPlayerRespawned();
        void NotifyGameOver();
        void NotifyAllClear();
        /// Result screens advance on input; returns true when it consumed the press.
        bool RequestAdvance();

        EStageState State() const { return state_; }
        float StateElapsed() const { return elapsed_; }
        /// 0..1 multiplier on the stage's scroll speed.
        float ScrollScale() const;
        /// Waves, ground targets and enemy fire only run while this is true.
        bool CombatRunning() const;
        /// The stage clock only advances while the stage itself is running, so a boss fight
        /// does not push later waves past their cue.
        bool StageClockRunning() const { return state_ == EStageState::Playing; }
        /// The run is over and what is on screen is a result, not a game. Auto-fire would
        /// otherwise keep the ship shooting into the All Clear banner.
        bool RunEnded() const { return state_ == EStageState::GameOver || state_ == EStageState::AllClear; }
        bool WantsNextStage() const { return state_ == EStageState::StageClear && advanceRequested_; }

    private:
        void Transition(EStageState next);

        FStageFlowConfig config_{};
        EStageState state_ = EStageState::Ready;
        EStageState stateBeforeDeath_ = EStageState::Playing;
        float elapsed_ = 0.0f;
        bool advanceRequested_ = false;
    };
}
