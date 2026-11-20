#include "Engine/Common/CoreMinimal.hpp"
#include "WeaponSystem.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Player/PlayerController.hpp"

namespace NextDayz
{
    namespace
    {
        // Magnified optics: the housing vignette starts closing in at this aim
        // progress, and the geometric tube is swapped for the lens image once
        // the vignette is this far in (ScopeBlend).
        constexpr float kScopeBlendStart = 0.55f;
        constexpr float kScopeMeshHide = 0.35f;
        // View-model recoil velocity added as the stock seats on the cheek.
        constexpr float kAdsSettleImpulse = 0.010f;

        struct FMeleeViewPose
        {
            glm::vec3 offset{0.0f};
            glm::vec3 euler{0.0f}; // degrees
        };

        FMeleeViewPose MeleeReadyPose(const FWeaponDef& def)
        {
            if (def.id == "axe") return {{0.28f, -0.28f, 0.46f}, {-66.0f, -12.0f, 65.0f}};
            if (def.id == "crowbar") return {{0.30f, -0.24f, 0.44f}, {-68.0f, -16.0f, -14.0f}};
            return {{0.24f, -0.22f, 0.43f}, {-25.0f, -18.0f, -12.0f}};
        }

        FMeleeViewPose MeleeLowPose(const FWeaponDef& def)
        {
            if (def.id == "axe") return {{0.32f, -0.46f, 0.48f}, {-74.0f, -10.0f, 60.0f}};
            if (def.id == "crowbar") return {{0.32f, -0.40f, 0.46f}, {-65.0f, -16.0f, -18.0f}};
            return {{0.28f, -0.34f, 0.45f}, {32.0f, -12.0f, -15.0f}};
        }

        // The same contact times drive first-person motion, TPS clips and damage.
        FMeleeViewPose SampleMeleeSwing(const FWeaponDef& def, float time)
        {
            const FMeleeViewPose ready = MeleeReadyPose(def);
            const bool knife = def.id == "knife";
            const bool axe = def.id == "axe";
            const std::array<FMeleeViewPose, 5> poses = {{
                ready,
                {{knife ? 0.34f : 0.40f, axe ? -0.02f : -0.16f, 0.38f},
                 {knife ? -10.0f : -105.0f, knife ? 12.0f : 45.0f, -28.0f}},
                {{knife ? 0.03f : -0.03f, axe ? -0.24f : -0.18f, knife ? 0.78f : 0.60f},
                 {axe ? 18.0f : -8.0f, knife ? -5.0f : -38.0f, -12.0f}},
                {{knife ? 0.01f : -0.20f, axe ? -0.40f : -0.25f, 0.52f},
                 {axe ? 58.0f : 10.0f, knife ? -8.0f : -65.0f, axe ? 18.0f : 22.0f}},
                ready,
            }};
            const std::array<float, 5> times = {0.0f, def.meleeStrikeTime01 * 0.60f,
                                                def.meleeStrikeTime01, def.meleeStrikeTime01 + 0.16f, 1.0f};
            for (size_t key = 1; key < poses.size(); ++key)
            {
                if (time <= times[key])
                {
                    const float t = glm::smoothstep(times[key - 1], times[key], time);
                    return {glm::mix(poses[key - 1].offset, poses[key].offset, t),
                            glm::mix(poses[key - 1].euler, poses[key].euler, t)};
                }
            }
            return ready;
        }
    }

    void WeaponSystem::SetViewModelAssets(const std::array<uint32_t, kWeapons.size()>& modelIds,
                                          const std::array<FWeaponMaterialSlots, kWeapons.size()>& materialIds)
    {
        viewModelModelIds_ = modelIds;
        viewModelMaterialIds_ = materialIds;
        viewModelAssetsSet_ = true;
    }

    void WeaponSystem::OnSceneLoaded(NextEngine& engine)
    {
        slots_ = {};
        activeSlot_ = 0;
        previousSlot_ = -1;
        switchSourceSlot_ = -1;
        reloading_ = false;
        switching_ = false;
        shotSequence_ = 0;
        CancelMelee();
        viewModelOffset_ = config_.ViewModelHipOffset;
        viewModelMeleeEuler_ = glm::vec3(0.0f);
        viewModelRecoil_ = 0.0f;
        viewModelRecoilVelocity_ = 0.0f;
        swayAngle_ = glm::vec2(0.0f);
        swayVelocity_ = glm::vec2(0.0f);
        moveVelocity_ = glm::vec2(0.0f);
        landingOffset_ = 0.0f;
        landingVelocity_ = 0.0f;
        aimAlpha_ = 0.0f;
        previousAimProgress_ = 0.0f;
        viewModelVisible_ = false;
        scopeView_ = false;
        scopeBlend_ = 0.0f;
        renderedViewModelValid_ = false;
        viewModelInScene_ = false;
        viewModelWeaponIndex_ = -1;
        shotEvents_.clear();
        hitEvents_.clear();
        traceEvents_.clear();
        if (!viewModelAssetsSet_)
        {
            return;
        }
        Assets::Scene& scene = engine.GetScene();
        // rayCastVisible = false so the player's own shots never hit the view model.
        viewModelNode_ = Assets::SceneBuilder::CreateRenderNode(
            "nd_viewmodel", glm::vec3(0.0f, -1000.0f, 0.0f), glm::vec3(1.0f), scene.GenerateInstanceId(),
            viewModelModelIds_[0], viewModelMaterialIds_[0], false,
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
        auto physics = std::make_shared<Runtime::PhysicsComponent>();
        physics->SetMobility(Runtime::ENodeMobility::Dynamic);
        viewModelNode_->AddComponent(physics);
        scene.AddNode(viewModelNode_);
        attachmentVisuals_.Bind(scene, viewModelNode_, attachmentAssets_, "nd_viewmodel");
        arms_.Bind(scene, viewModelNode_);
        viewModelInScene_ = true;
        scene.MarkDirty();
    }

    void WeaponSystem::OnSceneUnloaded()
    {
        CancelMelee();
        viewModelNode_.reset();
        attachmentVisuals_.Clear();
        arms_.Clear();
        viewModelVisible_ = false;
        scopeView_ = false;
        scopeBlend_ = 0.0f;
        renderedViewModelValid_ = false;
        viewModelInScene_ = false;
        viewModelWeaponIndex_ = -1;
        shotEvents_.clear();
        hitEvents_.clear();
        traceEvents_.clear();
    }

    bool WeaponSystem::ViewModelRecoilActive() const
    {
        return std::abs(viewModelRecoil_) > 0.0001f || std::abs(viewModelRecoilVelocity_) > 0.001f;
    }

    bool WeaponSystem::ViewModelMuzzle(glm::vec3& outPosition, glm::vec3& outForward) const
    {
        if (!viewModelNode_ || (!viewModelVisible_ && !scopeView_) || !HasActiveWeapon())
        {
            return false;
        }
        const glm::mat4& world = viewModelNode_->WorldTransform();
        const FWeaponModelPoints points = WeaponModelPoints(ActiveWeaponId());
        outPosition = glm::vec3(world * glm::vec4(0.0f, points.boreHeight,
            points.muzzleForward + (IsSuppressed() ? SuppressorLength(ActiveWeaponId()) : 0.0f), 1.0f));
        outForward = glm::normalize(glm::vec3(world[2]));
        return outPosition.y > -900.0f;
    }

    bool WeaponSystem::ViewModelSight(glm::vec3& outPosition, glm::vec3& outForward) const
    {
        if (!viewModelNode_ || (!viewModelVisible_ && !scopeView_) || !HasActiveWeapon() || ActiveIsMelee()) return false;
        const FWeaponModelPoints points = WeaponSightPoints(ActiveWeaponId(), SlotAttachments(activeSlot_));
        const glm::mat4& world = viewModelNode_->WorldTransform();
        outPosition = glm::vec3(world * glm::vec4(0.0f, points.sightHeight, points.rearSightForward, 1.0f));
        outForward = glm::normalize(glm::vec3(world[2]));
        return true;
    }

    bool WeaponSystem::ViewModelRenderedSight(glm::vec3& outPosition, glm::vec3& outForward) const
    {
        if (!renderedViewModelValid_ || !HasActiveWeapon() || ActiveIsMelee()) return false;
        const FWeaponModelPoints points = WeaponSightPoints(ActiveWeaponId(), SlotAttachments(activeSlot_));
        outPosition = glm::vec3(renderedViewModelTransform_ * glm::vec4(0.0f, points.sightHeight, points.rearSightForward, 1.0f));
        outForward = glm::normalize(glm::vec3(renderedViewModelTransform_[2]));
        return true;
    }

    bool WeaponSystem::ViewModelReflexWindow(glm::vec3& outCenter, glm::vec3& outHalfX, glm::vec3& outHalfY) const
    {
        const EWeaponAttachment optic = ActiveOptic();
        if (!viewModelNode_ || !viewModelVisible_ || !HasActiveWeapon() || ActiveIsMelee() ||
            (optic != EWeaponAttachment::RedDot && optic != EWeaponAttachment::Holographic))
        {
            return false;
        }
        // Mount on the rail (WeaponAttachmentVisuals); window sizes from nextdayz_attachments.scad:
        // the red dot's front aperture, the holographic frame's inner opening.
        const std::string id = ActiveWeaponId();
        const bool holo = optic == EWeaponAttachment::Holographic;
        const glm::vec3 mount(0.0f, WeaponOpticRailHeight(id), WeaponOpticRailForward(id));
        const glm::vec3 local = mount + (holo ? glm::vec3(0.0f, 0.045f, -0.006f) : glm::vec3(0.0f, 0.035f, 0.030f));
        const glm::vec2 half = holo ? glm::vec2(0.0285f, 0.026f) : glm::vec2(0.024f);
        const glm::mat4& world = viewModelNode_->WorldTransform();
        outCenter = glm::vec3(world * glm::vec4(local, 1.0f));
        outHalfX = glm::vec3(world * glm::vec4(half.x, 0.0f, 0.0f, 0.0f));
        outHalfY = glm::vec3(world * glm::vec4(0.0f, half.y, 0.0f, 0.0f));
        return true;
    }

    void WeaponSystem::RefreshViewModelVisibility()
    {
        if (viewModelNode_)
        {
            if (auto render = viewModelNode_->GetComponent<Runtime::RenderComponent>())
            {
                render->SetVisible(viewModelVisible_ && HasActiveWeapon() && !presentationSuppressed_);
            }
        }
    }

    void WeaponSystem::ResetEquipPresentation()
    {
        viewModelMeleeEuler_ = glm::vec3(0.0f);
    }

    void WeaponSystem::Update(float deltaSeconds, PlayerController& player, Inventory& inventory, NextEngine& engine)
    {
        WeaponRuntime::Update(deltaSeconds, player, inventory, MakeDesktopPlayerRayQuery(engine));
        const FWeaponDef* def = ActiveWeapon();
        const FWeaponAttachments& loadout = SlotAttachments(activeSlot_);
        const float scopeFov = def ? WeaponSightPoints(def->id, loadout).scopeFov : 0.0f;
        // View-model follows the camera; lerp offset toward hip/ADS pose.
        if (viewModelNode_)
        {
            const int weaponIndex = WeaponIndex(ActiveWeaponId());
            if (weaponIndex >= 0 && weaponIndex != viewModelWeaponIndex_)
            {
                viewModelWeaponIndex_ = weaponIndex;
                if (auto render = viewModelNode_->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetModelId(viewModelModelIds_[weaponIndex]);
                    render->SetMaterials(viewModelMaterialIds_[weaponIndex]);
                }
                engine.GetScene().MarkDirty();
            }

            const float recoilDt = std::max(0.0f, deltaSeconds);
            viewModelRecoilVelocity_ +=
                (-config_.ViewModelRecoilSpring * viewModelRecoil_ -
                 config_.ViewModelRecoilDamping * viewModelRecoilVelocity_) *
                recoilDt;
            viewModelRecoil_ += viewModelRecoilVelocity_ * recoilDt;

            // Hip pose (smoothed below, with reload / traversal dips) and the
            // sight pose: the actual rear sight / eyepiece anchored ahead of the
            // eye. The raise between them follows the player's aim timeline so
            // the camera zoom, the flight from third person and the weapon all
            // arrive together; a shared grip offset left long guns floating.
            glm::vec3 targetOffset = config_.ViewModelHipOffset;
            glm::vec3 sightOffset = targetOffset;
            const bool firearm = def && !def->IsMelee();
            if (firearm)
            {
                const FWeaponModelPoints points = WeaponSightPoints(def->id, loadout);
                sightOffset = glm::vec3(0.0f, -points.sightHeight * config_.ViewModelScale,
                                        points.adsEyeRelief - points.rearSightForward * config_.ViewModelScale);
            }
            glm::vec3 meleeEuler(0.0f);
            if (def && def->IsMelee())
            {
                const FMeleeViewPose pose = player.IsAiming() || IsSwinging() ? MeleeReadyPose(*def) : MeleeLowPose(*def);
                targetOffset = pose.offset;
                meleeEuler = glm::radians(pose.euler);
            }
            const float actionTime = PresentationActionTime();
            const float actionArc = std::sin(glm::pi<float>() * actionTime);
            if (reloading_)
            {
                targetOffset.y -= actionArc * 0.18f;
                targetOffset.x += actionArc * 0.08f;
            }
            // Traversal: the weapon swings down and out to the firing side while
            // the hands are on the obstacle, and comes back up as the body stands.
            if (player.IsTraversing() && !IsSwinging())
            {
                targetOffset += glm::vec3(0.06f, -0.11f, -0.08f);
                meleeEuler += glm::radians(glm::vec3(28.0f, -16.0f, 20.0f));
            }
            const float t = 1.0f - std::exp(-config_.ViewModelLerpSpeed * std::max(0.0f, deltaSeconds));
            viewModelOffset_ = glm::mix(viewModelOffset_, targetOffset, t);
            // Follow the switch timeline directly: the old gun leaves the
            // view before the model changes, then the new one is drawn in.
            // Smoothing this offset lets the replacement pop into view.
            float switchLower = 0.0f;
            if (switching_)
            {
                switchLower = actionTime < 0.5f
                    ? glm::smoothstep(0.0f, kWeaponViewModelHideTime, actionTime)
                    : 1.0f - glm::smoothstep(kWeaponViewModelShowTime, 1.0f, actionTime);
            }
            if (def && def->IsMelee() && IsSwinging())
            {
                const FMeleeViewPose pose = SampleMeleeSwing(*def, PresentationActionTime());
                viewModelOffset_ = pose.offset;
                meleeEuler = glm::radians(pose.euler);
            }

            const bool betweenWeapons = switching_ && actionTime >= kWeaponViewModelHideTime && actionTime < kWeaponViewModelShowTime;
            const bool firstPersonWeapon = HasActiveWeapon() && player.IsFirstPerson() && !presentationSuppressed_ && !betweenWeapons;
            // A magnified optic: as the eye closes on the eyepiece the housing
            // vignette closes in from the screen edges (ScopeBlend, drawn by the
            // HUD) and the empty geometric tube gives way to the lens image.
            // Third person still renders the complete scoped rifle.
            const float aimProgress = player.AimProgress();
            scopeBlend_ = firstPersonWeapon && scopeFov > 0.0f && !reloading_ && !switching_
                              ? glm::smoothstep(kScopeBlendStart, 1.0f, aimProgress) : 0.0f;
            scopeView_ = scopeBlend_ >= kScopeMeshHide;
            const bool show = firstPersonWeapon && !scopeView_;
            if (show != viewModelVisible_ || show != viewModelInScene_)
            {
                viewModelVisible_ = show;
                if (auto render = viewModelNode_->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetVisible(show);
                }
                if (!show && viewModelInScene_)
                {
                    engine.GetScene().RemoveNodeByInstanceId(viewModelNode_->GetInstanceId());
                    viewModelInScene_ = false;
                }
                else if (show && !viewModelInScene_)
                {
                    engine.GetScene().AddNode(viewModelNode_);
                    viewModelInScene_ = true;
                }
                engine.GetScene().MarkDirty();
            }
            viewModelNode_->Scale() = firstPersonWeapon ? glm::vec3(config_.ViewModelScale) : glm::vec3(0.0f);
            glm::vec3 motionOffset(0.0f);
            glm::vec3 motionEuler(0.0f);
            UpdateViewModelMotion(player, deltaSeconds, motionOffset, motionEuler);
            float sightWeight = aimAlpha_;
            glm::vec3 poseOffset = viewModelOffset_;
            if (firearm)
            {
                // The raise is driven exactly by the aim timeline, so a settled
                // sight sits precisely on the eye line with no smoothing residue.
                const float raise = (reloading_ || switching_ || player.IsTraversing()) ? 0.0f : aimAlpha_;
                poseOffset = glm::mix(viewModelOffset_, sightOffset, raise);
                sightWeight = raise;
                // The sight follows the eye when shouldered. Camera bob and
                // recoil still move the view; hip-fire inertia must not break ADS alignment.
                motionOffset *= 1.0f - sightWeight;
                motionEuler *= 1.0f - sightWeight;
                // Swing the gun up in an arc rather than sliding it: it cants
                // toward the firing shoulder and rolls level as the stock meets
                // the cheek. Zero at both ends, so hip and sight poses are exact.
                const float arc = std::sin(glm::pi<float>() * raise);
                motionEuler += glm::radians(glm::vec3(2.5f, 3.5f, -9.0f)) * arc;
                // A small thump as the stock seats: one impulse when the sight arrives.
                if (aimProgress >= 1.0f && previousAimProgress_ < 1.0f && player.IsAiming())
                {
                    viewModelRecoilVelocity_ += kAdsSettleImpulse;
                }
            }
            previousAimProgress_ = aimProgress;
            viewModelMeleeEuler_ = IsSwinging() ? meleeEuler : glm::mix(viewModelMeleeEuler_, meleeEuler, t);
            motionEuler += viewModelMeleeEuler_;
            const bool waistPhase = (actionTime < 0.5f ? switchSourceSlot_ : switchTargetSlot_) == kSidearmSlot;
            motionEuler += glm::radians(waistPhase ? glm::vec3(70.0f, -10.0f, -5.0f)
                                                  : glm::vec3(55.0f, -18.0f, -22.0f)) * switchLower;
            glm::mat4 cameraWorld(1.0f);
            if (firstPersonWeapon)
            {
                const glm::vec3 eye = player.CameraPosition();
                const glm::vec3 forward = player.CameraForward();
                const glm::vec3 up = player.CameraUp();
                const glm::vec3 right = glm::normalize(glm::cross(forward, up));
                // Proper (det +1) rotation basis: object +Z -> forward, +Y -> up,
                // +X = up x forward (right() is left-handed w.r.t. forward/up).
                const glm::vec3 xAxis = glm::normalize(glm::cross(up, forward));
                const glm::vec3 offset = poseOffset + motionOffset +
                    (waistPhase ? glm::vec3(0.22f, -0.52f, -0.05f) : glm::vec3(0.12f, -0.65f, -0.22f)) * switchLower;
                glm::vec3 pos =
                    eye + right * offset.x + up * offset.y + forward * (offset.z - viewModelRecoil_ * 2.5f);
                const glm::quat cameraRotation = glm::quat_cast(glm::mat3(xAxis, up, forward));
                const glm::quat motionRotation =
                    glm::angleAxis(motionEuler.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
                    glm::angleAxis(motionEuler.x, glm::vec3(1.0f, 0.0f, 0.0f)) *
                    glm::angleAxis(motionEuler.z, glm::vec3(0.0f, 0.0f, 1.0f)) *
                    glm::angleAxis(reloading_ ? actionArc * 0.45f : 0.0f, glm::vec3(0.0f, 0.0f, 1.0f)) *
                    glm::angleAxis(-viewModelRecoil_ * 100.0f * glm::radians(config_.ViewModelRecoilPitchDegrees),
                                   glm::vec3(1.0f, 0.0f, 0.0f));
                if (firearm)
                {
                    const FWeaponModelPoints points = WeaponSightPoints(def->id, loadout);
                    const glm::vec3 sight = glm::vec3(0.0f, points.sightHeight, points.rearSightForward) * config_.ViewModelScale;
                    // Tilt about the rear sight, rather than sweeping it around the grip.
                    pos += cameraRotation * (sight - motionRotation * sight) * sightWeight;
                }
                viewModelNode_->Translation() = pos;
                viewModelNode_->Rotation() = cameraRotation * motionRotation;
                viewModelNode_->RecalcTransform(true);
                cameraWorld = glm::translate(glm::mat4(1.0f), eye) * glm::mat4_cast(cameraRotation);
            }
            else
            {
                viewModelNode_->Translation() = glm::vec3(0.0f, -1000.0f, 0.0f);
                viewModelNode_->RecalcTransform(true);
            }
            // The camera is current-frame, but scene proxies are handed off one
            // frame later. Patch only this camera-attached mesh in the upload
            // snapshot; rebuilding the whole scene synchronously would stall it.
            Assets::Scene& scene = engine.GetScene();
            attachmentVisuals_.Update(ActiveWeaponId(), loadout, show);
            // The survivor's own arms hold every firearm the camera carries.
            const bool armsVisible = show && firearm;
            arms_.Update(ActiveWeaponId(),
                         armsVisible ? glm::inverse(cameraWorld) * viewModelNode_->WorldTransform() : glm::mat4(1.0f),
                         config_.ViewModelScale, armsVisible, true);
            std::vector<std::shared_ptr<Assets::Node>> cameraNodes;
            cameraNodes.reserve(1 + kAttachmentSlotCount + arms_.Nodes().size());
            cameraNodes.push_back(viewModelNode_);
            cameraNodes.insert(cameraNodes.end(), attachmentVisuals_.Nodes().begin(), attachmentVisuals_.Nodes().end());
            const size_t firstArmNode = cameraNodes.size();
            cameraNodes.insert(cameraNodes.end(), arms_.Nodes().begin(), arms_.Nodes().end());
            bool patchedProxy = false;
            renderedViewModelValid_ = false;
            renderedAttachmentError_ = 0.0f;
            renderedArmError_ = 0.0f;
            std::vector<bool> matched(cameraNodes.size(), false);
            for (auto& proxy : scene.GetNodeProxies())
            {
                const auto match = std::find_if(cameraNodes.begin(), cameraNodes.end(), [&](const auto& node)
                { return node && proxy.instanceId == node->GetInstanceId(); });
                if (match == cameraNodes.end()) continue;
                const auto& node = *match;
                const size_t matchIndex = static_cast<size_t>(std::distance(cameraNodes.begin(), match));
                matched[matchIndex] = true;
                const auto render = node->GetComponent<Runtime::RenderComponent>();
                const bool visible = show && render && render->GetVisible();
                const glm::mat4 world = node->WorldTransform();
                if (visible)
                {
                    proxy.combinedPrevTS = proxy.worldTS * glm::inverse(world);
                    proxy.worldTS = world;
                    if (node == viewModelNode_)
                    {
                        renderedViewModelTransform_ = proxy.worldTS;
                        renderedViewModelValid_ = true;
                    }
                    else
                    {
                        const glm::mat4 local = glm::translate(glm::mat4(1.0f), node->Translation()) *
                            glm::mat4_cast(node->Rotation()) * glm::scale(glm::mat4(1.0f), node->Scale());
                        const glm::mat4 expected = viewModelNode_->WorldTransform() * local;
                        float& error = matchIndex >= firstArmNode ? renderedArmError_ : renderedAttachmentError_;
                        for (int column = 0; column < 4; ++column)
                            error = std::max(error, glm::length(proxy.worldTS[column] - expected[column]));
                    }
                }
                proxy.visible = visible ? render->GetRenderParticipationMask() : 0;
                patchedProxy = true;
            }
            for (size_t i = 1; i < cameraNodes.size(); ++i)
                if (show && cameraNodes[i] && cameraNodes[i]->GetComponent<Runtime::RenderComponent>()->GetVisible() && !matched[i])
                    (i >= firstArmNode ? renderedArmError_ : renderedAttachmentError_) = 1000.0f;
            if (patchedProxy) scene.RequestGpuNodeProxyUpload();
            scene.MarkTransformDirty();
        }
    }

    void WeaponSystem::UpdateViewModelMotion(const PlayerController& player, float deltaSeconds, glm::vec3& outOffset,
                                             glm::vec3& outEuler)
    {
        const float dt = std::max(deltaSeconds, 0.0f);
        const FWeaponFeelConfig& feel = config_;
        // The player's raise timeline (eased), shared with the camera zoom and flight.
        aimAlpha_ = player.AimBlend();
        const float hipAlpha = 1.0f - aimAlpha_;
        const float sprint = glm::smoothstep(0.0f, 1.0f, player.SprintAlpha());
        const FPlayerLocomotionState& locomotion = player.LocomotionState();

        // Look inertia: the weapon lags the camera's angular velocity and
        // springs back past centre when the mouse stops.
        const glm::vec2 angularVelocity = dt > 0.0f ? player.LookDelta() / dt : glm::vec2(0.0f);
        const float swayLimit = glm::radians(feel.ViewModelSwayMaxDegrees);
        const glm::vec2 swayTarget =
            glm::clamp(-angularVelocity * feel.ViewModelSwayLagSeconds, glm::vec2(-swayLimit), glm::vec2(swayLimit)) *
            glm::mix(1.0f, 0.3f, aimAlpha_);
        const float springDt = std::min(dt, 0.05f);
        swayVelocity_ += (feel.ViewModelSwaySpring * (swayTarget - swayAngle_) -
                          feel.ViewModelSwayDamping * swayVelocity_) *
                         springDt;
        swayAngle_ += swayVelocity_ * springDt;

        // Movement: tilt into a strafe, drift against the motion.
        const glm::vec2 velocity = player.LocalVelocity();
        moveVelocity_ = glm::mix(moveVelocity_, velocity,
                                 1.0f - std::exp(-dt / std::max(feel.ViewModelMoveSmoothingSeconds, 1.0e-3f)));

        // Stride bob, a figure eight: one sway and two dips per stride.
        const float speedScale =
            locomotion.onGround ? glm::clamp(locomotion.horizontalSpeed / 4.2f, 0.0f, 1.6f) : 0.0f;
        const float bobScale = speedScale * glm::mix(1.0f, feel.ViewModelBobSprintScale, sprint) *
                               glm::mix(1.0f, feel.ViewModelBobAimScale, aimAlpha_);
        const float theta = glm::two_pi<float>() * player.StridePhase();
        const float dip = 0.5f - 0.5f * std::cos(2.0f * theta);

        // Idle breathing.
        breathTime_ += dt;
        const float breath = std::sin(glm::two_pi<float>() * breathTime_ / std::max(feel.ViewModelBreathSeconds, 0.1f)) *
                             (1.0f - glm::clamp(speedScale * 2.0f, 0.0f, 1.0f)) * glm::mix(1.0f, 0.35f, aimAlpha_);

        // Landing: a kick into a spring.
        if (player.LandingSpeed() > 0.0f)
        {
            landingVelocity_ -= player.LandingSpeed() * feel.ViewModelLandingKick * 10.0f;
        }
        landingVelocity_ += (-160.0f * landingOffset_ - 16.0f * landingVelocity_) * springDt;
        landingOffset_ = glm::clamp(landingOffset_ + landingVelocity_ * springDt, -0.08f, 0.04f);

        outOffset = glm::vec3(feel.ViewModelBob.x * std::sin(theta) * bobScale - swayAngle_.x * 0.05f * hipAlpha -
                                  moveVelocity_.x * feel.ViewModelMoveOffsetPerMps * hipAlpha,
                              -feel.ViewModelBob.y * dip * bobScale + swayAngle_.y * 0.03f * hipAlpha +
                                  feel.ViewModelBreathHeight * breath + landingOffset_,
                              -std::max(moveVelocity_.y, 0.0f) * feel.ViewModelMoveOffsetPerMps * 0.6f * hipAlpha) +
                    feel.ViewModelSprintOffset * sprint;
        outEuler = glm::vec3(swayAngle_.y + glm::radians(feel.ViewModelSprintEulerDegrees.x) * sprint,
                             swayAngle_.x + glm::radians(feel.ViewModelSprintEulerDegrees.y) * sprint,
                             glm::radians(feel.ViewModelBobRollDegrees) * std::sin(theta) * bobScale +
                                 glm::radians(feel.ViewModelMoveRollDegreesPerMps) * moveVelocity_.x *
                                     glm::mix(1.0f, 0.25f, aimAlpha_) +
                                 glm::radians(feel.ViewModelSprintEulerDegrees.z) * sprint);
    }
}
