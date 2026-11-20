#include "Engine/Common/CoreMinimal.hpp"
#include "FirstPersonArms.hpp"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <fmt/format.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Gameplay/Rig/RigPoseOps.h"

#include "Application/Game/NextDayz/Data/ItemDefs.hpp"

namespace NextDayz
{
    namespace
    {
        glm::quat RotationOf(const glm::mat4& matrix)
        {
            return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(matrix[0])),
                                                           glm::normalize(glm::vec3(matrix[1])),
                                                           glm::normalize(glm::vec3(matrix[2])))));
        }


    }

    void FirstPersonArms::SetAssets(FFirstPersonArmAssets assets)
    {
        assets_ = std::move(assets);
        bones_ = {};
        valid_ = false;
        if (!assets_.rig) return;
        const Assets::FRigAsset& rig = *assets_.rig;
        bones_.upperArmL = rig.FindBone("bone_upperarm_l");
        bones_.foreArmL = rig.FindBone("bone_forearm_l");
        bones_.handL = rig.FindBone("bone_hand_l");
        bones_.upperArmR = rig.FindBone("bone_upperarm_r");
        bones_.foreArmR = rig.FindBone("bone_forearm_r");
        bones_.handR = rig.FindBone("bone_hand_r");
        valid_ = bones_.upperArmL >= 0 && bones_.foreArmL >= 0 && bones_.handL >= 0 && bones_.upperArmR >= 0 &&
                 bones_.foreArmR >= 0 && bones_.handR >= 0 && assets_.handsReady;
        if (!valid_) SPDLOG_ERROR("[NextDayz] first-person arms: missing arm bones or hand models");
    }

    void FirstPersonArms::Bind(Assets::Scene& scene, const std::shared_ptr<Assets::Node>& viewModel)
    {
        Clear();
        if (!valid_ || !viewModel) return;
        scene_ = &scene;
        parent_ = viewModel;
        const auto create = [&](std::string_view name, uint32_t model, const std::array<uint32_t, 16>& materials)
        {
            auto node = Assets::SceneBuilder::CreateRenderNode(name, glm::vec3(0.0f), glm::vec3(0.0f),
                                                               scene.GenerateInstanceId(), model, materials, false,
                                                               glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
            auto physics = std::make_shared<Runtime::PhysicsComponent>();
            physics->SetMobility(Runtime::ENodeMobility::Dynamic);
            node->AddComponent(physics);
            node->SetParent(viewModel);
            // Register now so every part reserves its own instance ID.
            scene.AddNode(node);
            nodes_.push_back(node);
            inScene_.push_back(true);
            return node;
        };
        for (const FFirstPersonArmPart& part : assets_.parts)
        {
            create(fmt::format("nd_viewmodel/arm/{}/{}", part.itemId.empty() ? "body" : part.itemId,
                               assets_.rig->bones[part.bone].name),
                   part.modelId, part.materials);
        }
        hands_[0] = create("nd_viewmodel/arm/hand_grip", assets_.handModels[0], assets_.handMaterials[0]);
        hands_[1] = create("nd_viewmodel/arm/hand_support", assets_.handModels[1], assets_.handMaterials[1]);
    }

    void FirstPersonArms::Clear()
    {
        nodes_.clear();
        inScene_.clear();
        hands_ = {};
        scene_ = nullptr;
        parent_.reset();
        gripError_ = 0.0f;
    }

    void FirstPersonArms::SetClothing(const std::vector<std::string>& equipped)
    {
        equipped_ = equipped;
        coat_ = std::any_of(equipped_.begin(), equipped_.end(), [](const std::string& id)
        {
            const FItemDef* definition = FindItemDef(id);
            return definition && definition->equipSlot == EEquipSlot::Torso;
        });
    }

    bool FirstPersonArms::PartWanted(const FFirstPersonArmPart& part, bool supportHand) const
    {
        // Only forearms: the upper arms are behind the eye and the rig's own
        // hands give way to the first-person hand models.
        if (part.bone != bones_.foreArmL && part.bone != bones_.foreArmR) return false;
        if (part.bone == bones_.foreArmL && !supportHand) return false;
        if (!part.itemId.empty())
            return std::find(equipped_.begin(), equipped_.end(), part.itemId) != equipped_.end();
        return !coat_; // a coat sleeve covers the bare forearm
    }

    int FirstPersonArms::VisibleCount() const
    {
        return static_cast<int>(std::count_if(nodes_.begin(), nodes_.end(), [](const auto& node)
        { return node && node->template GetComponent<Runtime::RenderComponent>()->GetVisible(); }));
    }

    void FirstPersonArms::Update(std::string_view weaponId, const glm::mat4& weaponInEye, float viewModelScale,
                                 bool visible, bool supportHand)
    {
        gripError_ = 0.0f;
        if (!scene_ || nodes_.empty()) return;
        const size_t partCount = assets_.parts.size();
        bool dirty = false;
        for (size_t index = 0; index < nodes_.size(); ++index)
        {
            auto& node = nodes_[index];
            const bool wanted = index < partCount ? PartWanted(assets_.parts[index], supportHand)
                                                  : (index == partCount || supportHand);
            const bool show = visible && wanted;
            auto render = node->GetComponent<Runtime::RenderComponent>();
            if (render->GetVisible() != show) dirty = true;
            render->SetVisible(show);
            if (show != inScene_[index])
            {
                if (show)
                {
                    // Scene removal clears the parent: rebind after ADS scope / switch.
                    node->SetParent(parent_.lock());
                    scene_->AddNode(node);
                }
                else scene_->RemoveNodeByInstanceId(node->GetInstanceId());
                inScene_[index] = show;
                dirty = true;
            }
            if (!show) node->Scale() = glm::vec3(0.0f);
        }
        if (dirty) scene_->MarkDirty();
        if (!visible || viewModelScale <= 0.0f) return;

        armPose_.Solve(*assets_.rig, bones_, weaponId, weaponInEye, assets_.weaponScale, viewModelScale, supportHand);
        gripError_ = armPose_.gripError;
        for (int side = 0; side < 2; ++side)
        {
            Assets::Node& hand = *hands_[side];
            const glm::mat4& placement = armPose_.hands[side];
            hand.Translation() = glm::vec3(placement[3]);
            hand.Rotation() = RotationOf(placement);
            hand.Scale() = glm::vec3(1.0f);
            hand.RecalcTransform(true);
        }

        for (size_t index = 0; index < partCount; ++index)
        {
            if (!inScene_[index]) continue;
            const glm::mat4 local = armPose_.Forearm(assets_.parts[index].bone);
            Assets::Node& node = *nodes_[index];
            node.Translation() = glm::vec3(local[3]);
            node.Rotation() = RotationOf(local);
            node.Scale() = FFirstPersonArmPose::ForearmScale(local);
            node.RecalcTransform(true);
        }
    }
}
