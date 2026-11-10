#pragma once

// ============================================================================
// ScrollCamera.hpp - The one thing that defines "where the stage is right now".
//
// The world is static and the camera walks it, so the camera centre is also the
// stage clock: everything else (spawn windows, the playfield bounds, debris
// recycling) is expressed relative to CentreZ().
//
// Orientation contract (design section 5.1), and it is not negotiable because
// every kit asset's default facing depends on it:
//   forward / screen up = engine -Z        up vector = (0, 0, -1)
//   screen right        = engine +X        (so SCAD +X reads right, unmirrored)
// A top-down camera has to pick between those two; this is the pick that makes
// the kit's "everything faces the player by default" come out true.
// ============================================================================

#include <glm/glm.hpp>

#include "Application/Game/NextRaiden/NextRaidenConfig.hpp"
#include "Engine/Assets/AssetsFwd.hpp"

namespace NextRaiden
{
    class FScrollCamera
    {
    public:
        void Configure(const FScreenConfig& screen, float flightAltitude)
        {
            config_ = screen;
            flightAltitude_ = flightAltitude;
        }
        void Reset(float startZ) { centreZ_ = startZ; }

        /// The stage runs towards -Z, so scrolling is the centre walking down.
        void Advance(float scrollSpeed, float deltaSeconds) { centreZ_ -= scrollSpeed * deltaSeconds; }

        float CentreZ() const { return centreZ_; }
        glm::vec3 Eye() const;
        glm::vec3 Target() const { return glm::vec3(0.0f, 0.0f, centreZ_); }

        void Fill(Assets::Camera& outCamera) const;

        float FlightAltitude() const { return flightAltitude_; }

        /// Where the frustum's bottom (screen-down) and top (screen-up) edges cross a
        /// horizontal plane. The lens is tilted, so the ground plane and the flight plane give
        /// answers metres apart, and which one a system wants is not interchangeable:
        ///   * flight plane - spawning, bullet culling, clamping the player. These live at
        ///     altitude, and using the ground's edge would spawn enemies already on screen.
        ///   * ground plane  - debris recycling. Debris ends up on the ground, and using the
        ///     flight plane's edge would delete pieces while the player can still see them.
        float GroundNearZ() const { return EdgeZ(0.0f, 1.0f); }
        float GroundFarZ() const { return EdgeZ(0.0f, -1.0f); }
        float FlightNearZ() const { return EdgeZ(flightAltitude_, 1.0f); }
        float FlightFarZ() const { return EdgeZ(flightAltitude_, -1.0f); }

        /// screenDown = +1 for the bottom edge, -1 for the top edge.
        float EdgeZ(float planeY, float screenDown) const;

    private:
        FScreenConfig config_{};
        float centreZ_ = 0.0f;
        float flightAltitude_ = 7.0f;
    };
}
