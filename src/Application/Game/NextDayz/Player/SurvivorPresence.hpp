#pragma once

#include <cstdint>
#include <glm/vec3.hpp>

namespace NextDayz
{
    // A survivor as the world sees it this frame.
    struct FSurvivorPresence
    {
        uint32_t id = 0;                         // non-zero; single player is kLocalPlayerTargetId
        glm::vec3 feet{};
        glm::vec3 eye{};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
        bool alive = true;
        float sightScale = 1.0f;                 // lantern / crouch; night and fog are world-wide
    };
}
