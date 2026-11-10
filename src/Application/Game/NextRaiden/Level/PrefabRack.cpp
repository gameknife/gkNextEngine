#include "Application/Game/NextRaiden/Level/PrefabRack.hpp"

#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"

namespace NextRaiden
{
    namespace
    {
        constexpr std::string_view kPrefabPrefix = "nr_pf_";

        bool IsPrefabName(const std::string& name)
        {
            return name.rfind(kPrefabPrefix, 0) == 0;
        }
    }

    glm::vec3 FPrefabRack::ParkPosition(int slot)
    {
        // Behind the start line and spread out so nothing overlaps while parked. Parked
        // objects are invisible, so this only has to stay out of the camera and out of the
        // way of the scene's vertical extent.
        return glm::vec3(static_cast<float>(slot % 32) * 3.0f - 48.0f, 0.0f,
                         260.0f + static_cast<float>(slot / 32) * 6.0f);
    }

    void FPrefabRack::Index(const std::vector<std::shared_ptr<Assets::Node>>& nodes)
    {
        prefabs_.clear();
        nextInstanceId_ = 1;

        for (const std::shared_ptr<Assets::Node>& node : nodes)
        {
            if (!node)
            {
                continue;
            }
            nextInstanceId_ = std::max(nextInstanceId_, node->GetInstanceId() + 1u);

            const std::string& name = node->GetName();
            if (!IsPrefabName(name))
            {
                continue;
            }
            if (!prefabs_.emplace(name, node).second)
            {
                // Two racks, or a prefab module called twice. The pool would silently use
                // whichever came first, so say so instead.
                SPDLOG_WARN("[NextRaiden] duplicate prefab '{}' in the rack; keeping the first", name);
                continue;
            }
            // The rack itself must never draw or collide: it is a parts bin, not scenery.
            Assets::NodeUtils::SetVisibleRecursive(node, false);
            Assets::NodeUtils::SetRayCastVisibleRecursive(node, false);
        }

        SPDLOG_INFO("[NextRaiden] prefab rack: {} prefabs indexed", prefabs_.size());
    }

    std::shared_ptr<Assets::Node> FPrefabRack::CloneSubtree(const Assets::Node& source,
                                                            const std::shared_ptr<Assets::Node>& parent, bool isRoot,
                                                            const glm::vec3& park,
                                                            std::vector<std::shared_ptr<Assets::Node>>& nodes)
    {
        // The rack's own placement is baked into the prefab root's local transform, so the
        // root is re-seated at the park position; children keep the local pose that makes
        // the object look like itself.
        const glm::vec3 translation = isRoot ? park : source.Translation();
        const glm::quat rotation = isRoot ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : source.Rotation();

        std::shared_ptr<Assets::Node> clone =
            Assets::Node::CreateNode(source.GetName(), translation, rotation, source.Scale(), nextInstanceId_++);
        if (parent)
        {
            clone->SetParent(parent);
        }

        if (const auto* render = source.GetComponent<Runtime::RenderComponent>())
        {
            auto copy = std::make_shared<Runtime::RenderComponent>();
            copy->SetModelId(render->GetModelId());
            copy->SetMaterials(render->GetMaterials());
            copy->SetCastShadows(render->GetCastShadows());
            copy->SetReceiveGI(render->GetReceiveGI());
            copy->SetRayTraceVisible(render->GetRayTraceVisible());
            // Parked and silent until the pool hands it out. rayCastVisible stays false so
            // Scene::EnsureNodePhysicsBody does not give every pooled object an implicit
            // static collider at its parking spot (design section 5.3).
            copy->SetVisible(false);
            copy->SetRayCastVisible(false);
            clone->AddComponent(std::move(copy));
        }

        nodes.push_back(clone);

        for (const std::shared_ptr<Assets::Node>& child : source.Children())
        {
            if (child)
            {
                CloneSubtree(*child, clone, false, park, nodes);
            }
        }
        return clone;
    }

    std::vector<std::shared_ptr<Assets::Node>> FPrefabRack::Clone(const std::string& prefab, int count,
                                                                  std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                                                  const glm::vec3& park)
    {
        std::vector<std::shared_ptr<Assets::Node>> clones;
        if (count <= 0)
        {
            return clones;
        }

        const auto found = prefabs_.find(prefab);
        if (found == prefabs_.end())
        {
            SPDLOG_WARN("[NextRaiden] prefab '{}' is not in the stage rack; its pool stays empty", prefab);
            return clones;
        }

        clones.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            clones.push_back(CloneSubtree(*found->second, nullptr, true, park, nodes));
        }
        return clones;
    }
}
