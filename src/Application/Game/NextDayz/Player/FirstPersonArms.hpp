#pragma once

// ============================================================================
// FirstPersonArms.hpp - The survivor's own arms (skin and the sleeves of the
// coat being worn) holding the first-person weapon.
//
// Everything hangs under the view model node: it shares its scale, its
// visibility and its same-frame proxy patch, so the hands can never lag the
// gun. The hands are dedicated first-person models (nextdayz_fps_hands.scad)
// placed on each gun's grip and handguard; the forearms are the third-person
// rig's own forearm parts (bare skin or the sleeves of the coat worn), slimmed
// for the close view. Each frame both arms are solved in "eye space"
// (camera-local, +X left / +Y up / +Z forward, rig units): shoulders sit at
// fixed points beside and below the eye, the forearms reach the hands' wrists.
// Wherever the view model goes -- hip, sight, sprint carry, reload dip -- the
// arms follow it. Upper arms stay out of the picture (behind the eye).
// ============================================================================

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigLayeredAnimator.h"

namespace Assets
{
    class Node;
    class Scene;
}

namespace NextDayz
{
    struct FFirstPersonArmPart
    {
        int32_t bone = -1;
        uint32_t modelId = 0;
        std::array<uint32_t, 16> materials{};
        std::string itemId; // empty: the body's own part (skin)
    };

    // Recorded by PlayerRigVisual when it injects the rig (BeforeSceneRebuild).
    struct FFirstPersonArmAssets
    {
        const Assets::FRigAsset* rig = nullptr;
        std::vector<FFirstPersonArmPart> parts;
        float weaponScale = 0.9f; // third-person weapon size relative to the body
        // First-person hands (bone_fps_hand_grip / _support), injected by the game.
        std::array<uint32_t, 2> handModels{};
        std::array<std::array<uint32_t, 16>, 2> handMaterials{};
        bool handsReady = false;
    };

    class FirstPersonArms
    {
    public:
        void SetAssets(FFirstPersonArmAssets assets);
        void Bind(Assets::Scene& scene, const std::shared_ptr<Assets::Node>& viewModel);
        void Clear();
        void SetClothing(const std::vector<std::string>& equipped);

        // weaponInEye: the view-model weapon frame relative to the eye
        // (camera-local axes, metres, including the view-model scale).
        // supportHand = false leaves the left arm out (one-handed melee).
        void Update(std::string_view weaponId, const glm::mat4& weaponInEye, float viewModelScale, bool visible,
                    bool supportHand);

        const std::vector<std::shared_ptr<Assets::Node>>& Nodes() const { return nodes_; }
        int VisibleCount() const;
        // Worst wrist distance from its hand this frame (rig units); 0 when hidden.
        float GripError() const { return gripError_; }
        const std::array<std::shared_ptr<Assets::Node>, 2>& HandNodes() const { return hands_; }

    private:
        struct FBones
        {
            int32_t upperArmL = -1, foreArmL = -1, handL = -1;
            int32_t upperArmR = -1, foreArmR = -1, handR = -1;
        };

        bool PartWanted(const FFirstPersonArmPart& part, bool supportHand) const;

        FFirstPersonArmAssets assets_;
        FBones bones_{};
        bool valid_ = false;
        Assets::Scene* scene_ = nullptr;
        std::weak_ptr<Assets::Node> parent_;
        std::vector<std::shared_ptr<Assets::Node>> nodes_; // one per assets_.parts entry, then the two hands
        std::vector<bool> inScene_;
        std::array<std::shared_ptr<Assets::Node>, 2> hands_{};
        std::vector<std::string> equipped_;
        bool coat_ = false;
        NextGameplay::FRigPose pose_;
        std::vector<glm::mat4> transforms_;
        float gripError_ = 0.0f;
    };
}
