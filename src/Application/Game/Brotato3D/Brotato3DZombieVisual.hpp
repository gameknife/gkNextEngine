#pragma once

// ============================================================================
// Brotato3DZombieVisual.hpp - Humanoid zombie bodies for Brotato3D.
//
// FZombieRigResource is one zombie *type*: its ScadRig geometry (a variant of
// kit_bzombie.scad), the clips adopted from the walker rig, the injected part
// models and the material sets (per shirt tint, plus the hit-flash / warning /
// enraged tones the gameplay swaps in).
//
// FZombieRigVisual is one *body*. The simulation in Brotato3DEnemySystem.cpp
// decides; this only presents:
//   base     idle / walk / run / crawl, cadence-matched to the simulated speed
//   action   swipe / lunge / alert / stagger one-shots over the gait
//   death    back / forward / crumple, held on the last frame
//   flinch   a damped spring kicked by every hit
// Dismemberment throws the real limb: the severed bone subtree stops following
// the skeleton and is simulated ballistically in world space, so the piece on
// the ground is exactly the piece that was on the body, and the wound the kit
// hides inside the joint is what remains.
// ============================================================================

#include <array>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigInstance.h"
#include "Gameplay/Rig/RigLayeredAnimator.h"

namespace Assets
{
    class Node;
    class Scene;
    struct FMaterial;
}

namespace Brotato3D
{
    // Parents come before their children: the detached-limb pass relies on it.
    enum class EZombieLimb : uint8_t
    {
        Head,
        ArmL,
        ArmR,
        ForearmL,
        ForearmR,
        LegL,
        LegR,
        CalfL,
        CalfR,
        Count,
    };
    inline constexpr size_t zombieLimbCount = static_cast<size_t>(EZombieLimb::Count);

    enum class EZombieGait : uint8_t { Idle, Walk, Run, Crawl };
    enum class EZombieAction : uint8_t { Swipe, Lunge, Alert, Stagger };
    enum class EZombieDeath : uint8_t { Back, Forward, Crumple };
    enum class EZombieTone : uint8_t { Normal, Flash, Warning, Enraged };

    class FZombieRigResource
    {
    public:
        using FPartMaterials = std::array<uint32_t, 16>;

        FZombieRigResource() = default;
        // Copies the loaded rig (not what was injected into a scene); used to share the walker.
        FZombieRigResource(const FZombieRigResource& other);
        FZombieRigResource& operator=(const FZombieRigResource&) = delete;

        // clipDonor: the rig whose clips this variant plays (matched by bone name);
        // null when the file carries its own.
        bool Load(const std::string& scadPath, const Assets::FRigAsset* clipDonor);
        void Inject(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
                    const std::vector<glm::vec3>& tints);

        bool IsReady() const { return loaded_ && injected_; }
        const Assets::FRigAsset& Asset() const { return asset_; }
        size_t TintCount() const { return tintMaterials_.size(); }

        const std::vector<uint32_t>& PartModelIds() const { return partModelIds_; }
        const std::vector<FPartMaterials>& Materials(EZombieTone tone, size_t tint) const;

        const Assets::FRigClip* GaitClip(EZombieGait gait) const { return gaitClips_[static_cast<size_t>(gait)]; }
        const Assets::FRigClip* ActionClip(EZombieAction action) const { return actionClips_[static_cast<size_t>(action)]; }
        const Assets::FRigClip* DeathClip(EZombieDeath death) const { return deathClips_[static_cast<size_t>(death)]; }
        int32_t LimbBone(EZombieLimb limb) const { return limbBones_[static_cast<size_t>(limb)]; }
        int32_t TorsoBone() const { return torsoBone_; }
        int32_t HeadBone() const { return headBone_; }

    private:
        void ResolveClips();

        Assets::FRigAsset asset_{};
        bool loaded_ = false;
        bool injected_ = false;
        std::vector<uint32_t> partModelIds_;
        std::vector<std::vector<FPartMaterials>> tintMaterials_;   // [tint][part]
        std::vector<FPartMaterials> flashMaterials_;
        std::vector<FPartMaterials> warningMaterials_;
        std::vector<FPartMaterials> enragedMaterials_;
        std::array<const Assets::FRigClip*, 4> gaitClips_{};
        std::array<const Assets::FRigClip*, 4> actionClips_{};
        std::array<const Assets::FRigClip*, 3> deathClips_{};
        std::array<int32_t, zombieLimbCount> limbBones_{};
        int32_t torsoBone_ = -1;
        int32_t headBone_ = -1;
    };

    class FZombieRigVisual
    {
    public:
        bool Instantiate(Assets::Scene& scene, const FZombieRigResource& resource, const std::string& name);
        bool IsBound() const { return worldNode_ != nullptr; }

        // A new zombie takes this body: whole again, snapped to its pose.
        void Reset(size_t tint, float scale, float phase01);
        void Hide();

        void SetPlacement(const glm::vec3& feetPosition, float yawRadians, float extraScale = 1.0f);
        void SetGait(EZombieGait gait, float groundSpeed);
        void PlayAction(EZombieAction action, float playRate = 1.0f);
        void PlayDeath(EZombieDeath death);
        void SetTone(EZombieTone tone);
        // Kicks the flinch spring; direction is the world-space travel of the hit.
        void AddHit(const glm::vec3& worldDirection, float strength);

        bool HasLimb(EZombieLimb limb) const;
        // Throws the limb. worldVelocity is its launch velocity. Returns the
        // world position of the wound, or false when the limb is already gone.
        bool Sever(EZombieLimb limb, const glm::vec3& worldVelocity, std::mt19937& rng, glm::vec3* outWoundPos = nullptr);
        int SeveredCount() const;
        glm::vec3 BoneWorldPosition(int32_t bone) const;

        void Update(float deltaSeconds);

    private:
        struct FLooseLimb
        {
            bool severed = false;
            bool resting = false;
            glm::vec3 position{0.0f};
            glm::vec3 velocity{0.0f};
            glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec3 spinAxis{1.0f, 0.0f, 0.0f};
            float spinRate = 0.0f;
        };

        void ApplyTone();
        void UpdateLooseLimbs(float deltaSeconds);

        const FZombieRigResource* resource_ = nullptr;
        Assets::Scene* scene_ = nullptr;
        std::shared_ptr<Assets::Node> worldNode_;
        std::vector<Assets::Node*> boneNodes_;
        std::vector<Assets::Node*> partNodes_;
        NextGameplay::FRigLayeredAnimator animator_;
        NextGameplay::FRigLayerHandle baseLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle actionLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle deathLayer_ = NextGameplay::invalidRigLayerHandle;

        std::array<FLooseLimb, zombieLimbCount> limbs_{};
        EZombieGait gait_ = EZombieGait::Idle;
        EZombieTone tone_ = EZombieTone::Normal;
        EZombieTone appliedTone_ = EZombieTone::Normal;
        size_t tint_ = 0;
        size_t appliedTint_ = static_cast<size_t>(-1);
        float scale_ = 1.0f;
        float groundY_ = 0.0f;
        float yaw_ = 0.0f;
        bool visible_ = false;
        bool dead_ = false;
        const Assets::FRigClip* deathClip_ = nullptr;
        float deathSeconds_ = 0.0f;
        glm::vec2 flinch_{0.0f};          // x = pitch, y = roll (radians)
        glm::vec2 flinchVelocity_{0.0f};
    };
}
