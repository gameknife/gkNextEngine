#include "Engine/Common/CoreMinimal.hpp"

#include "WorldAnchorRegistry.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

namespace NextDayz
{
    namespace
    {
        void DisableMarker(Assets::Node* node, NextPhysics* physics)
        {
            if (!node)
            {
                return;
            }
            if (auto render = node->GetComponent<Runtime::RenderComponent>())
            {
                render->SetVisible(false);
                render->SetRayCastVisible(false);   // hidden markers must not stop bullets or the nav probes
            }
            if (auto component = node->GetComponent<Runtime::PhysicsComponent>(); component && physics)
            {
                physics->SetBodyActive(component->GetPhysicsBody(), false);
            }
            for (const auto& child : node->Children())
            {
                DisableMarker(child.get(), physics);
            }
        }
    }

    void WorldAnchorRegistry::Scan(NextEngine& engine)
    {
        Assets::Scene& scene = engine.GetScene();
        Scan(scene.Nodes(), engine.GetPhysicsEngine());
        scene.MarkDirty();
        SPDLOG_INFO("[NextDayz] registered {} world anchors (generation {})", anchors_.size(), generation_);
    }

    void WorldAnchorRegistry::Scan(std::span<const std::shared_ptr<Assets::Node>> nodes, NextPhysics* physics)
    {
        Clear();
        for (const auto& node : nodes)
        {
            if (!node || !IsWorldAnchor(node->GetName()))
            {
                continue;
            }
            node->RecalcTransform(true);
            auto anchor = ReadWorldAnchor(node->GetName(), node->GetMetadata(), node->WorldTransform(), node->GetInstanceId());
            if (!anchor)
            {
                continue;
            }
            const bool hideMarker = anchor->type != EWorldAnchorType::Well;
            anchors_.push_back(std::move(*anchor));
            if (hideMarker)
            {
                DisableMarker(node.get(), physics);
            }
        }
    }

    void WorldAnchorRegistry::Clear()
    {
        anchors_.clear();
        ++generation_;
        if (generation_ == 0)
        {
            ++generation_;
        }
    }

    const FWorldAnchor* WorldAnchorRegistry::Resolve(FWorldAnchorHandle handle) const
    {
        if (!handle.IsValid() || handle.generation != generation_ || handle.index >= anchors_.size())
        {
            return nullptr;
        }
        return &anchors_[handle.index];
    }

    std::vector<FWorldAnchorHandle> WorldAnchorRegistry::Find(EWorldAnchorType type) const
    {
        std::vector<FWorldAnchorHandle> result;
        for (size_t index = 0; index < anchors_.size(); ++index)
        {
            if (anchors_[index].type == type)
            {
                result.push_back({static_cast<uint32_t>(index), generation_});
            }
        }
        return result;
    }

    size_t WorldAnchorRegistry::Count(EWorldAnchorType type) const
    {
        return static_cast<size_t>(std::count_if(anchors_.begin(), anchors_.end(),
            [type](const FWorldAnchor& anchor) { return anchor.type == type; }));
    }
}
