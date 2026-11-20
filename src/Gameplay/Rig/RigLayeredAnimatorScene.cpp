#include "Engine/Common/CoreMinimal.hpp"
#include "Gameplay/Rig/RigLayeredAnimator.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/RigAsset.hpp"

namespace NextGameplay
{
    void FRigLayeredAnimator::Bind(const Assets::FRigAsset* asset, std::vector<Assets::Node*> boneNodes,
                                   Assets::Node* root)
    {
        asset_ = asset;
        boneNodes_ = std::move(boneNodes);
        root_ = root;
        ResetBinding(asset, boneNodes_.size());
    }

    void FRigLayeredAnimator::ApplyPose()
    {
        if (!asset_ || boneNodes_.empty())
        {
            return;
        }
        for (size_t i = 0; i < workingPose_.bones.size(); ++i)
        {
            Assets::Node* node = boneNodes_[i];
            if (!node)
            {
                continue;
            }
            const Assets::FRigBone& bone = asset_->bones[i];
            const FRigBonePose& pose = workingPose_.bones[i];
            node->Translation() = bone.bindT + bone.bindR * (bone.bindS * pose.t);
            node->Rotation() = glm::normalize(bone.bindR * pose.r);
            node->Scale() = bone.bindS * pose.s;
        }
        if (root_)
        {
            root_->RecalcTransform(true);
        }
    }

    void FRigLayeredAnimator::Update(float deltaSeconds)
    {
        if (!IsBound())
        {
            return;
        }
        Evaluate(deltaSeconds);
        Apply();
    }

    void FRigLayeredAnimator::Apply()
    {
        if (IsBound())
        {
            ApplyPose();
        }
    }

}
