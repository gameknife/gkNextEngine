#include "Engine/Common/CoreMinimal.hpp"
#include "PlayerController.hpp"

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

namespace NextDayz
{
    FWorldRayQuery MakeDesktopPlayerRayQuery(NextEngine& engine)
    {
        return [&engine](const glm::vec3& origin, const glm::vec3& direction, float maxDistance)
            -> std::optional<FWorldRayHit>
        {
            std::optional<FWorldRayHit> hit;
            engine.RayCast(origin, direction, [&](Assets::RayCastResult result)
            {
                if (result.Hit && result.T >= 0.0f && result.T <= maxDistance)
                {
                    hit = FWorldRayHit{glm::vec3(result.HitPoint), glm::vec3(result.Normal), result.T, result.InstanceId};
                }
                return true;
            });
            return hit;
        };
    }

    void PlayerController::Create(NextPhysics* physics, const glm::vec3& spawnPosition, const FConfig& config)
    {
        if (physics)
        {
            Create([physics](const FCharacterControllerSettings& settings)
                   { return physics->CreateCharacterController(settings); },
                   [physics] { physics->CompleteTick(); }, spawnPosition, config);
        }
        else
        {
            Create({}, {}, spawnPosition, config);
        }
    }

    void PlayerController::QueueContextualJump(NextEngine& engine)
    {
        QueueContextualJump(MakeDesktopPlayerRayQuery(engine));
    }

    void PlayerController::Update(float deltaSeconds, NextEngine& engine)
    {
        Update(deltaSeconds, MakeDesktopPlayerRayQuery(engine));
    }
}
