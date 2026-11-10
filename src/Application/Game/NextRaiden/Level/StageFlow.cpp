#include "Application/Game/NextRaiden/Level/StageFlow.hpp"

#include <algorithm>

namespace NextRaiden
{
    std::string_view StageStateName(EStageState state)
    {
        switch (state)
        {
        case EStageState::Ready: return "ready";
        case EStageState::Playing: return "playing";
        case EStageState::BossIntro: return "bossIntro";
        case EStageState::Boss: return "boss";
        case EStageState::StageClear: return "stageClear";
        case EStageState::Dead: return "dead";
        case EStageState::GameOver: return "gameOver";
        case EStageState::AllClear: return "allClear";
        default: return "unknown";
        }
    }

    void FStageFlow::Reset()
    {
        state_ = EStageState::Ready;
        stateBeforeDeath_ = EStageState::Playing;
        elapsed_ = 0.0f;
        advanceRequested_ = false;
    }

    void FStageFlow::Transition(EStageState next)
    {
        if (state_ == next)
        {
            return;
        }
        state_ = next;
        elapsed_ = 0.0f;
        advanceRequested_ = false;
    }

    void FStageFlow::Update(float deltaSeconds)
    {
        elapsed_ += deltaSeconds;
        switch (state_)
        {
        case EStageState::Ready:
            if (elapsed_ >= config_.ReadySeconds)
            {
                Transition(EStageState::Playing);
            }
            break;
        case EStageState::BossIntro:
            if (elapsed_ >= config_.BossIntroSeconds)
            {
                Transition(EStageState::Boss);
            }
            break;
        case EStageState::StageClear:
            if (elapsed_ >= config_.StageClearSeconds)
            {
                // The result screen stops waiting on its own; the game asks for the next stage.
                advanceRequested_ = true;
            }
            break;
        default:
            break;
        }
    }

    void FStageFlow::NotifyBossTriggered()
    {
        if (state_ == EStageState::Playing)
        {
            Transition(EStageState::BossIntro);
        }
    }

    void FStageFlow::NotifyBossDefeated()
    {
        if (state_ == EStageState::Boss || state_ == EStageState::BossIntro)
        {
            Transition(EStageState::StageClear);
        }
    }

    void FStageFlow::NotifyMidBossDefeated()
    {
        // A midboss hands the stage back rather than ending it.
        if (state_ == EStageState::Boss || state_ == EStageState::BossIntro)
        {
            Transition(EStageState::Playing);
        }
    }

    void FStageFlow::NotifyStageEndReached()
    {
        if (state_ == EStageState::Playing)
        {
            Transition(EStageState::StageClear);
        }
    }

    void FStageFlow::NotifyPlayerDied()
    {
        if (state_ == EStageState::Dead || state_ == EStageState::GameOver || state_ == EStageState::AllClear)
        {
            return;
        }
        // Remembered so a death during a boss fight goes back to the boss, not to the waves.
        stateBeforeDeath_ = state_;
        Transition(EStageState::Dead);
    }

    void FStageFlow::NotifyPlayerRespawned()
    {
        if (state_ == EStageState::Dead)
        {
            Transition(stateBeforeDeath_);
        }
    }

    void FStageFlow::NotifyGameOver()
    {
        Transition(EStageState::GameOver);
    }

    void FStageFlow::NotifyAllClear()
    {
        Transition(EStageState::AllClear);
    }

    bool FStageFlow::RequestAdvance()
    {
        if (state_ == EStageState::StageClear || state_ == EStageState::GameOver || state_ == EStageState::AllClear)
        {
            advanceRequested_ = true;
            return true;
        }
        return false;
    }

    float FStageFlow::ScrollScale() const
    {
        switch (state_)
        {
        case EStageState::Ready:
        case EStageState::Playing:
        case EStageState::Dead:
            return 1.0f;
        case EStageState::BossIntro:
        {
            // Ease the world to a stop across the intro instead of cutting the scroll, which
            // otherwise reads as a dropped frame.
            const float t = config_.BossIntroSeconds > 0.0f
                                ? std::clamp(elapsed_ / config_.BossIntroSeconds, 0.0f, 1.0f)
                                : 1.0f;
            return 1.0f + (config_.BossScrollScale - 1.0f) * t;
        }
        case EStageState::Boss:
            return config_.BossScrollScale;
        default:
            return 0.0f;
        }
    }

    bool FStageFlow::CombatRunning() const
    {
        return state_ == EStageState::Playing || state_ == EStageState::BossIntro || state_ == EStageState::Boss;
    }
}
