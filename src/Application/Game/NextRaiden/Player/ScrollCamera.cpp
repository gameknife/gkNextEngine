#include "Application/Game/NextRaiden/Player/ScrollCamera.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "Engine/Assets/Core/Model.hpp"

namespace NextRaiden
{
    namespace
    {
        constexpr glm::vec3 kUp{0.0f, 0.0f, -1.0f};
    }

    glm::vec3 FScrollCamera::Eye() const
    {
        // Pitch 0 looks straight down; the tilt pulls the lens back along +Z (screen down)
        // so the low-poly buildings show a little of their sides.
        const float back = config_.CameraHeight * std::tan(glm::radians(config_.CameraPitchDegrees));
        return glm::vec3(0.0f, config_.CameraHeight, centreZ_ + back);
    }

    void FScrollCamera::Fill(Assets::Camera& outCamera) const
    {
        outCamera.ModelView = glm::lookAt(Eye(), Target(), kUp);
        outCamera.FieldOfView = config_.FieldOfView;
        outCamera.NearPlane = config_.NearPlane;
        outCamera.FarPlane = config_.FarPlane;
    }

    float FScrollCamera::EdgeZ(float planeY, float screenDown) const
    {
        const glm::vec3 eye = Eye();
        const glm::vec3 forward = glm::normalize(Target() - eye);
        const glm::vec3 right = glm::normalize(glm::cross(forward, kUp));
        const glm::vec3 up = glm::cross(right, forward);

        const float halfFov = glm::radians(config_.FieldOfView * 0.5f);
        const glm::vec3 edge = glm::normalize(forward - up * (screenDown * std::tan(halfFov)));
        if (edge.y >= -1e-4f)
        {
            // Edge points at or above the horizon: there is no intersection. Report a long way
            // ahead rather than a NaN, so callers keep working at silly pitch values.
            return centreZ_ - config_.FarPlane;
        }
        const float t = (planeY - eye.y) / edge.y;
        return eye.z + edge.z * t;
    }
}
