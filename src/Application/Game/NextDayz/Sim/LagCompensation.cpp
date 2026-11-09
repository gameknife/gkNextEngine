#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/Sim/LagCompensation.hpp"

#include "Application/Game/NextDayz/Sim/WorldSim.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieSystem.hpp"

#include <glm/geometric.hpp>

namespace NextDayz
{
    float DistanceToBodyAxis(const glm::vec3& feet, const glm::vec3& point, float height)
    {
        const float y = std::clamp(point.y, feet.y, feet.y + height);
        return glm::distance(point, glm::vec3(feet.x, y, feet.z));
    }

    void FLagHistory::Record(double time, std::span<const FZombieRuntime> zombies,
                             std::span<const FSurvivorPresence> survivors)
    {
        FFrame frame;
        frame.time = time;
        for (const FZombieRuntime& zombie : zombies)
        {
            // The dead are still hittable while they fall; only empty slots are skipped.
            if (zombie.active) frame.zombies.push_back({zombie.handle, zombie.position});
        }
        for (const FSurvivorPresence& survivor : survivors)
        {
            if (survivor.alive) frame.survivors.push_back({survivor.id, survivor.feet});
        }
        frames_.push_back(std::move(frame));
        while (!frames_.empty() && frames_.front().time < time - keepSeconds_)
        {
            frames_.pop_front();
        }
    }

    template <class TFind>
    std::optional<float> FLagHistory::ClosestAxis(const glm::vec3& point, double time, double window,
                                                  TFind&& find) const
    {
        std::optional<float> best;
        for (const FFrame& frame : frames_)
        {
            if (frame.time < time - window || frame.time > time + window) continue;
            if (const std::optional<glm::vec3> feet = find(frame))
            {
                const float distance = DistanceToBodyAxis(*feet, point);
                if (!best || distance < *best) best = distance;
            }
        }
        return best;
    }

    std::optional<float> FLagHistory::ZombieDistance(FZombieHandle zombie, const glm::vec3& point, double time,
                                                     double window) const
    {
        return ClosestAxis(point, time, window, [zombie](const FFrame& frame) -> std::optional<glm::vec3>
        {
            for (const FZombieSample& sample : frame.zombies)
                if (sample.handle == zombie) return sample.feet;
            return std::nullopt;
        });
    }

    std::optional<float> FLagHistory::SurvivorDistance(uint32_t survivorId, const glm::vec3& point, double time,
                                                       double window) const
    {
        return ClosestAxis(point, time, window, [survivorId](const FFrame& frame) -> std::optional<glm::vec3>
        {
            for (const FSurvivorSample& sample : frame.survivors)
                if (sample.id == survivorId) return sample.feet;
            return std::nullopt;
        });
    }
}
