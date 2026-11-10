#pragma once

// ============================================================================
// LagCompensation.hpp - Where every infected and survivor stood over the last
// second of server time. A client aims at what it sees, which is the server's
// world as it was a round trip and an interpolation delay ago; the server
// checks a reported hit against the target's position at that moment
// (favour the shooter), instead of against where the target stands now.
// Pure data: the server records one frame per tick and queries on a hit.
// See docs/projects/nextdayz/nextdayz-multiplayer-design.md.
// ============================================================================

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

#include <deque>
#include <span>

#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"

namespace NextDayz
{
    struct FZombieRuntime;
    struct FSurvivorPresence;

    class FLagHistory
    {
    public:
        explicit FLagHistory(double keepSeconds = 1.0) : keepSeconds_(keepSeconds) {}

        void Record(double time, std::span<const FZombieRuntime> zombies, std::span<const FSurvivorPresence> survivors);
        void Clear() { frames_.clear(); }

        // The closest a body's axis (feet to head) came to `point` in
        // [time - window, time + window]; nullopt when it was not recorded then.
        std::optional<float> ZombieDistance(FZombieHandle zombie, const glm::vec3& point, double time,
                                            double window) const;
        std::optional<float> SurvivorDistance(uint32_t survivorId, const glm::vec3& point, double time,
                                              double window) const;

        size_t FrameCount() const { return frames_.size(); }
        double OldestTime() const { return frames_.empty() ? 0.0 : frames_.front().time; }

        static constexpr float kBodyHeight = 1.8f;

    private:
        struct FZombieSample
        {
            FZombieHandle handle;
            glm::vec3 feet{};
        };
        struct FSurvivorSample
        {
            uint32_t id = 0;
            glm::vec3 feet{};
        };
        struct FFrame
        {
            double time = 0.0;
            std::vector<FZombieSample> zombies;
            std::vector<FSurvivorSample> survivors;
        };

        template <class TFind>
        std::optional<float> ClosestAxis(const glm::vec3& point, double time, double window, TFind&& find) const;

        double keepSeconds_ = 1.0;
        std::deque<FFrame> frames_;
    };

    // Distance from `point` to the vertical body axis standing on `feet`.
    float DistanceToBodyAxis(const glm::vec3& feet, const glm::vec3& point, float height = FLagHistory::kBodyHeight);
}
