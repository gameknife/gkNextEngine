#include "Application/Game/NextRaiden/Combat/DebrisSystem.hpp"

#include <algorithm>
#include <array>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include "Application/Game/NextRaiden/Level/PrefabRack.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

namespace NextRaiden
{
    namespace
    {

        constexpr std::array kPartPrefabs = {
            Prefabs::kPartArmour, Prefabs::kPartWing, Prefabs::kPartEngine, Prefabs::kPartCanopy,
            Prefabs::kPartFin,    Prefabs::kPartPod,  Prefabs::kPartGun,
        };
    }

    void FDebrisSystem::Create(FPrefabRack& rack, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                               const FDebrisConfig& config)
    {
        config_ = config;
        slots_.clear();
        detached_.clear();
        aliveCount_ = 0;
        cursor_ = 0;

        if (config_.PerPartPrefab <= 0)
        {
            return;
        }

        int parkSlot = 24;
        for (const char* prefab : kPartPrefabs)
        {
            const glm::vec3 park = FPrefabRack::ParkPosition(parkSlot++);
            std::vector<std::shared_ptr<Assets::Node>> clones =
                rack.Clone(prefab, config_.PerPartPrefab, nodes, park);
            for (size_t i = 0; i < clones.size(); ++i)
            {
                FSlot slot;
                slot.Node = clones[i];
                // Spread the parked pieces out: they get real bodies, and a stack of bodies
                // sharing one spot would spend the first frame after Bind shoving each other.
                slot.Park = park + glm::vec3(static_cast<float>(i) * 2.0f, 0.0f, 0.0f);
                slots_.push_back(std::move(slot));
            }
        }
    }

    bool FDebrisSystem::MeasureNode(const Assets::Node& node, glm::vec3& outHalfExtent,
                                    glm::vec3& outCentreOffset) const
    {
        if (!scene_)
        {
            return false;
        }
        glm::vec3 boundsMin(0.0f);
        glm::vec3 boundsMax(0.0f);
        if (!Assets::NodeUtils::GetSubtreeWorldBounds(*scene_, &node, boundsMin, boundsMax))
        {
            return false;
        }
        outHalfExtent = glm::max((boundsMax - boundsMin) * 0.5f, glm::vec3(0.05f));
        outCentreOffset = (boundsMin + boundsMax) * 0.5f - node.WorldTranslation();
        return true;
    }

    void FDebrisSystem::Bind(Assets::Scene& scene, NextPhysics* physics)
    {
        scene_ = &scene;
        physics_ = physics;
        aliveCount_ = 0;
        detached_.clear();

        if (!physics_ || slots_.empty())
        {
            if (!physics_ && !slots_.empty())
            {
                SPDLOG_WARN("[NextRaiden] no physics backend; destroyed pieces will vanish instead of falling");
            }
            return;
        }

        int bound = 0;
        for (FSlot& slot : slots_)
        {
            if (!slot.Node)
            {
                continue;
            }

            if (!MeasureNode(*slot.Node, slot.HalfExtent, slot.CentreOffset))
            {
                // A prefab with no drawable model: keep a token box so the pool still works.
                slot.HalfExtent = glm::vec3(0.25f);
                slot.CentreOffset = glm::vec3(0.0f, 0.25f, 0.0f);
            }

            const glm::vec3 bodyPosition = slot.Park + slot.CentreOffset;
            slot.Body = physics_->CreateBoxBody(bodyPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), slot.HalfExtent,
                                                NextMotionType::Dynamic);
            if (slot.Body.IsInvalid())
            {
                continue;
            }
            if (!scene.BindPhysicsBody(*slot.Node, slot.Body, Runtime::ENodeMobility::Dynamic))
            {
                physics_->RemoveBody(slot.Body);
                slot.Body = NextBodyID{};
                continue;
            }
            if (auto* component = slot.Node->GetComponent<Runtime::PhysicsComponent>())
            {
                component->SetPhysicsOffset(slot.CentreOffset);
            }
            physics_->SetBodyActive(slot.Body, false);
            Assets::NodeUtils::SetVisibleRecursive(slot.Node, false);
            ++bound;
        }

        SPDLOG_INFO("[NextRaiden] debris: {} slots with dynamic bodies, budget {}", bound, config_.Budget);
    }

    void FDebrisSystem::Unbind()
    {
        // The scene owns the bodies once they are bound to a node, and it is being torn down
        // with them; dropping the pointers is the whole job.
        for (FSlot& slot : slots_)
        {
            slot.Body = NextBodyID{};
            slot.Active = false;
            slot.Node.reset();
        }
        slots_.clear();
        detached_.clear();
        physics_ = nullptr;
        scene_ = nullptr;
        aliveCount_ = 0;
    }

    void FDebrisSystem::SetBudget(int budget)
    {
        config_.Budget = std::max(0, budget);
        while (aliveCount_ > config_.Budget && MakeRoom())
        {
        }
    }

    bool FDebrisSystem::MakeRoom()
    {
        // Detached chunks go first. Both are cosmetic, but a detached piece is gone for good
        // either way, whereas a pooled slot will be reused in a second anyway.
        if (!detached_.empty())
        {
            auto oldest = std::max_element(detached_.begin(), detached_.end(),
                                           [](const FDetached& a, const FDetached& b) { return a.Age < b.Age; });
            RecycleDetached(*oldest);
            detached_.erase(oldest);
            return true;
        }

        FSlot* oldest = nullptr;
        for (FSlot& slot : slots_)
        {
            if (slot.Active && (!oldest || slot.Age > oldest->Age))
            {
                oldest = &slot;
            }
        }
        if (oldest)
        {
            Recycle(*oldest);
            return true;
        }
        return false;
    }

    FDebrisSystem::FSlot* FDebrisSystem::TakeSlot()
    {
        const size_t count = slots_.size();
        for (size_t i = 0; i < count; ++i)
        {
            FSlot& slot = slots_[(cursor_ + i) % count];
            if (!slot.Active && !slot.Body.IsInvalid())
            {
                cursor_ = (cursor_ + i + 1) % count;
                return &slot;
            }
        }

        // Everything is busy: take the oldest. A shmup would rather drop the piece that has
        // already been lying on the runway for ten seconds than skip the explosion in front
        // of the player.
        FSlot* oldest = nullptr;
        for (FSlot& slot : slots_)
        {
            if (slot.Active && (!oldest || slot.Age > oldest->Age))
            {
                oldest = &slot;
            }
        }
        if (oldest)
        {
            Recycle(*oldest);
            return oldest;
        }
        return nullptr;
    }

    glm::vec3 FDebrisSystem::RandomLaunchVelocity(const glm::vec3& inheritVelocity, FRaidenRandom& random) const
    {
        // Outward in the XZ plane plus a lift, so pieces arc away from the blast instead of
        // dropping straight down out of it.
        const float angle = random.Range(0.0f, glm::two_pi<float>());
        const float speed = random.Range(config_.MinSpeed, config_.MaxSpeed);
        return inheritVelocity + glm::vec3(std::sin(angle) * speed,
                                           random.Range(config_.MinLift, config_.MaxLift),
                                           std::cos(angle) * speed);
    }

    glm::vec3 FDebrisSystem::RandomSpin(FRaidenRandom& random) const
    {
        return glm::vec3(random.Signed() * config_.SpinRadiansPerSec, random.Signed() * config_.SpinRadiansPerSec,
                         random.Signed() * config_.SpinRadiansPerSec);
    }

    void FDebrisSystem::Spawn(const glm::vec3& at, const glm::vec3& inheritVelocity, int pieces,
                              FRaidenRandom& random)
    {
        if (!IsAvailable() || config_.Budget <= 0 || pieces <= 0)
        {
            return;
        }

        for (int i = 0; i < pieces; ++i)
        {
            if (aliveCount_ >= config_.Budget)
            {
                break;
            }
            FSlot* slot = TakeSlot();
            if (!slot)
            {
                break;
            }

            const glm::vec3 velocity = RandomLaunchVelocity(inheritVelocity, random);
            const glm::quat rotation = glm::angleAxis(
                random.Range(0.0f, glm::two_pi<float>()),
                glm::normalize(glm::vec3(random.Signed(), random.Signed() + 0.01f, random.Signed())));

            const glm::vec3 spawnOffset(random.Signed() * 0.6f, random.Range(0.0f, 0.8f), random.Signed() * 0.6f);
            physics_->SetBodyTransform(slot->Body, at + spawnOffset + slot->CentreOffset, rotation, true);
            physics_->SetBodyVelocity(slot->Body, velocity, RandomSpin(random));
            physics_->SetBodyActive(slot->Body, true);
            Assets::NodeUtils::SetVisibleRecursive(slot->Node, true);
            slot->Age = 0.0f;
            slot->Active = true;
            ++aliveCount_;
        }
    }

    bool FDebrisSystem::Detach(const std::shared_ptr<Assets::Node>& node, const glm::vec3& inheritVelocity,
                               FRaidenRandom& random)
    {
        if (!node || !physics_ || !scene_ || config_.Budget <= 0)
        {
            return false;
        }
        if (aliveCount_ >= config_.Budget && !MakeRoom())
        {
            return false;
        }

        glm::vec3 halfExtent(0.25f);
        glm::vec3 centreOffset(0.0f);
        if (!MeasureNode(*node, halfExtent, centreOffset))
        {
            return false;
        }

        // Take it off its parent while keeping it exactly where it looks like it is. The node's
        // transform is local to the parent, so the world pose has to be written back as the new
        // local pose or the piece teleports the moment it comes loose.
        const glm::vec3 worldPosition = node->WorldTranslation();
        const glm::quat worldRotation = node->WorldRotation();
        const glm::vec3 worldScale = node->WorldScale();
        node->ClearParent();
        node->SetTransform(worldPosition, worldRotation, worldScale);

        const NextBodyID body = physics_->CreateBoxBody(worldPosition + centreOffset, worldRotation, halfExtent,
                                                        NextMotionType::Dynamic);
        if (body.IsInvalid())
        {
            return false;
        }
        if (!scene_->BindPhysicsBody(*node, body, Runtime::ENodeMobility::Dynamic))
        {
            physics_->RemoveBody(body);
            return false;
        }
        if (auto* component = node->GetComponent<Runtime::PhysicsComponent>())
        {
            component->SetPhysicsOffset(centreOffset);
        }

        physics_->SetBodyVelocity(body, RandomLaunchVelocity(inheritVelocity, random), RandomSpin(random));
        physics_->SetBodyActive(body, true);
        Assets::NodeUtils::SetVisibleRecursive(node, true);

        FDetached piece;
        piece.Node = node;
        piece.Body = body;
        piece.CentreOffset = centreOffset;
        detached_.push_back(std::move(piece));
        ++aliveCount_;
        return true;
    }

    void FDebrisSystem::Recycle(FSlot& slot)
    {
        if (!slot.Active)
        {
            return;
        }
        if (physics_ && !slot.Body.IsInvalid())
        {
            physics_->SetBodyTransform(slot.Body, slot.Park + slot.CentreOffset, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                       true);
            physics_->SetBodyActive(slot.Body, false);
        }
        Assets::NodeUtils::SetVisibleRecursive(slot.Node, false);
        slot.Active = false;
        slot.Age = 0.0f;
        --aliveCount_;
    }

    void FDebrisSystem::RecycleDetached(FDetached& piece)
    {
        if (physics_ && !piece.Body.IsInvalid())
        {
            physics_->SetBodyActive(piece.Body, false);
        }
        Assets::NodeUtils::SetVisibleRecursive(piece.Node, false);
        // The body stays owned by the node's PhysicsComponent and dies with the scene. Removing
        // it here would leave that component holding a stale id for the rest of the stage.
        piece.Body = NextBodyID{};
        --aliveCount_;
    }

    void FDebrisSystem::Step(float deltaSeconds, float recycleZ)
    {
        if (!physics_)
        {
            return;
        }

        const auto shouldRecycle = [&](const FNextPhysicsBody* body, float age)
        {
            if (!body)
            {
                return true;
            }
            // 1. Slid off the bottom of the screen. The ground plane's near edge, not the
            //    flight plane's: with the lens tilted they are metres apart, and using the
            //    wrong one makes pieces wink out while the player is still looking at them.
            if (body->position.z > recycleZ + config_.RecycleMargin)
            {
                return true;
            }
            // 2. Fell out of the world.
            if (body->position.y < config_.KillPlaneY)
            {
                return true;
            }
            // 3. Old and asleep. Resting pieces cost nearly nothing once the solver parks
            //    them, so this is only here to stop a long stage accumulating a carpet.
            return age > config_.MaxAgeSeconds && glm::length(body->velocity) < config_.SleepSpeed;
        };

        for (FSlot& slot : slots_)
        {
            if (!slot.Active)
            {
                continue;
            }
            slot.Age += deltaSeconds;
            if (shouldRecycle(physics_->GetBody(slot.Body), slot.Age))
            {
                Recycle(slot);
            }
        }

        for (size_t i = detached_.size(); i-- > 0;)
        {
            FDetached& piece = detached_[i];
            piece.Age += deltaSeconds;
            if (shouldRecycle(physics_->GetBody(piece.Body), piece.Age))
            {
                RecycleDetached(piece);
                detached_.erase(detached_.begin() + static_cast<ptrdiff_t>(i));
            }
        }
    }
}
