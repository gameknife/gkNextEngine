#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextPhysics/JoltCharacter.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace JPH;

namespace
{
    RefConst<Shape> CreateFootAnchoredCharacterShape(float height, float radius)
    {
        const float cylinderHalfHeight = (height - 2.0f * radius) * 0.5f;
        RefConst<Shape> capsule = new CapsuleShape(std::max(cylinderHalfHeight, 0.01f), radius);
        return new RotatedTranslatedShape(Vec3(0, height * 0.5f, 0), Quat::sIdentity(), capsule);
    }

    class FJoltCharacterControllerBackend final : public INextCharacterControllerBackend
    {
    public:
        FJoltCharacterControllerBackend(PhysicsSystem& physicsSystem, TempAllocator& tempAllocator,
                                        const FCharacterControllerSettings& settings)
            : physicsSystem_(physicsSystem), tempAllocator_(tempAllocator), height_(settings.height),
              radius_(settings.radius), padding_(settings.padding)
        {
            RefConst<Shape> shape = CreateFootAnchoredCharacterShape(height_, radius_);

            CharacterVirtualSettings characterSettings;
            characterSettings.mShape = shape;
            characterSettings.mMaxSlopeAngle = DegreesToRadians(settings.maxSlopeAngle);
            characterSettings.mMaxStrength = settings.maxStrength;
            characterSettings.mCharacterPadding = settings.padding;
            characterSettings.mPenetrationRecoverySpeed = 1.0f;
            characterSettings.mPredictiveContactDistance = 0.1f;
            characterSettings.mSupportingVolume = Plane(Vec3::sAxisY(), -settings.radius);
            characterSettings.mMass = settings.mass;

            character_ = new CharacterVirtual(
                &characterSettings,
                RVec3(settings.initialPosition.x, settings.initialPosition.y, settings.initialPosition.z),
                Quat::sIdentity(), 0, &physicsSystem_);
            updateSettings_.mStickToFloorStepDown = Vec3(0, -settings.maxStepHeight, 0);
            updateSettings_.mWalkStairsStepUp = Vec3(0, settings.maxStepHeight, 0);
        }

        void Update(const glm::vec3& inputDirection, float speed, bool jump, float deltaSeconds) override
        {
            if (!character_)
            {
                return;
            }

            constexpr float gravity = -15.0f;
            constexpr float jumpSpeed = 6.0f;
            const bool onGround = character_->GetGroundState() == CharacterVirtual::EGroundState::OnGround;
            float verticalVelocity = velocity_.y;
            if (onGround)
            {
                verticalVelocity = jump ? jumpSpeed : 0.0f;
            }
            else
            {
                verticalVelocity += gravity * deltaSeconds;
            }

            const Vec3 newVelocity(inputDirection.x * speed, verticalVelocity, inputDirection.z * speed);
            character_->SetLinearVelocity(newVelocity);
            velocity_ = glm::vec3(newVelocity.GetX(), newVelocity.GetY(), newVelocity.GetZ());

            const BroadPhaseLayerFilter& broadPhaseFilter =
                physicsSystem_.GetDefaultBroadPhaseLayerFilter(NextLayers::MOVING);
            const ObjectLayerFilter& objectLayerFilter =
                physicsSystem_.GetDefaultLayerFilter(NextLayers::MOVING);
            character_->ExtendedUpdate(deltaSeconds, Vec3(0, gravity, 0), updateSettings_,
                                       broadPhaseFilter, objectLayerFilter, {}, {}, tempAllocator_);
        }

        void UpdateWithVelocity(const glm::vec3& worldVelocity, bool inheritGround, float deltaSeconds) override
        {
            if (!character_)
            {
                return;
            }

            // Jolt's CharacterVirtualTest recipe: refresh the ground velocity, then keep the part
            // of the character's own velocity that is not already supplied by the platform. Without
            // this a character standing on a moving kinematic body is left behind by it.
            Vec3 newVelocity(worldVelocity.x, worldVelocity.y, worldVelocity.z);
            if (inheritGround)
            {
                character_->UpdateGroundVelocity();
                const Vec3 groundVelocity = character_->GetGroundVelocity();
                if (character_->GetGroundState() == CharacterVirtual::EGroundState::OnGround)
                {
                    // Only inherit upward platform motion: a descending platform must not drag the
                    // character through it, gravity already carries them down onto it.
                    const float inheritedY = std::max(groundVelocity.GetY(), 0.0f);
                    newVelocity += Vec3(groundVelocity.GetX(), 0.0f, groundVelocity.GetZ());
                    if (newVelocity.GetY() < inheritedY)
                    {
                        newVelocity.SetY(inheritedY);
                    }
                }
            }

            character_->SetLinearVelocity(newVelocity);
            velocity_ = glm::vec3(newVelocity.GetX(), newVelocity.GetY(), newVelocity.GetZ());

            const BroadPhaseLayerFilter& broadPhaseFilter =
                physicsSystem_.GetDefaultBroadPhaseLayerFilter(NextLayers::MOVING);
            const ObjectLayerFilter& objectLayerFilter =
                physicsSystem_.GetDefaultLayerFilter(NextLayers::MOVING);
            // The game already integrated gravity into worldVelocity; passing zero here keeps
            // ExtendedUpdate's stick-to-floor / walk-stairs behaviour without double gravity.
            character_->ExtendedUpdate(deltaSeconds, Vec3::sZero(), updateSettings_, broadPhaseFilter,
                                       objectLayerFilter, {}, {}, tempAllocator_);
        }

        glm::vec3 GetGroundVelocity() const override
        {
            if (!character_ || character_->GetGroundState() != CharacterVirtual::EGroundState::OnGround)
            {
                return glm::vec3(0.0f);
            }
            const Vec3 velocity = character_->GetGroundVelocity();
            return glm::vec3(velocity.GetX(), velocity.GetY(), velocity.GetZ());
        }

        glm::vec3 GetGroundNormal() const override
        {
            if (!character_)
            {
                return glm::vec3(0.0f, 1.0f, 0.0f);
            }
            const Vec3 normal = character_->GetGroundNormal();
            return glm::vec3(normal.GetX(), normal.GetY(), normal.GetZ());
        }

        NextBodyID GetGroundBodyID() const override
        {
            if (!character_)
            {
                return NextBodyID();
            }
            const BodyID ground = character_->GetGroundBodyID();
            return ground.IsInvalid() ? NextBodyID() : NextBodyID(ground.GetIndexAndSequenceNumber());
        }

        bool TrySetHeight(float height) override
        {
            constexpr float kMinCylinderHeight = 0.02f;
            constexpr float kSameHeightEpsilon = 1.0e-4f;
            if (!character_ || !std::isfinite(height) || height < 2.0f * radius_ + kMinCylinderHeight)
            {
                return false;
            }
            if (std::abs(height - height_) <= kSameHeightEpsilon)
            {
                return true;
            }

            RefConst<Shape> shape = CreateFootAnchoredCharacterShape(height, radius_);
            const BroadPhaseLayerFilter& broadPhaseFilter =
                physicsSystem_.GetDefaultBroadPhaseLayerFilter(NextLayers::MOVING);
            const ObjectLayerFilter& objectLayerFilter =
                physicsSystem_.GetDefaultLayerFilter(NextLayers::MOVING);
            const float maxPenetrationDepth =
                height < height_ ? std::numeric_limits<float>::max() : std::max(padding_, 1.0e-3f);
            if (!character_->SetShape(shape, maxPenetrationDepth, broadPhaseFilter, objectLayerFilter, {}, {},
                                      tempAllocator_))
            {
                return false;
            }

            height_ = height;
            character_->RefreshContacts(broadPhaseFilter, objectLayerFilter, {}, {}, tempAllocator_);
            return true;
        }

        void SetPosition(const glm::vec3& position) override
        {
            if (!character_)
            {
                return;
            }
            character_->SetPosition(RVec3(position.x, position.y, position.z));
            character_->SetLinearVelocity(Vec3::sZero());
            velocity_ = glm::vec3(0.0f);

            const BroadPhaseLayerFilter& broadPhaseFilter =
                physicsSystem_.GetDefaultBroadPhaseLayerFilter(NextLayers::MOVING);
            const ObjectLayerFilter& objectLayerFilter =
                physicsSystem_.GetDefaultLayerFilter(NextLayers::MOVING);
            character_->RefreshContacts(broadPhaseFilter, objectLayerFilter, {}, {}, tempAllocator_);
        }

        glm::vec3 GetPosition() const override
        {
            if (!character_)
            {
                return glm::vec3(0.0f);
            }
            const RVec3 position = character_->GetPosition();
            return glm::vec3(static_cast<float>(position.GetX()),
                             static_cast<float>(position.GetY()),
                             static_cast<float>(position.GetZ()));
        }

        glm::vec3 GetLinearVelocity() const override { return velocity_; }

        ECharacterGroundState GetGroundState() const override
        {
            if (!character_)
            {
                return ECharacterGroundState::InAir;
            }
            switch (character_->GetGroundState())
            {
            case CharacterVirtual::EGroundState::OnGround: return ECharacterGroundState::OnGround;
            case CharacterVirtual::EGroundState::OnSteepGround: return ECharacterGroundState::OnSteepGround;
            case CharacterVirtual::EGroundState::NotSupported: return ECharacterGroundState::NotSupported;
            case CharacterVirtual::EGroundState::InAir: return ECharacterGroundState::InAir;
            }
            return ECharacterGroundState::InAir;
        }

        float GetHeight() const override { return height_; }
        bool IsValid() const override { return character_ != nullptr; }

    private:
        PhysicsSystem& physicsSystem_;
        TempAllocator& tempAllocator_;
        Ref<CharacterVirtual> character_;
        CharacterVirtual::ExtendedUpdateSettings updateSettings_;
        glm::vec3 velocity_{0.0f};
        float height_ = 0.0f;
        float radius_ = 0.0f;
        float padding_ = 0.0f;
    };
}

std::unique_ptr<INextCharacterControllerBackend> Modules::Physics::MakeJoltCharacter(
    JPH::PhysicsSystem& physicsSystem, JPH::TempAllocator& tempAllocator,
    const FCharacterControllerSettings& settings)
{
    return std::make_unique<FJoltCharacterControllerBackend>(physicsSystem, tempAllocator, settings);
}
