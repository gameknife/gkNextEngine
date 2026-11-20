#pragma once

#include "Application/Game/NextDayz/Player/PlayerController.hpp"

#include <span>

namespace NextDayz
{
    // The player controller must be destroyed before its collision world.
    class FWebPhysics
    {
    public:
        FWebPhysics(size_t meshCount, size_t instanceCount);
        ~FWebPhysics();

        void AddMesh(std::span<const float> positions, std::span<const uint32_t> indices);
        void AddInstances(std::span<const uint32_t> records);
        std::unique_ptr<INextCharacterControllerBackend> CreateCharacter(const FCharacterControllerSettings& settings);
        std::optional<FWorldRayHit> Raycast(const glm::vec3& origin, const glm::vec3& direction, float distance) const;

    private:
        struct FImpl;
        std::unique_ptr<FImpl> impl_;
    };
}
