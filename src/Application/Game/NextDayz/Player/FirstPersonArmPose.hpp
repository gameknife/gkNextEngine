#pragma once

#include "Gameplay/Rig/RigPoseOps.h"

#include <array>
#include <string_view>

namespace NextDayz
{
    struct FFirstPersonArmBones
    {
        int32_t upperArmL = -1, foreArmL = -1, handL = -1;
        int32_t upperArmR = -1, foreArmR = -1, handR = -1;
    };

    class FFirstPersonArmPose
    {
    public:
        void Solve(const Assets::FRigDefinition& rig, const FFirstPersonArmBones& bones, std::string_view weaponId,
                   const glm::mat4& weaponInEye, float weaponScale, float viewModelScale, bool supportHand);
        glm::mat4 Forearm(int32_t bone) const;
        static glm::vec3 ForearmScale(const glm::mat4& matrix);
        std::array<glm::mat4, 2> hands;
        float gripError = 0.f;

    private:
        NextGameplay::FRigPose pose_;
        std::vector<glm::mat4> transforms_;
        glm::mat4 bodyInWeapon_{1.f};
    };
}
