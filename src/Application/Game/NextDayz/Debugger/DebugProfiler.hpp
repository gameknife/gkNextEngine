#pragma once

// ============================================================================
// DebugProfiler.hpp - Per-system CPU cost of a NextDayz frame. The game wraps
// each subsystem call in a ScopedTimer; the debugger shows the last, smoothed
// and peak milliseconds so a slow system is visible before it is a hitch.
// A handful of clock reads per frame; always on.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <chrono>

namespace NextDayz::Debug
{
    enum class ETimer : uint8_t
    {
        Tick,           // the whole game tick
        Actions,        // timed actions and their commits
        Player,         // controller, traversal, camera
        Survival,
        Weapons,        // firing, traces, hit/shot events
        SpawnDirector,
        InfectedAi,     // perception, state machine, steering, path solves
        InfectedGround, // per-infected floor raycasts
        InfectedVisuals,// rig poses + hitboxes
        Presentation,   // audio, effects, player rig
        Loot,           // hover + restock
        World,          // wells, clock, weather, pressure
        Hud,            // OnRenderUI: product HUD
        Debugger,       // the debugger itself (diffing, overlays, window)
        Count,
    };
    inline constexpr size_t kTimerCount = static_cast<size_t>(ETimer::Count);

    inline const char* TimerName(ETimer timer)
    {
        switch (timer)
        {
        case ETimer::Tick: return "Game tick";
        case ETimer::Actions: return "Actions";
        case ETimer::Player: return "Player 3C";
        case ETimer::Survival: return "Survival";
        case ETimer::Weapons: return "Weapons";
        case ETimer::SpawnDirector: return "Spawn director";
        case ETimer::InfectedAi: return "Infected AI";
        case ETimer::InfectedGround: return "Infected ground";
        case ETimer::InfectedVisuals: return "Infected visuals";
        case ETimer::Presentation: return "Audio/FX/rig";
        case ETimer::Loot: return "Loot";
        case ETimer::World: return "World/time";
        case ETimer::Hud: return "HUD";
        case ETimer::Debugger: return "Debugger";
        case ETimer::Count: break;
        }
        return "?";
    }

    struct FTimerStat
    {
        float lastMs = 0.0f;
        float averageMs = 0.0f;   // exponential moving average
        float peakMs = 0.0f;      // slowly decaying peak
        float pendingMs = 0.0f;   // accumulating this frame
    };

    class Profiler
    {
    public:
        void Add(ETimer timer, float milliseconds) { stats_[static_cast<size_t>(timer)].pendingMs += milliseconds; }
        // Closes the frame: what accumulated becomes `lastMs`.
        void EndFrame()
        {
            for (FTimerStat& stat : stats_)
            {
                stat.lastMs = stat.pendingMs;
                stat.averageMs = frames_ == 0 ? stat.lastMs : stat.averageMs + (stat.lastMs - stat.averageMs) * 0.08f;
                stat.peakMs = std::max(stat.peakMs * 0.995f, stat.lastMs);
                stat.pendingMs = 0.0f;
            }
            ++frames_;
        }
        const FTimerStat& Stat(ETimer timer) const { return stats_[static_cast<size_t>(timer)]; }
        uint64_t Frames() const { return frames_; }

    private:
        std::array<FTimerStat, kTimerCount> stats_{};
        uint64_t frames_ = 0;
    };

    class ScopedTimer
    {
    public:
        ScopedTimer(Profiler& profiler, ETimer timer)
            : profiler_(profiler), timer_(timer), start_(std::chrono::steady_clock::now())
        {
        }
        ~ScopedTimer()
        {
            const auto elapsed = std::chrono::steady_clock::now() - start_;
            profiler_.Add(timer_, std::chrono::duration<float, std::milli>(elapsed).count());
        }
        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;

    private:
        Profiler& profiler_;
        ETimer timer_;
        std::chrono::steady_clock::time_point start_;
    };
}
