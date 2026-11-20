#include "Engine/Common/CoreMinimal.hpp"
#include "FirstPersonArmPose.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace NextDayz
{
    namespace
    {
        // Wrist centres of the two hand models (nextdayz_fps_hands.scad).
        const glm::vec3 kWristGrip(-0.012f, -0.030f, -0.070f);
        const glm::vec3 kWristSupport(0.012f, -0.062f, -0.050f);

        // A hand model in weapon-model space (+X left, +Y up, +Z muzzle).
        struct FHandPlacement
        {
            glm::vec3 position{0.0f};
            float rakeDegrees = 0.0f; // grip axis tipped back from vertical
        };

        // Right hand: on the pistol grip / wrist the gun is built around
        // (nextdayz_weapons.scad origin), its axis raked back like the grip.
        FHandPlacement GripPlacement(std::string_view weaponId)
        {
            if (weaponId == "pistol" || weaponId == "aps") return {{0.0f, -0.020f, -0.021f}, 12.0f};
            if (weaponId == "svd") return {{0.0f, -0.022f, -0.012f}, 16.0f};
            if (weaponId == "mosin") return {{0.0f, 0.004f, -0.035f}, 35.0f};   // straight wrist
            if (weaponId == "svt40") return {{0.0f, 0.002f, -0.034f}, 32.0f};   // semi-pistol wrist
            if (weaponId == "shotgun" || weaponId == "toz34") return {{0.0f, 0.002f, -0.030f}, 35.0f}; // semi-pistol grip
            return {{0.0f, -0.020f, -0.006f}, 13.0f};                          // AK-74 family
        }

        // Left hand: under the handguard / forend; under the magazine base
        // (cup and saucer) for the pistol.
        FHandPlacement SupportPlacement(std::string_view weaponId)
        {
            if (weaponId == "pistol" || weaponId == "aps") return {{0.004f, -0.062f, -0.012f}, 0.0f};
            if (weaponId == "svd") return {{0.0f, 0.062f, 0.340f}, 0.0f};
            if (weaponId == "mosin") return {{0.0f, 0.048f, 0.280f}, 0.0f};
            if (weaponId == "svt40") return {{0.0f, 0.052f, 0.340f}, 0.0f};
            if (weaponId == "shotgun") return {{0.0f, 0.040f, 0.250f}, 0.0f};
            if (weaponId == "toz34") return {{0.0f, 0.036f, 0.250f}, 0.0f};
            if (weaponId == "aks74u") return {{0.0f, 0.054f, 0.270f}, 0.0f};  // short handguard
            if (weaponId == "bizon") return {{0.0f, 0.030f, 0.290f}, 0.0f};   // around the helical drum
            return {{0.0f, 0.058f, 0.300f}, 0.0f};
        }

        glm::mat4 PlacementMatrix(const FHandPlacement& placement)
        {
            return glm::translate(glm::mat4(1.0f), placement.position) *
                   glm::mat4_cast(glm::angleAxis(glm::radians(placement.rakeDegrees), glm::vec3(1.0f, 0.0f, 0.0f)));
        }

        // Eye space (camera-local, +X left, +Y up, +Z forward), rig units.
        struct FArmStance
        {
            glm::vec3 shoulderR;
            glm::vec3 shoulderL;
            glm::vec3 elbowPoleR; // relative to the shoulder
            glm::vec3 elbowPoleL;
        };

        FArmStance StanceFor(std::string_view weaponId)
        {
            // Pistol: isosceles stance, both shoulders square to the target.
            if (weaponId == "pistol" || weaponId == "aps")
                return {{-0.165f, -0.215f, -0.03f}, {0.165f, -0.215f, -0.03f},
                        {-0.30f, -0.55f, 0.15f}, {0.30f, -0.55f, 0.15f}};
            // Long guns: bladed body, firing shoulder back under the stock, the
            // support shoulder rolled forward. Firing elbow out and down (its
            // forearm stays below the sight picture); support
            // elbow well out to the left, so its forearm climbs in from the lower
            // left corner instead of hiding behind the receiver.
            return {{-0.185f, -0.205f, -0.10f}, {0.200f, -0.220f, 0.06f},
                    {-0.40f, -0.45f, 0.05f}, {0.50f, -0.35f, 0.10f}};
        }
    }

    void FFirstPersonArmPose::Solve(const Assets::FRigDefinition& rig, const FFirstPersonArmBones& bones,
                                  std::string_view weaponId, const glm::mat4& weaponInEye, float weaponScale,
                                  float viewModelScale, bool supportHand)
    {
        hands = {PlacementMatrix(GripPlacement(weaponId)), PlacementMatrix(SupportPlacement(weaponId))};
        // The weapon in eye space at body scale: the rig carries its weapon at
        // weaponScale, the view model at viewModelScale.
        const glm::mat4 weaponInBody =
            glm::scale(glm::mat4(1.0f), glm::vec3(weaponScale / viewModelScale)) * weaponInEye;
        const glm::vec3 wristR(weaponInBody * hands[0] * glm::vec4(kWristGrip, 1.0f));
        const glm::vec3 wristL(weaponInBody * hands[1] * glm::vec4(kWristSupport, 1.0f));

        using namespace NextGameplay;
        pose_.Reset(rig.bones.size());
        ComputeRigComponentTransforms(rig, pose_, transforms_);
        const FArmStance stance = StanceFor(weaponId);
        SetRigBoneComponentPosition(rig, pose_, transforms_, bones.upperArmR, stance.shoulderR);
        SetRigBoneComponentPosition(rig, pose_, transforms_, bones.upperArmL, stance.shoulderL);
        SolveRigTwoBoneIK(rig, pose_, transforms_, bones.upperArmR, bones.foreArmR, bones.handR, wristR,
                          stance.shoulderR + stance.elbowPoleR);
        gripError = glm::length(RigBonePosition(transforms_, bones.handR) - wristR);
        if (supportHand)
        {
            SolveRigTwoBoneIK(rig, pose_, transforms_, bones.upperArmL, bones.foreArmL, bones.handL, wristL,
                              stance.shoulderL + stance.elbowPoleL);
            gripError = std::max(gripError, glm::length(RigBonePosition(transforms_, bones.handL) - wristL));
        }

        // Forearm parts hang under the view model too, i.e. in weapon-model space.
        bodyInWeapon_ = glm::inverse(weaponInBody);
    }

    glm::mat4 FFirstPersonArmPose::Forearm(int32_t bone) const
    {
        return bodyInWeapon_ * transforms_[bone];
    }

    glm::vec3 FFirstPersonArmPose::ForearmScale(const glm::mat4& matrix)
    {
        // Slim the third-person forearm block for the close view.
        return glm::length(glm::vec3(matrix[0])) * glm::vec3(0.58f, 1.0f, 0.58f);
    }
}
