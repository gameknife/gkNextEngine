#include "Brotato3DZombieVisual.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Modules/ScadLoader/FScadRig.h"

namespace Brotato3D
{
    namespace
    {
        constexpr float parkedY = -1000.0f;
        // Ground speeds the gait clips were authored at (brotato_zombie_walker.scad).
        constexpr float walkAuthoredSpeed = 0.95f;
        constexpr float runAuthoredSpeed = 3.2f;
        constexpr float crawlAuthoredSpeed = 0.55f;
        constexpr float minPlayRate = 0.6f;
        constexpr float maxPlayRate = 1.9f;
        constexpr float flinchSpring = 150.0f;
        constexpr float flinchDamping = 15.0f;
        constexpr float flinchMaxRadians = 0.6f;
        constexpr float limbGravity = 20.0f;
        constexpr float limbRestSpeed = 0.7f;
        const glm::vec3 axisX(1.0f, 0.0f, 0.0f);
        const glm::vec3 axisY(0.0f, 1.0f, 0.0f);
        const glm::vec3 axisZ(0.0f, 0.0f, 1.0f);

        constexpr std::array<const char*, zombieLimbCount> limbBoneNames = {
            "bone_head",      "bone_upperarm_l", "bone_upperarm_r", "bone_forearm_l", "bone_forearm_r",
            "bone_thigh_l",   "bone_thigh_r",    "bone_calf_l",     "bone_calf_r",
        };

        // The limb whose loss also takes this one with it (an arm takes its forearm).
        constexpr std::array<int, zombieLimbCount> limbParent = {-1, -1, -1, 1, 2, -1, -1, 5, 6};

        uint32_t ColorKey(const glm::vec3& color)
        {
            const glm::ivec3 q = glm::ivec3(glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f)) * 255.0f + 0.5f);
            return static_cast<uint32_t>(q.x) | (static_cast<uint32_t>(q.y) << 8) | (static_cast<uint32_t>(q.z) << 16);
        }
    }

    bool FZombieRigResource::Load(const std::string& scadPath, const Assets::FRigAsset* clipDonor)
    {
        std::string error;
        std::vector<std::string> warnings;
        loaded_ = Assets::FScadRigLoader::LoadRig(scadPath, {}, asset_, error, &warnings);
        if (!loaded_)
        {
            SPDLOG_ERROR("[Brotato3D] failed to load zombie rig '{}': {}", scadPath, error);
            return false;
        }
        for (const std::string& warning : warnings)
        {
            SPDLOG_WARN("[Brotato3D] zombie rig '{}': {}", scadPath, warning);
        }

        if (clipDonor && clipDonor != &asset_)
        {
            // Every variant shares the walker's skeleton, so its clips retarget by bone name.
            asset_.clips.clear();
            for (const Assets::FRigClip& source : clipDonor->clips)
            {
                Assets::FRigClip clip = source;
                clip.channels.clear();
                for (const Assets::FRigChannel& channel : source.channels)
                {
                    if (channel.bone < 0 || channel.bone >= static_cast<int32_t>(clipDonor->bones.size()))
                    {
                        continue;
                    }
                    const int32_t bone = asset_.FindBone(clipDonor->bones[channel.bone].name);
                    if (bone < 0)
                    {
                        continue;
                    }
                    Assets::FRigChannel retargeted = channel;
                    retargeted.bone = bone;
                    clip.channels.push_back(std::move(retargeted));
                }
                asset_.clips.push_back(std::move(clip));
            }
        }

        ResolveClips();
        return true;
    }

    FZombieRigResource::FZombieRigResource(const FZombieRigResource& other) : asset_(other.asset_), loaded_(other.loaded_)
    {
        // Clip pointers address this copy's own asset, never the source's.
        if (loaded_)
        {
            ResolveClips();
        }
    }

    void FZombieRigResource::ResolveClips()
    {
        const auto clip = [this](const char* name)
        {
            const Assets::FRigClip* found = asset_.FindClip(name);
            if (!found)
            {
                SPDLOG_WARN("[Brotato3D] zombie rig has no clip '{}'", name);
            }
            return found;
        };
        gaitClips_ = {clip("idle"), clip("walk"), clip("run"), clip("crawl")};
        actionClips_ = {clip("attack_swipe"), clip("attack_lunge"), clip("alert"), clip("stagger")};
        deathClips_ = {clip("death_back"), clip("death_forward"), clip("death_crumple")};
        for (size_t limb = 0; limb < zombieLimbCount; ++limb)
        {
            limbBones_[limb] = asset_.FindBone(limbBoneNames[limb]);
        }
        torsoBone_ = asset_.FindBone("bone_torso");
        headBone_ = asset_.FindBone("bone_head");
    }

    void FZombieRigResource::Inject(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
                                    const std::vector<glm::vec3>& tints)
    {
        injected_ = false;
        partModelIds_.clear();
        tintMaterials_.clear();
        flashMaterials_.clear();
        warningMaterials_.clear();
        enragedMaterials_.clear();
        if (!loaded_)
        {
            return;
        }

        const std::vector<glm::vec3> palette = tints.empty() ? std::vector<glm::vec3>{glm::vec3(0.5f)} : tints;
        std::vector<uint32_t> tintIds;
        tintIds.reserve(palette.size());
        for (const glm::vec3& tint : palette)
        {
            tintIds.push_back(Assets::SceneBuilder::AddLambertianMaterial(materials, tint));
        }
        const uint32_t flashId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f));
        const uint32_t warningId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f, 0.10f, 0.06f));
        std::map<uint32_t, uint32_t> enragedByColor;
        const auto enraged = [&](const glm::vec3& color)
        {
            const glm::vec3 angry = glm::mix(color, glm::vec3(0.95f, 0.14f, 0.08f), 0.55f);
            const uint32_t key = ColorKey(angry);
            if (const auto it = enragedByColor.find(key); it != enragedByColor.end())
            {
                return it->second;
            }
            const uint32_t id = Assets::SceneBuilder::AddLambertianMaterial(materials, angry);
            enragedByColor.emplace(key, id);
            return id;
        };

        tintMaterials_.assign(palette.size(), {});
        for (const Assets::FRigPart& part : asset_.parts)
        {
            models.push_back(asset_.partModels[part.modelIndex]);
            partModelIds_.push_back(static_cast<uint32_t>(models.size() - 1));

            FPartMaterials base{};
            FPartMaterials flash{};
            FPartMaterials warning{};
            FPartMaterials angry{};
            flash.fill(flashId);
            warning.fill(warningId);
            const size_t sectionCount = std::min(part.sectionColors.size(), base.size());
            for (size_t section = 0; section < sectionCount; ++section)
            {
                const bool tintable = section < part.sectionTintable.size() && part.sectionTintable[section];
                base[section] = tintable ? tintIds.front()
                                         : Assets::SceneBuilder::AddRigSectionMaterial(materials, part, section);
                angry[section] = enraged(tintable ? palette.front() : glm::vec3(part.sectionColors[section]));
            }
            flashMaterials_.push_back(flash);
            warningMaterials_.push_back(warning);
            enragedMaterials_.push_back(angry);
            for (size_t tint = 0; tint < palette.size(); ++tint)
            {
                FPartMaterials tinted = base;
                for (size_t section = 0; section < sectionCount; ++section)
                {
                    if (section < part.sectionTintable.size() && part.sectionTintable[section])
                    {
                        tinted[section] = tintIds[tint];
                    }
                }
                tintMaterials_[tint].push_back(tinted);
            }
        }
        injected_ = true;
    }

    const std::vector<FZombieRigResource::FPartMaterials>& FZombieRigResource::Materials(EZombieTone tone,
                                                                                         size_t tint) const
    {
        switch (tone)
        {
        case EZombieTone::Flash:
            return flashMaterials_;
        case EZombieTone::Warning:
            return warningMaterials_;
        case EZombieTone::Enraged:
            return enragedMaterials_;
        case EZombieTone::Normal:
            break;
        }
        return tintMaterials_[std::min(tint, tintMaterials_.size() - 1)];
    }

    bool FZombieRigVisual::Instantiate(Assets::Scene& scene, const FZombieRigResource& resource,
                                       const std::string& name)
    {
        if (!resource.IsReady())
        {
            return false;
        }
        resource_ = &resource;
        scene_ = &scene;
        const Assets::FRigAsset& asset = resource.Asset();

        worldNode_ = Assets::Node::CreateNode(name, glm::vec3(0.0f, parkedY, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                              glm::vec3(1.0f), scene.GenerateInstanceId());
        // Marked dynamic so the engine never promotes the parts to static mesh colliders.
        auto physics = std::make_shared<Runtime::PhysicsComponent>();
        physics->SetMobility(Runtime::ENodeMobility::Dynamic);
        worldNode_->AddComponent(physics);
        scene.AddNode(worldNode_);

        NextGameplay::FRigInstanceDesc desc;
        desc.namePrefix = name;
        desc.partModelIds = resource.PartModelIds();
        desc.partMaterialIds = resource.Materials(EZombieTone::Normal, 0);
        boneNodes_.clear();
        const std::shared_ptr<Assets::Node> rigRoot = NextGameplay::FRigInstance::Instantiate(scene, asset, desc, boneNodes_);
        if (!rigRoot)
        {
            worldNode_.reset();
            resource_ = nullptr;
            scene_ = nullptr;
            return false;
        }
        rigRoot->SetParent(worldNode_);

        partNodes_.assign(asset.parts.size(), nullptr);
        for (size_t partIndex = 0; partIndex < asset.parts.size(); ++partIndex)
        {
            const int32_t bone = asset.parts[partIndex].bone;
            if (bone < 0 || bone >= static_cast<int32_t>(boneNodes_.size()) || !boneNodes_[bone])
            {
                continue;
            }
            const std::string partName = fmt::format("{}/{}__part{}", name, asset.bones[bone].name, partIndex);
            for (const auto& child : boneNodes_[bone]->Children())
            {
                if (child->GetName() == partName)
                {
                    partNodes_[partIndex] = child.get();
                    break;
                }
            }
        }

        using NextGameplay::ERigLayerBlendMode;
        using NextGameplay::FRigBoneMask;
        animator_ = {};
        animator_.Bind(&asset, boneNodes_, worldNode_.get());
        baseLayer_ = animator_.CreateLayer("base", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset));
        actionLayer_ = animator_.CreateLayer("action", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset));
        deathLayer_ = animator_.CreateLayer("death", ERigLayerBlendMode::Override, FRigBoneMask::FullBody(asset));
        animator_.SetLayerWeight(deathLayer_, 0.0f);

        Assets::NodeUtils::SetVisibleRecursive(worldNode_, false);
        Assets::NodeUtils::SetRayCastVisibleRecursive(worldNode_, false);
        visible_ = false;
        appliedTint_ = 0;
        appliedTone_ = EZombieTone::Normal;
        return true;
    }

    void FZombieRigVisual::Reset(size_t tint, float scale, float phase01)
    {
        if (!worldNode_ || !resource_)
        {
            return;
        }
        limbs_ = {};
        tint_ = resource_->TintCount() > 0 ? tint % resource_->TintCount() : 0;
        tone_ = EZombieTone::Normal;
        scale_ = scale;
        dead_ = false;
        deathClip_ = nullptr;
        deathSeconds_ = 0.0f;
        gait_ = EZombieGait::Idle;
        flinch_ = glm::vec2(0.0f);
        flinchVelocity_ = glm::vec2(0.0f);
        ApplyTone();

        animator_.SetLayerWeight(deathLayer_, 0.0f);
        animator_.SetLayerWeight(actionLayer_, 0.0f);
        const Assets::FRigClip* idle = resource_->GaitClip(EZombieGait::Idle);
        animator_.SetLoopBlend(baseLayer_, {{idle, 1.0f}}, "gait", 1.0f, 0.0f);
        // Desynchronise the horde: every body starts somewhere else in its cycle.
        animator_.Evaluate(std::clamp(phase01, 0.0f, 1.0f) * (idle ? idle->duration : 1.0f));
        animator_.Apply();
    }

    void FZombieRigVisual::Hide()
    {
        if (!worldNode_)
        {
            return;
        }
        worldNode_->Translation() = glm::vec3(0.0f, parkedY, 0.0f);
        worldNode_->RecalcTransform(true);
        if (visible_)
        {
            Assets::NodeUtils::SetVisibleRecursive(worldNode_, false);
            visible_ = false;
        }
    }

    void FZombieRigVisual::SetPlacement(const glm::vec3& feetPosition, float yawRadians, float extraScale)
    {
        if (!worldNode_)
        {
            return;
        }
        yaw_ = yawRadians;
        groundY_ = std::max(feetPosition.y, 0.0f);
        worldNode_->Translation() = feetPosition;
        worldNode_->Rotation() = glm::angleAxis(yawRadians, axisY);
        worldNode_->Scale() = glm::vec3(scale_ * extraScale);
    }

    void FZombieRigVisual::SetGait(EZombieGait gait, float groundSpeed)
    {
        if (!resource_ || dead_)
        {
            return;
        }
        gait_ = gait;
        // The rig is scaled at the world node, so a giant covers more ground per stride.
        const float speed = std::max(groundSpeed, 0.0f) / std::max(scale_, 0.1f);
        if (gait == EZombieGait::Crawl)
        {
            const float rate = speed > 0.02f ? std::clamp(speed / crawlAuthoredSpeed, minPlayRate, maxPlayRate) : 0.0f;
            animator_.SetLoopBlend(baseLayer_, {{resource_->GaitClip(EZombieGait::Crawl), 1.0f}}, "crawl", rate, 0.25f);
            return;
        }

        const Assets::FRigClip* idle = resource_->GaitClip(EZombieGait::Idle);
        const Assets::FRigClip* walk = resource_->GaitClip(EZombieGait::Walk);
        const Assets::FRigClip* run = resource_->GaitClip(EZombieGait::Run);
        float idleWeight = 0.0f;
        float walkWeight = 0.0f;
        float runWeight = 0.0f;
        if (gait == EZombieGait::Idle || speed < 0.02f)
        {
            idleWeight = 1.0f;
        }
        else if (speed < walkAuthoredSpeed)
        {
            walkWeight = speed / walkAuthoredSpeed;
            idleWeight = 1.0f - walkWeight;
        }
        else if (speed < runAuthoredSpeed)
        {
            runWeight = (speed - walkAuthoredSpeed) / (runAuthoredSpeed - walkAuthoredSpeed);
            walkWeight = 1.0f - runWeight;
        }
        else
        {
            runWeight = 1.0f;
        }

        // Cadence-match the blended gait to the ground speed so the feet plant.
        float playRate = 1.0f;
        const float gaitWeight = walkWeight + runWeight;
        if (gaitWeight > 1.0e-3f && walk && run)
        {
            const float cadence = (walkWeight / walk->duration + runWeight / run->duration) / gaitWeight;
            const float authored = (walkWeight * walkAuthoredSpeed + runWeight * runAuthoredSpeed) / gaitWeight;
            const float overspeed = std::clamp(speed / std::max(authored, 0.01f), minPlayRate, maxPlayRate);
            const Assets::FRigClip* dominant =
                idleWeight > std::max(walkWeight, runWeight) ? idle : (runWeight > walkWeight ? run : walk);
            playRate = cadence * overspeed * (dominant ? dominant->duration : 1.0f);
        }
        animator_.SetLoopBlend(baseLayer_, {{idle, idleWeight}, {walk, walkWeight}, {run, runWeight}}, "gait", playRate,
                               0.2f);
    }

    void FZombieRigVisual::PlayAction(EZombieAction action, float playRate)
    {
        if (!resource_ || dead_)
        {
            return;
        }
        if (const Assets::FRigClip* clip = resource_->ActionClip(action))
        {
            animator_.SetLayerWeight(actionLayer_, 1.0f);
            animator_.PlayOneShot(actionLayer_, clip, playRate, 0.08f, 0.18f, false);
        }
    }

    void FZombieRigVisual::PlayDeath(EZombieDeath death)
    {
        if (!resource_ || dead_)
        {
            return;
        }
        dead_ = true;
        tone_ = EZombieTone::Normal;
        animator_.SetLayerWeight(actionLayer_, 0.0f, 0.05f);
        // Sampled by our own clock (see Update) so the fall holds its last frame: the corpse.
        deathClip_ = resource_->DeathClip(death);
        deathSeconds_ = 0.0f;
        if (deathClip_)
        {
            animator_.SetManualBlend(deathLayer_, {{deathClip_, 1.0f}}, 0.0f, 0.0f);
            animator_.SetLayerWeight(deathLayer_, 1.0f, 0.07f);
        }
    }

    void FZombieRigVisual::SetTone(EZombieTone tone)
    {
        tone_ = dead_ ? EZombieTone::Normal : tone;
    }

    void FZombieRigVisual::ApplyTone()
    {
        if (!resource_ || (tone_ == appliedTone_ && tint_ == appliedTint_))
        {
            return;
        }
        const auto& materials = resource_->Materials(tone_, tint_);
        for (size_t partIndex = 0; partIndex < partNodes_.size() && partIndex < materials.size(); ++partIndex)
        {
            if (Assets::Node* node = partNodes_[partIndex])
            {
                if (auto render = node->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetMaterials(materials[partIndex]);
                }
            }
        }
        appliedTone_ = tone_;
        appliedTint_ = tint_;
    }

    void FZombieRigVisual::AddHit(const glm::vec3& worldDirection, float strength)
    {
        glm::vec3 local = glm::angleAxis(-yaw_, axisY) * glm::vec3(worldDirection.x, 0.0f, worldDirection.z);
        const float length = glm::length(local);
        if (length < 1.0e-4f)
        {
            return;
        }
        local /= length;
        // A round from the front rocks the chest back; from the side it rolls the shoulders.
        flinchVelocity_.x += local.z * strength;
        flinchVelocity_.y -= local.x * strength;
    }

    bool FZombieRigVisual::HasLimb(EZombieLimb limb) const
    {
        const size_t index = static_cast<size_t>(limb);
        if (limbs_[index].severed)
        {
            return false;
        }
        const int parent = limbParent[index];
        return parent < 0 || !limbs_[static_cast<size_t>(parent)].severed;
    }

    int FZombieRigVisual::SeveredCount() const
    {
        return static_cast<int>(std::count_if(limbs_.begin(), limbs_.end(), [](const FLooseLimb& limb) { return limb.severed; }));
    }

    glm::vec3 FZombieRigVisual::BoneWorldPosition(int32_t bone) const
    {
        if (bone < 0 || bone >= static_cast<int32_t>(boneNodes_.size()) || !boneNodes_[bone])
        {
            return worldNode_ ? worldNode_->Translation() : glm::vec3(0.0f);
        }
        return glm::vec3(boneNodes_[bone]->WorldTransform()[3]);
    }

    bool FZombieRigVisual::Sever(EZombieLimb limb, const glm::vec3& worldVelocity, std::mt19937& rng,
                                 glm::vec3* outWoundPos)
    {
        if (!resource_ || !worldNode_ || !HasLimb(limb))
        {
            return false;
        }
        const int32_t bone = resource_->LimbBone(limb);
        if (bone < 0 || bone >= static_cast<int32_t>(boneNodes_.size()) || !boneNodes_[bone])
        {
            return false;
        }

        const glm::mat4& world = boneNodes_[bone]->WorldTransform();
        FLooseLimb& loose = limbs_[static_cast<size_t>(limb)];
        loose.severed = true;
        loose.resting = false;
        loose.position = glm::vec3(world[3]);
        loose.rotation = glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(world[0])),
                                                                 glm::normalize(glm::vec3(world[1])),
                                                                 glm::normalize(glm::vec3(world[2])))));
        loose.velocity = worldVelocity;
        std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
        glm::vec3 axis(unit(rng), unit(rng) * 0.4f, unit(rng));
        loose.spinAxis = glm::length(axis) > 1.0e-3f ? glm::normalize(axis) : axisX;
        loose.spinRate = 7.0f + 9.0f * std::abs(unit(rng));
        if (outWoundPos)
        {
            *outWoundPos = loose.position;
        }
        return true;
    }

    void FZombieRigVisual::UpdateLooseLimbs(float deltaSeconds)
    {
        const float limbScale = worldNode_->Scale().x;
        const float restHeight = groundY_ + 0.07f * limbScale;
        for (size_t index = 0; index < zombieLimbCount; ++index)
        {
            FLooseLimb& loose = limbs_[index];
            if (!loose.severed)
            {
                continue;
            }
            const int32_t bone = resource_->LimbBone(static_cast<EZombieLimb>(index));
            Assets::Node* node = bone >= 0 && bone < static_cast<int32_t>(boneNodes_.size()) ? boneNodes_[bone] : nullptr;
            Assets::Node* parent = node ? node->GetParent() : nullptr;
            if (!node || !parent)
            {
                continue;
            }

            if (!loose.resting)
            {
                loose.velocity.y -= limbGravity * deltaSeconds;
                loose.position += loose.velocity * deltaSeconds;
                loose.rotation = glm::normalize(glm::angleAxis(loose.spinRate * deltaSeconds, loose.spinAxis) * loose.rotation);
                if (loose.position.y <= restHeight)
                {
                    loose.position.y = restHeight;
                    if (std::abs(loose.velocity.y) < 1.6f && glm::length(glm::vec2(loose.velocity.x, loose.velocity.z)) < limbRestSpeed)
                    {
                        loose.resting = true;
                        loose.velocity = glm::vec3(0.0f);
                    }
                    else
                    {
                        loose.velocity.y = -loose.velocity.y * 0.32f;
                        loose.velocity.x *= 0.55f;
                        loose.velocity.z *= 0.55f;
                        loose.spinRate *= 0.5f;
                    }
                }
            }

            // The skeleton keeps animating; the limb stays where the world put it.
            const glm::mat4 desired = glm::translate(glm::mat4(1.0f), loose.position) * glm::mat4_cast(loose.rotation) *
                                      glm::scale(glm::mat4(1.0f), glm::vec3(limbScale));
            const glm::mat4 local = glm::inverse(parent->WorldTransform()) * desired;
            const glm::vec3 scale(glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])),
                                  glm::length(glm::vec3(local[2])));
            if (scale.x < 1.0e-5f || scale.y < 1.0e-5f || scale.z < 1.0e-5f)
            {
                continue;
            }
            node->Translation() = glm::vec3(local[3]);
            node->Rotation() = glm::normalize(glm::quat_cast(glm::mat3(glm::vec3(local[0]) / scale.x,
                                                                       glm::vec3(local[1]) / scale.y,
                                                                       glm::vec3(local[2]) / scale.z)));
            node->Scale() = scale;
            node->RecalcTransform(true);
        }
    }

    void FZombieRigVisual::Update(float deltaSeconds)
    {
        if (!worldNode_ || !resource_)
        {
            return;
        }
        const float dt = std::max(deltaSeconds, 0.0f);
        if (!visible_)
        {
            Assets::NodeUtils::SetVisibleRecursive(worldNode_, true);
            visible_ = true;
        }
        ApplyTone();

        // Sub-stepped so a hitch cannot blow the spring up.
        for (float remaining = dt; remaining > 0.0f;)
        {
            const float step = std::min(remaining, 1.0f / 90.0f);
            flinchVelocity_ += (-flinchSpring * flinch_ - flinchDamping * flinchVelocity_) * step;
            flinch_ += flinchVelocity_ * step;
            remaining -= step;
        }
        flinch_ = glm::clamp(flinch_, glm::vec2(-flinchMaxRadians), glm::vec2(flinchMaxRadians));

        if (dead_ && deathClip_)
        {
            deathSeconds_ += dt * 1.15f;
            const float duration = deathClip_->duration > 0.0f ? deathClip_->duration : 1.0f;
            animator_.SetManualBlend(deathLayer_, {{deathClip_, 1.0f}}, std::clamp(deathSeconds_ / duration, 0.0f, 1.0f), 0.0f);
        }

        animator_.Evaluate(dt);
        const int32_t torso = resource_->TorsoBone();
        NextGameplay::FRigPose& pose = animator_.MutablePose();
        if (!dead_ && torso >= 0 && torso < static_cast<int32_t>(pose.bones.size()) &&
            (std::abs(flinch_.x) > 1.0e-3f || std::abs(flinch_.y) > 1.0e-3f))
        {
            NextGameplay::FRigBonePose& bone = pose.bones[torso];
            bone.r = glm::normalize(bone.r * glm::angleAxis(flinch_.x, axisX) * glm::angleAxis(flinch_.y, axisZ));
            bone.Mark(NextGameplay::ERigPoseComponent::Rotation);
        }
        animator_.Apply();
        UpdateLooseLimbs(dt);
    }
}
