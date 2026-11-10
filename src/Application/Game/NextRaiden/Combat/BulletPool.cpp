#include "Application/Game/NextRaiden/Combat/BulletPool.hpp"

#include <algorithm>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Application/Game/NextRaiden/Level/PrefabRack.hpp"
#include "Application/Game/NextRaiden/Player/ScrollCamera.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"

namespace NextRaiden
{
    namespace
    {
        constexpr float kCullMargin = 8.0f;
        /// 比播放列宽出一截：敌弹会从立柱后面斜着飞进来，砍太早就是凭空消失。
        constexpr float kCullHalfWidth = 76.0f;

        const char* PrefabForVisual(EBulletVisual visual)
        {
            switch (visual)
            {
            case EBulletVisual::Bolt: return Prefabs::kBolt;
            case EBulletVisual::BoltHostile: return Prefabs::kBoltHostile;
            case EBulletVisual::Orb: return Prefabs::kOrb;
            case EBulletVisual::Needle: return Prefabs::kNeedle;
            case EBulletVisual::Missile: return Prefabs::kMissile;
            case EBulletVisual::Bomb: return Prefabs::kBomb;
            case EBulletVisual::Laser: return Prefabs::kLaser;
            default: return Prefabs::kBolt;
            }
        }
    }

    void FBulletPool::Create(FPrefabRack& rack, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                             const FBulletPoolSizes& sizes)
    {
        Clear();
        sizes_ = sizes;
        int total = 0;
        for (size_t v = 0; v < visuals_.size(); ++v)
        {
            const int count = std::max(0, sizes.PerVisual[v]);
            FVisualPool& pool = visuals_[v];
            pool.Nodes = rack.Clone(PrefabForVisual(static_cast<EBulletVisual>(v)), count, nodes,
                                    FPrefabRack::ParkPosition(static_cast<int>(v) + 8));
            pool.Free.resize(pool.Nodes.size());
            for (size_t i = 0; i < pool.Nodes.size(); ++i)
            {
                pool.Free[i] = static_cast<int>(pool.Nodes.size() - 1 - i);
            }
            total += static_cast<int>(pool.Nodes.size());
        }
        bullets_.reserve(static_cast<size_t>(total));
    }

    void FBulletPool::Clear()
    {
        for (FVisualPool& pool : visuals_)
        {
            pool.Nodes.clear();
            pool.Free.clear();
        }
        bullets_.clear();
        aliveCount_ = 0;
    }

    bool FBulletPool::Emit(const FBulletSeed& seed, bool hostile, float radius)
    {
        FVisualPool& pool = visuals_[static_cast<size_t>(seed.Visual)];
        if (pool.Free.empty())
        {
            return false;
        }
        const int slot = pool.Free.back();
        pool.Free.pop_back();

        FBullet* bullet = nullptr;
        for (FBullet& candidate : bullets_)
        {
            if (!candidate.Active)
            {
                bullet = &candidate;
                break;
            }
        }
        if (!bullet)
        {
            bullets_.push_back(FBullet{});
            bullet = &bullets_.back();
        }

        bullet->Position = seed.Position;
        bullet->Velocity = seed.Velocity;
        bullet->Radius = radius;
        bullet->Damage = seed.Damage;
        bullet->Hostile = hostile;
        bullet->Visual = seed.Visual;
        bullet->Slot = slot;
        bullet->Active = true;
        ++aliveCount_;

        Assets::NodeUtils::SetVisibleRecursive(pool.Nodes[static_cast<size_t>(slot)], true);
        return true;
    }

    void FBulletPool::Kill(FBullet& bullet)
    {
        if (!bullet.Active)
        {
            return;
        }
        FVisualPool& pool = visuals_[static_cast<size_t>(bullet.Visual)];
        if (bullet.Slot >= 0 && bullet.Slot < static_cast<int>(pool.Nodes.size()))
        {
            const std::shared_ptr<Assets::Node>& node = pool.Nodes[static_cast<size_t>(bullet.Slot)];
            Assets::NodeUtils::SetVisibleRecursive(node, false);
            pool.Free.push_back(bullet.Slot);
        }
        bullet.Slot = -1;
        bullet.Active = false;
        --aliveCount_;
    }

    void FBulletPool::Step(float deltaSeconds, const FScrollCamera& camera)
    {
        const float nearZ = camera.FlightNearZ() + kCullMargin;
        const float farZ = camera.FlightFarZ() - kCullMargin;

        for (FBullet& bullet : bullets_)
        {
            if (!bullet.Active)
            {
                continue;
            }
            bullet.Position += bullet.Velocity * deltaSeconds;
            if (bullet.Position.z > nearZ || bullet.Position.z < farZ ||
                std::abs(bullet.Position.x) > kCullHalfWidth)
            {
                Kill(bullet);
            }
        }
    }

    int FBulletPool::HostileCount() const
    {
        return static_cast<int>(std::count_if(bullets_.begin(), bullets_.end(),
                                              [](const FBullet& b) { return b.Active && b.Hostile; }));
    }

    int FBulletPool::ClearHostile()
    {
        int cleared = 0;
        for (FBullet& bullet : bullets_)
        {
            if (bullet.Active && bullet.Hostile)
            {
                Kill(bullet);
                ++cleared;
            }
        }
        return cleared;
    }

    void FBulletPool::Apply()
    {
        for (const FBullet& bullet : bullets_)
        {
            if (!bullet.Active || bullet.Slot < 0)
            {
                continue;
            }
            FVisualPool& pool = visuals_[static_cast<size_t>(bullet.Visual)];
            const std::shared_ptr<Assets::Node>& node = pool.Nodes[static_cast<size_t>(bullet.Slot)];
            // Bullet meshes point at engine +Z like everything else in the kit, so the yaw that
            // faces the velocity is the same atan2 the rigs use.
            const float yaw = std::atan2(bullet.Velocity.x, bullet.Velocity.z);
            const float scale = sizes_.Scale[static_cast<size_t>(bullet.Visual)];
            node->SetTransform(bullet.Position, glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(scale));
        }
    }
}
