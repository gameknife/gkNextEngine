#include "Application/Game/NextRaiden/World/BossSystem.hpp"

#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>

#include "Application/Game/NextRaiden/Combat/BulletPool.hpp"
#include "Application/Game/NextRaiden/Combat/DebrisSystem.hpp"
#include "Application/Game/NextRaiden/Combat/RaidenRandom.hpp"
#include "Application/Game/NextRaiden/Player/ScrollCamera.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Subsystems/NextRig.hpp"

namespace NextRaiden
{
    namespace
    {
        /// Bosses arrive from beyond the top edge and slide into their hold depth.
        constexpr float kEntryOffset = 26.0f;
    }

    float WeakPointDamageScale(const FBossPhase& phase, const glm::vec3& weakPoint, const glm::vec3& hitPosition)
    {
        if (phase.WeakPointBone.empty())
        {
            return 1.0f;
        }
        const float dx = hitPosition.x - weakPoint.x;
        const float dz = hitPosition.z - weakPoint.z;
        const float radius = std::max(0.1f, phase.WeakPointRadius);
        return dx * dx + dz * dz <= radius * radius ? phase.WeakPointMultiplier : phase.BodyMultiplier;
    }

    bool ParseBossMove(std::string_view name, EBossMove& out)
    {
        if (name == "hold") { out = EBossMove::Hold; return true; }
        if (name == "strafe") { out = EBossMove::Strafe; return true; }
        if (name == "charge") { out = EBossMove::Charge; return true; }
        return false;
    }

    void FBossSystem::Declare(NextRig* rig, const std::vector<FBossDesc>& bosses, std::vector<Assets::Model>& models,
                              std::vector<Assets::FMaterial>& materials)
    {
        bosses_ = bosses;
        pools_.assign(bosses_.size(), 0u);
        current_ = nullptr;
        instance_ = 0;
        active_ = false;

        if (!rig)
        {
            SPDLOG_WARN("[NextRaiden] no rig subsystem; this stage will have no bosses");
            return;
        }

        for (size_t i = 0; i < bosses_.size(); ++i)
        {
            if (bosses_[i].RigPath.empty())
            {
                continue;
            }
            // One instance each: two bosses are never on screen at the same time, and a rig
            // pool injects one copy of every part model per instance.
            pools_[i] = rig->DeclarePool(bosses_[i].RigPath, 1, models, materials);
            if (pools_[i] == NextRig::kInvalidId)
            {
                SPDLOG_ERROR("[NextRaiden] boss '{}': rig '{}' failed to load", bosses_[i].Id, bosses_[i].RigPath);
            }
        }
    }

    void FBossSystem::Bind(NextRig* rig, Assets::Scene* scene)
    {
        rig_ = rig;
        scene_ = scene;
        current_ = nullptr;
        instance_ = 0;
        active_ = false;
        phase_ = -1;
    }

    void FBossSystem::Unbind()
    {
        // The pools belong to the scene being torn down; the ids are meaningless after this.
        rig_ = nullptr;
        scene_ = nullptr;
        current_ = nullptr;
        instance_ = 0;
        active_ = false;
        pools_.clear();
        bosses_.clear();
    }

    bool FBossSystem::HasPool(size_t bossIndex) const
    {
        return bossIndex < pools_.size() && pools_[bossIndex] != NextRig::kInvalidId && pools_[bossIndex] != 0u;
    }

    bool FBossSystem::Begin(size_t bossIndex, const FScrollCamera& camera)
    {
        if (!rig_ || !HasPool(bossIndex))
        {
            return false;
        }
        Despawn();

        current_ = &bosses_[bossIndex];
        currentIndex_ = bossIndex;
        position_ = glm::vec3(0.0f, camera.FlightAltitude(), camera.CentreZ() - kEntryOffset);
        velocity_ = glm::vec3(0.0f);
        hitPoints_ = current_->HitPoints;
        weakPoint_ = position_;
        age_ = 0.0f;
        entryTimer_ = current_->EntrySeconds;
        fireTimer_ = 0.0f;
        fire2Timer_ = 0.0f;
        phaseDegrees_ = 0.0f;
        volleyIndex_ = 0;
        volleyIndex2_ = 0;
        phase_ = -1;

        // yaw 0 faces engine +Z, which is towards the player - the same default every kit
        // airframe already has.
        instance_ = rig_->Acquire(pools_[bossIndex], position_, 0.0f, glm::vec3(1.0f));
        if (instance_ == NextRig::kInvalidId)
        {
            current_ = nullptr;
            return false;
        }
        active_ = true;

        if (!current_->EntryClip.empty())
        {
            rig_->PlayClip(instance_, current_->EntryClip, 0.0f);
        }
        SPDLOG_INFO("[NextRaiden] boss '{}' enters with {:.0f} hp", current_->Id, hitPoints_);
        return true;
    }

    void FBossSystem::Despawn()
    {
        if (active_ && rig_ && instance_ != NextRig::kInvalidId)
        {
            rig_->Release(instance_);
        }
        instance_ = 0;
        active_ = false;
        current_ = nullptr;
        phase_ = -1;
    }

    void FBossSystem::EnterPhase(int phase, FDebrisSystem& debris, FRaidenRandom& random)
    {
        if (!current_ || phase < 0 || phase >= static_cast<int>(current_->Phases.size()) || phase == phase_)
        {
            return;
        }
        phase_ = phase;
        const FBossPhase& desc = current_->Phases[static_cast<size_t>(phase)];

        if (rig_ && !desc.Clip.empty())
        {
            // Asking first: a rig that has no such clip animates to its bind pose, which reads
            // as a broken boss rather than as a typo.
            if (rig_->HasClip(pools_[currentIndex_], desc.Clip))
            {
                rig_->PlayClip(instance_, desc.Clip, 0.25f);
            }
            else
            {
                SPDLOG_WARN("[NextRaiden] boss '{}' phase {}: rig has no clip '{}'", current_->Id, phase, desc.Clip);
            }
        }

        // Parts come off as the fight escalates. These are the rig's own bones, so what falls
        // is the engine that was bolted on a moment ago.
        for (const std::string& bone : desc.ShedBones)
        {
            if (!rig_ || !scene_)
            {
                break;
            }
            const int32_t nodeId = rig_->GetBoneNodeId(instance_, bone);
            if (nodeId < 0)
            {
                SPDLOG_WARN("[NextRaiden] boss '{}': no bone '{}' to shed", current_->Id, bone);
                continue;
            }
            if (Assets::Node* node = scene_->GetNodeByInstanceId(static_cast<uint32_t>(nodeId)))
            {
                if (std::shared_ptr<Assets::Node> shared = scene_->GetNodeSharedByInstanceId(node->GetInstanceId()))
                {
                    debris.Detach(shared, velocity_ * 0.3f, random);
                }
            }
        }
        if (!desc.ShedBones.empty())
        {
            debris.Spawn(position_, velocity_ * 0.3f, 2, random);
        }

        fireTimer_ = 0.0f;
        fire2Timer_ = 0.0f;
    }

    void FBossSystem::SetHitPointsFraction(float fraction)
    {
        if (!active_ || !current_)
        {
            return;
        }
        hitPoints_ = current_->HitPoints * std::clamp(fraction, 0.0f, 1.0f);
    }

    bool FBossSystem::WeakPointExposed() const
    {
        if (!IsVulnerable() || !current_ || phase_ < 0 || phase_ >= static_cast<int>(current_->Phases.size()))
        {
            return false;
        }
        return !current_->Phases[static_cast<size_t>(phase_)].WeakPointBone.empty();
    }

    float FBossSystem::WeakPointRadius() const
    {
        return WeakPointExposed() ? current_->Phases[static_cast<size_t>(phase_)].WeakPointRadius : 0.0f;
    }

    void FBossSystem::UpdateWeakPoint()
    {
        weakPoint_ = position_;
        if (!current_ || phase_ < 0 || !rig_ || !scene_)
        {
            return;
        }
        const FBossPhase& desc = current_->Phases[static_cast<size_t>(phase_)];
        if (desc.WeakPointBone.empty())
        {
            return;
        }
        const int32_t nodeId = rig_->GetBoneNodeId(instance_, desc.WeakPointBone);
        if (nodeId < 0)
        {
            // Falling back to the hull centre keeps the fight winnable; the warning is how a
            // renamed bone gets noticed instead of quietly becoming "the core is the middle".
            SPDLOG_WARN("[NextRaiden] boss '{}': no weak point bone '{}'; using the hull centre", current_->Id,
                        desc.WeakPointBone);
            return;
        }
        if (const Assets::Node* node = scene_->GetNodeByInstanceId(static_cast<uint32_t>(nodeId)))
        {
            weakPoint_ = node->WorldTranslation();
        }
    }

    void FBossSystem::FirePattern(const FPatternSpec& spec, int volleyIndex, const glm::vec3& playerPosition,
                                  const glm::vec3& playerVelocity, FBulletPool& bullets)
    {
        FBulletSeed seeds[32];
        FPatternContext context;
        context.Origin = position_;
        context.TargetPosition = playerPosition;
        context.TargetVelocity = playerVelocity;
        context.VolleyIndex = volleyIndex;
        context.PhaseDegrees = phaseDegrees_;

        const int written = EvaluatePattern(spec, context, seeds);
        for (int i = 0; i < written; ++i)
        {
            bullets.Emit(seeds[i], true, 0.32f);
        }
    }

    void FBossSystem::Step(float deltaSeconds, const FScrollCamera& camera, const glm::vec3& playerPosition,
                           const glm::vec3& playerVelocity, FBulletPool& bullets, FDebrisSystem& debris,
                           FRaidenRandom& random)
    {
        if (!active_ || !current_ || !rig_)
        {
            return;
        }

        age_ += deltaSeconds;
        entryTimer_ = std::max(0.0f, entryTimer_ - deltaSeconds);
        phaseDegrees_ += deltaSeconds * 90.0f;
        if (phaseDegrees_ > 360.0f)
        {
            phaseDegrees_ -= 360.0f;
        }

        // Pick the phase from remaining health. Written as a search rather than a countdown so
        // a bomb that takes two thresholds in one step still lands on the right one.
        const float fraction = HitPointsFraction();
        int wanted = 0;
        for (int i = 0; i < static_cast<int>(current_->Phases.size()); ++i)
        {
            if (fraction <= current_->Phases[static_cast<size_t>(i)].HpFraction)
            {
                wanted = i;
            }
        }
        if (wanted != phase_)
        {
            EnterPhase(wanted, debris, random);
        }
        if (phase_ < 0)
        {
            return;
        }
        const FBossPhase& desc = current_->Phases[static_cast<size_t>(phase_)];

        const glm::vec3 previous = position_;
        const float holdZ = camera.CentreZ() - current_->HoldDepth;
        // Slides in over the entry window, then holds station relative to the camera.
        const float arrive = current_->EntrySeconds > 0.0f
                                 ? std::clamp(age_ / current_->EntrySeconds, 0.0f, 1.0f)
                                 : 1.0f;
        position_.z = (camera.CentreZ() - kEntryOffset) + (holdZ - (camera.CentreZ() - kEntryOffset)) * arrive;
        position_.y = camera.FlightAltitude();

        const float speedScale = desc.MoveSpeedScale;
        switch (desc.Move)
        {
        case EBossMove::Hold:
            position_.x = 0.0f;
            break;
        case EBossMove::Strafe:
            position_.x = current_->MoveAmplitude *
                          std::sin(glm::two_pi<float>() * age_ * speedScale / std::max(0.2f, current_->MovePeriod));
            break;
        case EBossMove::Charge:
        {
            // Slides towards the player's column and lunges down the screen on the same beat,
            // so the dodge is a sidestep rather than a retreat.
            const float beat = glm::two_pi<float>() * age_ * speedScale / std::max(0.2f, current_->MovePeriod);
            position_.x += (playerPosition.x - position_.x) * std::clamp(deltaSeconds * 1.6f, 0.0f, 1.0f);
            position_.z += current_->MoveAmplitude * 0.35f * std::max(0.0f, std::sin(beat));
            break;
        }
        case EBossMove::Count:
        default:
            break;
        }
        // 要宽于 moveAmplitude（关底 BOSS 是 16），否则横移的两端会被削平成直线。
        position_.x = std::clamp(position_.x, -26.0f, 26.0f);
        velocity_ = deltaSeconds > 0.0f ? (position_ - previous) / deltaSeconds : glm::vec3(0.0f);

        rig_->SetTransform(instance_, position_, 0.0f);
        UpdateWeakPoint();

        if (entryTimer_ > 0.0f)
        {
            return; // no shooting while it is still arriving
        }

        if (desc.HasFire)
        {
            fireTimer_ -= deltaSeconds;
            if (fireTimer_ <= 0.0f)
            {
                fireTimer_ = std::max(0.08f, desc.Fire.IntervalSeconds);
                FirePattern(desc.Fire, volleyIndex_++, playerPosition, playerVelocity, bullets);
            }
        }
        if (desc.HasFire2)
        {
            fire2Timer_ -= deltaSeconds;
            if (fire2Timer_ <= 0.0f)
            {
                fire2Timer_ = std::max(0.08f, desc.Fire2.IntervalSeconds);
                FirePattern(desc.Fire2, volleyIndex2_++, playerPosition, playerVelocity, bullets);
            }
        }
    }

    bool FBossSystem::ApplyScaledDamage(float damage, FDebrisSystem& debris, FRaidenRandom& random)
    {
        hitPoints_ -= damage;
        if (hitPoints_ > 0.0f)
        {
            return false;
        }
        debris.Spawn(position_, velocity_ * 0.3f, current_->DebrisPieces, random);
        return true;
    }

    bool FBossSystem::ApplyDamage(float damage, const glm::vec3& hitPosition, FDebrisSystem& debris,
                                  FRaidenRandom& random)
    {
        if (!IsVulnerable() || !current_)
        {
            return false;
        }
        // Before the first Step has picked a phase there is no weak point to speak of, and a
        // hit that lands then should still count - swallowing it would stall the fight.
        const float scale = phase_ >= 0 ? WeakPointDamageScale(current_->Phases[static_cast<size_t>(phase_)],
                                                               weakPoint_, hitPosition)
                                        : 1.0f;
        return ApplyScaledDamage(damage * scale, debris, random);
    }

    bool FBossSystem::ApplyAreaDamage(float damage, FDebrisSystem& debris, FRaidenRandom& random)
    {
        if (!IsVulnerable() || !current_)
        {
            return false;
        }
        // Whatever is exposed, a screen-wide blast lands on the hull.
        const float scale = phase_ >= 0 ? current_->Phases[static_cast<size_t>(phase_)].BodyMultiplier : 1.0f;
        return ApplyScaledDamage(damage * scale, debris, random);
    }
}
