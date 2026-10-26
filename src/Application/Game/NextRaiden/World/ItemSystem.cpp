#include "Application/Game/NextRaiden/World/ItemSystem.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "Application/Game/NextRaiden/Level/PrefabRack.hpp"
#include "Application/Game/NextRaiden/Player/ScrollCamera.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"

namespace NextRaiden
{
    namespace
    {
        constexpr float kCullMargin = 6.0f;

        const char* PrefabForItem(EItemKind kind)
        {
            switch (kind)
            {
            case EItemKind::Power: return Prefabs::kItemPower;
            case EItemKind::Medal: return Prefabs::kItemMedal;
            case EItemKind::Repair: return Prefabs::kItemRepair;
            case EItemKind::Bomb: return Prefabs::kItemBomb;
            case EItemKind::Option: return Prefabs::kItemOption;
            default: return Prefabs::kItemPower;
            }
        }
    }

    void FItemSystem::Create(FPrefabRack& rack, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                             const FItemConfig& config)
    {
        Clear();
        config_ = config;
        if (config_.PoolPerKind <= 0)
        {
            return;
        }

        for (size_t k = 1; k < kinds_.size(); ++k) // index 0 is EItemKind::None
        {
            FKindPool& pool = kinds_[k];
            pool.Nodes = rack.Clone(PrefabForItem(static_cast<EItemKind>(k)), config_.PoolPerKind, nodes,
                                    FPrefabRack::ParkPosition(static_cast<int>(k) + 40));
            pool.Free.resize(pool.Nodes.size());
            for (size_t i = 0; i < pool.Nodes.size(); ++i)
            {
                pool.Free[i] = static_cast<int>(pool.Nodes.size() - 1 - i);
            }
        }
    }

    void FItemSystem::Clear()
    {
        for (FKindPool& pool : kinds_)
        {
            pool.Nodes.clear();
            pool.Free.clear();
        }
        items_.clear();
        aliveCount_ = 0;
    }

    void FItemSystem::Reset()
    {
        for (FItem& item : items_)
        {
            if (item.Active)
            {
                Release(item);
            }
        }
    }

    bool FItemSystem::Spawn(EItemKind kind, const glm::vec3& position)
    {
        if (kind == EItemKind::None || kind == EItemKind::Count)
        {
            return false;
        }
        FKindPool& pool = kinds_[static_cast<size_t>(kind)];
        if (pool.Free.empty())
        {
            return false;
        }
        const int slot = pool.Free.back();
        pool.Free.pop_back();

        FItem* item = nullptr;
        for (FItem& candidate : items_)
        {
            if (!candidate.Active)
            {
                item = &candidate;
                break;
            }
        }
        if (!item)
        {
            items_.push_back(FItem{});
            item = &items_.back();
        }

        item->Kind = kind;
        item->Position = position;
        item->Spin = 0.0f;
        item->Slot = slot;
        item->Active = true;
        ++aliveCount_;

        Assets::NodeUtils::SetVisibleRecursive(pool.Nodes[static_cast<size_t>(slot)], true);
        return true;
    }

    void FItemSystem::Release(FItem& item)
    {
        if (!item.Active)
        {
            return;
        }
        FKindPool& pool = kinds_[static_cast<size_t>(item.Kind)];
        if (item.Slot >= 0 && item.Slot < static_cast<int>(pool.Nodes.size()))
        {
            Assets::NodeUtils::SetVisibleRecursive(pool.Nodes[static_cast<size_t>(item.Slot)], false);
            pool.Free.push_back(item.Slot);
        }
        item.Slot = -1;
        item.Active = false;
        --aliveCount_;
    }

    void FItemSystem::Step(float deltaSeconds, const FScrollCamera& camera, const glm::vec3& playerPosition,
                           std::vector<EItemKind>& outPickups)
    {
        const float cullZ = camera.FlightNearZ() + kCullMargin;

        for (FItem& item : items_)
        {
            if (!item.Active)
            {
                continue;
            }

            item.Spin += config_.SpinDegreesPerSec * deltaSeconds;
            item.Position.z += config_.DriftSpeed * deltaSeconds;

            const glm::vec3 toPlayer = playerPosition - item.Position;
            const float distanceSq = toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z;

            if (distanceSq <= config_.PickupRadius * config_.PickupRadius)
            {
                outPickups.push_back(item.Kind);
                Release(item);
                continue;
            }

            if (distanceSq <= config_.MagnetRadius * config_.MagnetRadius && distanceSq > 1e-4f)
            {
                const float distance = std::sqrt(distanceSq);
                const glm::vec3 direction(toPlayer.x / distance, 0.0f, toPlayer.z / distance);
                item.Position += direction * config_.MagnetSpeed * deltaSeconds;
                item.Position.y = playerPosition.y;
            }

            if (item.Position.z > cullZ)
            {
                Release(item);
            }
        }
    }

    void FItemSystem::Apply()
    {
        for (const FItem& item : items_)
        {
            if (!item.Active || item.Slot < 0)
            {
                continue;
            }
            const FKindPool& pool = kinds_[static_cast<size_t>(item.Kind)];
            const std::shared_ptr<Assets::Node>& node = pool.Nodes[static_cast<size_t>(item.Slot)];
            node->SetTransform(item.Position, glm::angleAxis(glm::radians(item.Spin), glm::vec3(0.0f, 1.0f, 0.0f)),
                               glm::vec3(config_.Scale));
        }
    }
}
