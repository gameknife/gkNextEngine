#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/NextDayzConfig.hpp"

#include <glm/geometric.hpp>

namespace NextDayz
{
    // Foot positions in/out. Short sweeps keep even a low-frame-rate lunge,
    // separation push or death slide from tunnelling through walls/ledges.
    template<class TRaycast>
    glm::vec3 ResolveZombieMovement(const glm::vec3& from, const glm::vec3& to,
                                    const FZombieConfig& config, const TRaycast& raycast)
    {
        constexpr float skin = 0.015f;
        const float slopeCos = std::cos(glm::radians(48.0f));
        const auto tryStep = [&](const glm::vec3& start, glm::vec3 target, glm::vec3& result)
        {
            glm::vec3 delta = target - start;
            delta.y = 0.0f;
            const float distance = glm::length(delta);
            if (distance > 1.0e-5f)
            {
                const glm::vec3 direction = delta / distance;
                const glm::vec3 side(-direction.z, 0.0f, direction.x);
                for (float offset : {-config.ControllerRadius, 0.0f, config.ControllerRadius})
                {
                    for (float height : {config.MaxStepHeight + skin, config.ControllerHeight * 0.5f,
                                         config.ControllerHeight - skin})
                    {
                        const auto hit = raycast(start + side * offset + glm::vec3(0.0f, height, 0.0f), direction);
                        if (hit.Hit && hit.T < distance + config.ControllerRadius + skin)
                        {
                            return false;
                        }
                    }
                }
            }

            // A ray from chest height can hit a crate's top and lift the body
            // onto it. Only search up to a real step above the previous feet.
            const auto ground = raycast(glm::vec3(target.x, start.y + config.MaxStepHeight + skin, target.z),
                                        glm::vec3(0.0f, -1.0f, 0.0f));
            if (!ground.Hit || ground.Normal.y < slopeCos ||
                ground.T > config.MaxStepHeight + skin + config.MaxGroundDrop)
            {
                return false;
            }
            target.y = ground.HitPoint.y;
            const auto ceiling = raycast(target + glm::vec3(0.0f, skin, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            if (ceiling.Hit && ceiling.T < config.ControllerHeight - skin)
            {
                return false;
            }
            result = target;
            return true;
        };

        glm::vec3 delta = to - from;
        delta.y = 0.0f;
        const int steps = std::max(1, static_cast<int>(std::ceil(glm::length(delta) /
                                                               std::max(0.1f, config.ControllerRadius * 0.5f))));
        const glm::vec3 step = delta / static_cast<float>(steps);
        glm::vec3 position = from;
        for (int index = 0; index < steps; ++index)
        {
            glm::vec3 next;
            if (tryStep(position, position + step, next))
            {
                position = next;
            }
            else if (std::abs(step.x) > 1.0e-5f && tryStep(position, position + glm::vec3(step.x, 0.0f, 0.0f), next))
            {
                position = next;
            }
            else if (std::abs(step.z) > 1.0e-5f && tryStep(position, position + glm::vec3(0.0f, 0.0f, step.z), next))
            {
                position = next;
            }
            else
            {
                break;
            }
        }
        return position;
    }
}
