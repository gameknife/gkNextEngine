#include "Engine/Common/CoreMinimal.hpp"
#include "WeaponAttachmentVisuals.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

namespace NextDayz
{
    void WeaponAttachmentVisuals::Bind(Assets::Scene& scene, const std::shared_ptr<Assets::Node>& parent,
                                       const FAttachmentVisualAssets& assets, std::string_view prefix)
    {
        Clear();
        if (!assets.ready || !parent) return;
        scene_ = &scene;
        parent_ = parent;
        assets_ = assets;
        for (size_t slot = 0; slot < nodes_.size(); ++slot)
        {
            auto& node = nodes_[slot];
            node = Assets::SceneBuilder::CreateRenderNode(fmt::format("{}/attachment_{}", prefix, slot),
                glm::vec3(0.0f), glm::vec3(0.0f), scene.GenerateInstanceId(), assets.models[0], assets.materials[0],
                false, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
            auto physics = std::make_shared<Runtime::PhysicsComponent>();
            physics->SetMobility(Runtime::ENodeMobility::Dynamic);
            node->AddComponent(physics);
            node->GetComponent<Runtime::RenderComponent>()->SetVisible(false);
            node->SetParent(parent);
            // GenerateInstanceId advances when the node is registered. Reserve
            // each hidden node now, otherwise all five mounts share one ID.
            scene.AddNode(node);
            inScene_[slot] = true;
        }
    }

    void WeaponAttachmentVisuals::Clear()
    {
        nodes_ = {};
        indices_.fill(-1);
        inScene_.fill(false);
        scene_ = nullptr;
        parent_.reset();
    }

    int WeaponAttachmentVisuals::VisibleCount() const
    {
        return static_cast<int>(std::count_if(nodes_.begin(), nodes_.end(), [](const auto& node)
        { return node && node->template GetComponent<Runtime::RenderComponent>()->GetVisible(); }));
    }

    void WeaponAttachmentVisuals::Update(std::string_view weaponId, const FWeaponAttachments& attachments, bool visible)
    {
        if (!scene_) return;
        const auto points = WeaponModelPoints(weaponId);
        for (size_t slot = 0; slot < nodes_.size(); ++slot)
        {
            auto& node = nodes_[slot];
            if (!node) continue;
            const auto type = attachments[slot];
            const int index = type == EWeaponAttachment::None
                ? (slot == 0 && weaponId == "svd" ? 8 : -1) : static_cast<int>(type) - 1;
            auto render = node->GetComponent<Runtime::RenderComponent>();
            if (index >= 0 && index != indices_[slot])
            {
                render->SetModelId(assets_.models[index]);
                render->SetMaterials(assets_.materials[index]);
                scene_->MarkDirty();
            }
            indices_[slot] = index;
            const bool show = visible && index >= 0;
            render->SetVisible(show);
            if (show != inScene_[slot])
            {
                if (show)
                {
                    // Scene removal clears the parent relationship. Rebinding
                    // is essential when returning from ADS/scope or a switch.
                    node->SetParent(parent_.lock());
                    scene_->AddNode(node);
                }
                else scene_->RemoveNodeByInstanceId(node->GetInstanceId());
                inScene_[slot] = show;
                scene_->MarkDirty();
            }
            const FWeaponDef* weapon = FindWeaponDef(weaponId);
            const bool pistol = weapon && weapon->IsSidearm();
            glm::vec3 position(0.0f);
            switch (static_cast<EAttachmentSlot>(slot))
            {
            case EAttachmentSlot::Optic:
                if (index != 8) position = {0.0f, WeaponOpticRailHeight(weaponId), WeaponOpticRailForward(weaponId)};
                break;
            case EAttachmentSlot::Grip:
                position = {0.0f, points.boreHeight - 0.040f, weaponId == "svd" ? 0.36f : 0.28f};
                break;
            case EAttachmentSlot::Magazine:
                if (weaponId == "aps") position = {0.0f, -0.135f, -0.05f};
                else if (weaponId == "svt40") position = {0.0f, -0.14f, 0.09f};
                else position = {0.0f, pistol ? -0.115f : weaponId == "svd" ? -0.15f : -0.19f, pistol ? -0.04f : 0.15f};
                break;
            case EAttachmentSlot::Muzzle: position = {0.0f, points.boreHeight, points.muzzleForward}; break;
            case EAttachmentSlot::Tactical:
                position = {pistol ? 0.0f : -0.045f, points.boreHeight - (pistol ? 0.05f : 0.015f), pistol ? 0.11f : 0.30f};
                break;
            default: break;
            }
            node->Translation() = position;
            node->Scale() = glm::vec3(show ? (pistol && slot == static_cast<size_t>(EAttachmentSlot::Muzzle) ? 0.7f : 1.0f) : 0.0f);
            node->RecalcTransform(true);
        }
    }
}
