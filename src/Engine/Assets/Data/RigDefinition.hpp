#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Engine/Assets/Data/AnimationChannel.hpp"

namespace Assets
{
    struct FRigBone
    {
        std::string name;                 // e.g. "bone_torso"
        int32_t parent = -1;              // index into FRigDefinition::bones, root = -1
        glm::vec3 bindT{0.0f};            // bind-pose local TRS (engine space)
        glm::quat bindR{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 bindS{1.0f};
        std::vector<int32_t> children;
    };

    struct FRigChannel
    {
        int32_t bone = -1;
        AnimationChannel<glm::vec3> position;   // pivot-local offset (engine space)
        AnimationChannel<glm::quat> rotation;   // euler keys converted at load time
        AnimationChannel<glm::vec3> scale;
    };

    struct FRigClip
    {
        std::string name;
        float duration = 0.0f;
        bool loop = true;
        std::vector<FRigChannel> channels;
    };

    struct FRigDefinition
    {
        std::vector<FRigBone> bones;      // bones[0] = root, parents before children
        std::vector<FRigClip> clips;

        int32_t FindBone(std::string_view name) const
        {
            for (size_t i = 0; i < bones.size(); ++i)
            {
                if (bones[i].name == name)
                {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        const FRigClip* FindClip(std::string_view name) const
        {
            for (const FRigClip& clip : clips)
            {
                if (clip.name == name)
                {
                    return &clip;
                }
            }
            return nullptr;
        }
    };
} // namespace Assets
