#pragma once

#include <functional>
#include <memory>
#include <glm/glm.hpp>

#include "Engine/Runtime/Subsystems/NextPhysicsTypes.hpp"

enum class ECharacterGroundState
{
    OnGround,
    OnSteepGround,
    NotSupported,
    InAir,
};

struct FCharacterControllerSettings
{
    float height = 1.75f;
    float radius = 0.3f;
    float maxSlopeAngle = 50.0f;
    float maxStepHeight = 0.35f;
    float mass = 70.0f;
    float maxStrength = 100.0f;
    float padding = 0.02f;
    glm::vec3 initialPosition{0.0f, 1.0f, 0.0f};
};

class INextCharacterControllerBackend
{
public:
    virtual ~INextCharacterControllerBackend() = default;
    virtual void Update(const glm::vec3& inputDirection, float speed, bool jump, float deltaSeconds) = 0;
    /// Sets the character velocity for this step exactly as given (the game integrates gravity,
    /// jump, hover and launch itself), then runs ExtendedUpdate. When inheritGround is true and the
    /// character is on ground, the ground body velocity at the contact point is added, so a
    /// character riding a kinematic platform or a rotating disc keeps its footing.
    virtual void UpdateWithVelocity(const glm::vec3& worldVelocity, bool inheritGround, float deltaSeconds) = 0;
    /// Velocity of the body the character is standing on, at the contact point (zero when in air).
    virtual glm::vec3 GetGroundVelocity() const = 0;
    virtual glm::vec3 GetGroundNormal() const = 0;
    virtual NextBodyID GetGroundBodyID() const = 0;
    /// Changes the foot-anchored character height. Growing the shape fails when
    /// the added volume is obstructed; on failure the previous shape is kept.
    virtual bool TrySetHeight(float height) = 0;
    /// Moves the foot-anchored character to an already validated position and
    /// refreshes contacts. Intended for scripted traversal/teleport actions.
    virtual void SetPosition(const glm::vec3& position) = 0;
    virtual glm::vec3 GetPosition() const = 0;
    virtual glm::vec3 GetLinearVelocity() const = 0;
    virtual ECharacterGroundState GetGroundState() const = 0;
    virtual float GetHeight() const = 0;
    virtual bool IsValid() const = 0;
};

using FCharacterControllerFactory = std::function<std::unique_ptr<INextCharacterControllerBackend>(
    const FCharacterControllerSettings&)>;
