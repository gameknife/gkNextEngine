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
        // Wrist centres of the two hand models (nextdayz_fps_hands.scad).
        const glm::vec3 kWristGrip(-0.012f, -0.030f, -0.070f);
        const glm::vec3 kWristSupport(0.012f, -0.062f, -0.050f);
        // The rig's forearm block is ~11 cm thick: right for a figure seen
        // from metres away, a plank next to the eye. Slim its cross-section.
        const glm::vec3 kForearmSlim(0.58f, 1.0f, 0.58f);

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
            if (weaponId == "pistol") return {{0.0f, -0.020f, -0.021f}, 12.0f};
            if (weaponId == "svd") return {{0.0f, -0.022f, -0.012f}, 16.0f};
            if (weaponId == "mosin") return {{0.0f, 0.004f, -0.035f}, 35.0f};   // straight wrist
            if (weaponId == "shotgun") return {{0.0f, 0.002f, -0.030f}, 35.0f}; // semi-pistol grip
            return {{0.0f, -0.020f, -0.006f}, 13.0f};                          // AK-74
        }

        // Left hand: under the handguard / forend; under the magazine base
        // (cup and saucer) for the pistol.
        FHandPlacement SupportPlacement(std::string_view weaponId)
        {
            if (weaponId == "pistol") return {{0.004f, -0.062f, -0.012f}, 0.0f};
            if (weaponId == "svd") return {{0.0f, 0.062f, 0.340f}, 0.0f};
            if (weaponId == "mosin") return {{0.0f, 0.048f, 0.280f}, 0.0f};
            if (weaponId == "shotgun") return {{0.0f, 0.040f, 0.250f}, 0.0f};
            return {{0.0f, 0.058f, 0.300f}, 0.0f};
        }

        glm::mat4 PlacementMatrix(const FHandPlacement& placement)
        {
            return glm::translate(glm::mat4(1.0f), placement.position) *
                   glm::mat4_cast(glm::angleAxis(glm::radians(placement.rakeDegrees), glm::vec3(1.0f, 0.0f, 0.0f)));
        }

        glm::quat RotationOf(const glm::mat4& matrix)
        {
            return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(matrix[0])),
                                                           glm::normalize(glm::vec3(matrix[1])),
                                                           glm::normalize(glm::vec3(matrix[2])))));
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
            if (weaponId == "pistol")
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
        { return node && node->GetComponent<Runtime::RenderComponent>()->GetVisible(); }));
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

        // Hands sit on the gun (weapon-model space, under the view model).
        const glm::mat4 grip = PlacementMatrix(GripPlacement(weaponId));
        const glm::mat4 support = PlacementMatrix(SupportPlacement(weaponId));
        for (int side = 0; side < 2; ++side)
        {
            Assets::Node& hand = *hands_[side];
            const glm::mat4& placement = side == 0 ? grip : support;
            hand.Translation() = glm::vec3(placement[3]);
            hand.Rotation() = RotationOf(placement);
            hand.Scale() = glm::vec3(1.0f);
            hand.RecalcTransform(true);
        }

        // The weapon in eye space at body scale: the rig carries its weapon at
        // weaponScale, the view model at viewModelScale.
        const Assets::FRigAsset& rig = *assets_.rig;
        const glm::mat4 weaponInBody =
            glm::scale(glm::mat4(1.0f), glm::vec3(assets_.weaponScale / viewModelScale)) * weaponInEye;
        const glm::vec3 wristR(weaponInBody * grip * glm::vec4(kWristGrip, 1.0f));
        const glm::vec3 wristL(weaponInBody * support * glm::vec4(kWristSupport, 1.0f));

        using namespace NextGameplay;
        pose_.Reset(rig.bones.size());
        ComputeRigComponentTransforms(rig, pose_, transforms_);
        const FArmStance stance = StanceFor(weaponId);
        SetRigBoneComponentPosition(rig, pose_, transforms_, bones_.upperArmR, stance.shoulderR);
        SetRigBoneComponentPosition(rig, pose_, transforms_, bones_.upperArmL, stance.shoulderL);
        SolveRigTwoBoneIK(rig, pose_, transforms_, bones_.upperArmR, bones_.foreArmR, bones_.handR, wristR,
                          stance.shoulderR + stance.elbowPoleR);
        gripError_ = glm::length(RigBonePosition(transforms_, bones_.handR) - wristR);
        if (supportHand)
        {
            SolveRigTwoBoneIK(rig, pose_, transforms_, bones_.upperArmL, bones_.foreArmL, bones_.handL, wristL,
                              stance.shoulderL + stance.elbowPoleL);
            gripError_ = std::max(gripError_, glm::length(RigBonePosition(transforms_, bones_.handL) - wristL));
        }

        // Forearm parts hang under the view model too, i.e. in weapon-model space.
        const glm::mat4 bodyInWeapon = glm::inverse(weaponInBody);
        for (size_t index = 0; index < partCount; ++index)
        {
            if (!inScene_[index]) continue;
            const glm::mat4 local = bodyInWeapon * transforms_[assets_.parts[index].bone];
            Assets::Node& node = *nodes_[index];
            node.Translation() = glm::vec3(local[3]);
            node.Rotation() = RotationOf(local);
            node.Scale() = glm::length(glm::vec3(local[0])) * kForearmSlim;
            node.RecalcTransform(true);
        }
    }
}
