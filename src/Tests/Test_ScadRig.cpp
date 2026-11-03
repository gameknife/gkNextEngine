#include <catch2/catch_all.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadLexer.h"
#include "Modules/ScadLoader/FScadParser.h"
#include "Modules/ScadLoader/FScadRig.h"
#include "Gameplay/Rig/RigPoseOps.h"
#include "Data/ItemDefs.hpp"
#include <algorithm>

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

using namespace Assets;
using namespace Assets::Scad;

namespace
{
    SceneEvalResult EvalRigScene(const std::string& src)
    {
        std::vector<Token> tokens;
        std::string err;
        REQUIRE(ScadLexer::Tokenize(src, tokens, err));

        Scope scope;
        REQUIRE(ScadParser::Parse(tokens, scope, err));

        std::unordered_map<std::string, StmtPtr> modules;
        std::unordered_map<std::string, StmtPtr> functions;
        Scope top;
        for (const StmtPtr& s : scope)
        {
            if (s->kind == StmtKind::ModuleDef) modules[s->name] = s;
            else if (s->kind == StmtKind::FunctionDef) functions[s->name] = s;
            else top.push_back(s);
        }

        ScadLoadOptions options;
        SceneEvalResult result;
        std::string evalErr;
        ScadEvaluator::EvaluateScene(top, modules, functions, options, result, evalErr);
        return result;
    }

    bool BuildRigFromSource(
        const std::string& src,
        FRigAsset& outAsset,
        std::vector<std::string>* warnings = nullptr,
        const ScadRigLoadOptions& options = {})
    {
        const SceneEvalResult result = EvalRigScene(src);
        std::string err;
        return FScadRigLoader::BuildRig(result, options, outAsset, err, warnings);
    }

    bool AnyWarningContains(const std::vector<std::string>& warnings, const std::string& needle)
    {
        for (const std::string& w : warnings)
        {
            if (w.find(needle) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    const char* kBasicRig =
        "module bone_head() { cube(0.2, center = true); }\n"
        "module bone_torso() {\n"
        "    cube([0.3, 0.2, 0.5], center = true);\n"
        "    translate([0, 0, 0.55]) bone_head();\n"
        "}\n"
        "module bone_root() { translate([0, 0, 0.84]) bone_torso(); }\n"
        "bone_root();\n";
}

TEST_CASE("ScadRig builds bone hierarchy with engine-space pivots", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(kBasicRig, asset, &warnings));

    REQUIRE(warnings.empty());
    REQUIRE(asset.bones.size() == 3);
    CHECK(asset.bones[0].name == "bone_root");
    CHECK(asset.bones[0].parent == -1);
    CHECK(asset.bones[1].name == "bone_torso");
    CHECK(asset.bones[1].parent == 0);
    CHECK(asset.bones[2].name == "bone_head");
    CHECK(asset.bones[2].parent == 1);
    REQUIRE(asset.bones[0].children == std::vector<int32_t>{1});
    REQUIRE(asset.bones[1].children == std::vector<int32_t>{2});

    // SCAD translate [0,0,z] (Z-up) becomes engine [0,z,0] (Y-up).
    CHECK(asset.bones[1].bindT.y == Catch::Approx(0.84f));
    CHECK(asset.bones[2].bindT.y == Catch::Approx(0.55f));
    CHECK(glm::length(asset.bones[0].bindT) == Catch::Approx(0.0f).margin(1e-6));

    CHECK(asset.FindBone("bone_head") == 2);
    CHECK(asset.FindBone("bone_missing") == -1);

    // bone_root has no geometry of its own; torso and head carry one part each.
    REQUIRE(asset.parts.size() == 2);
    REQUIRE(asset.partModels.size() == 2);
    for (const FRigPart& part : asset.parts)
    {
        CHECK(part.modelIndex >= 0);
        CHECK(part.sectionColors.size() == 1);
        CHECK_FALSE(part.sectionTintable[0]);
    }
}

TEST_CASE("ScadRig converts rotate pivots through the basis conjugation", "[Unit][ScadRig]")
{
    FRigAsset asset;
    REQUIRE(BuildRigFromSource(
        "module bone_arm() { cube(0.1, center = true); }\n"
        "module bone_root() { translate([0, 0, 1]) rotate([90, 0, 0]) bone_arm(); }\n"
        "bone_root();\n",
        asset));

    REQUIRE(asset.bones.size() == 2);
    const FRigBone& arm = asset.bones[1];
    CHECK(arm.bindT.y == Catch::Approx(1.0f).margin(1e-5));

    // SCAD Rx(90) conjugated to engine space still rotates +90 deg about engine X:
    // engine +Y maps to engine +Z.
    const glm::vec3 rotated = arm.bindR * glm::vec3(0.0f, 1.0f, 0.0f);
    CHECK(rotated.x == Catch::Approx(0.0f).margin(1e-5));
    CHECK(rotated.y == Catch::Approx(0.0f).margin(1e-5));
    CHECK(rotated.z == Catch::Approx(1.0f).margin(1e-5));
}

TEST_CASE("ScadRig collapses helper modules into the owning bone", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(
        "module part_box() { translate([1, 0, 0]) cube(0.1, center = true); }\n"
        "module bone_root() { part_box(); }\n"
        "bone_root();\n",
        asset, &warnings));

    CHECK(warnings.empty());
    REQUIRE(asset.bones.size() == 1);
    REQUIRE(asset.parts.size() == 1);
    REQUIRE(asset.partModels.size() == 1);

    // The helper's translate is baked into the part geometry.
    const Model& model = asset.partModels[0];
    const glm::vec3 center = (model.GetLocalAABBMin() + model.GetLocalAABBMax()) * 0.5f;
    CHECK(center.x == Catch::Approx(1.0f).margin(1e-4));
    CHECK(center.y == Catch::Approx(0.0f).margin(1e-4));
    CHECK(center.z == Catch::Approx(0.0f).margin(1e-4));
}

TEST_CASE("ScadRig warns on duplicate bone calls and keeps the first", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(
        "module bone_leg() { cube(0.1, center = true); }\n"
        "module bone_root() { bone_leg(); translate([1, 0, 0]) bone_leg(); }\n"
        "bone_root();\n",
        asset, &warnings));

    CHECK(asset.bones.size() == 2);
    CHECK(AnyWarningContains(warnings, "more than once"));
}

TEST_CASE("ScadRig warns on stray top-level geometry and pivot scale", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(
        "module bone_arm() { cube(0.1, center = true); }\n"
        "module bone_root() { scale([2, 1, 1]) bone_arm(); }\n"
        "cube(1);\n"
        "bone_root();\n",
        asset, &warnings));

    CHECK(AnyWarningContains(warnings, "ignored") );
    CHECK(AnyWarningContains(warnings, "scale"));
    REQUIRE(asset.bones.size() == 2);
}

TEST_CASE("ScadRig fails without a top-level bone call", "[Unit][ScadRig]")
{
    const SceneEvalResult result = EvalRigScene("cube(1);\n");
    FRigAsset asset;
    std::string err;
    REQUIRE_FALSE(FScadRigLoader::BuildRig(result, ScadRigLoadOptions{}, asset, err));
    CHECK(err.find("no top-level bone") != std::string::npos);
}

TEST_CASE("ScadRig parses clips with loop metadata and converts channels", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(
        std::string(kBasicRig) +
        "anim_walk = [\n"
        "    [\"bone_torso\", \"rot\", [[0, [90, 0, 0]], [0.4, [-90, 0, 0]], [0.8, [90, 0, 0]]]],\n"
        "    [\"bone_root\",  \"pos\", [[0, [0, 0, 0]], [0.2, [0, 0, 0.03]], [0.8, [0, 0, 0]]]],\n"
        "];\n"
        "anim_sit = [\n"
        "    [\"loop\", false],\n"
        "    [\"bone_root\", \"pos\", [[0, [0, 0, -0.42]]]],\n"
        "];\n",
        asset, &warnings));

    CHECK(warnings.empty());
    REQUIRE(asset.clips.size() == 2);

    const FRigClip* walk = asset.FindClip("walk");
    REQUIRE(walk != nullptr);
    CHECK(walk->loop);
    CHECK(walk->duration == Catch::Approx(0.8f));
    REQUIRE(walk->channels.size() == 2);

    // rot channel: euler 90 deg about SCAD X -> engine quaternion, slerp-able.
    const FRigChannel* torso = nullptr;
    const FRigChannel* root = nullptr;
    for (const FRigChannel& c : walk->channels)
    {
        if (asset.bones[c.bone].name == "bone_torso") torso = &c;
        if (asset.bones[c.bone].name == "bone_root") root = &c;
    }
    REQUIRE(torso != nullptr);
    REQUIRE(root != nullptr);
    REQUIRE(torso->rotation.Keys.size() == 3);
    const glm::vec3 rotated = torso->rotation.Keys[0].Value * glm::vec3(0.0f, 1.0f, 0.0f);
    CHECK(rotated.z == Catch::Approx(1.0f).margin(1e-5));

    // pos channel: SCAD [0,0,0.03] -> engine [0,0.03,0].
    REQUIRE(root->position.Keys.size() == 3);
    CHECK(root->position.Keys[1].Value.y == Catch::Approx(0.03f).margin(1e-6));

    // Sampling midway between key 0 and 1 lerps the position.
    FRigChannel& mutableRoot = const_cast<FRigChannel&>(*root);
    const glm::vec3 sampled = mutableRoot.position.Sample(0.1f);
    CHECK(sampled.y == Catch::Approx(0.015f).margin(1e-6));

    const FRigClip* sit = asset.FindClip("sit");
    REQUIRE(sit != nullptr);
    CHECK_FALSE(sit->loop);
    CHECK(sit->duration == Catch::Approx(0.0f));
    REQUIRE(sit->channels.size() == 1);
    REQUIRE(sit->channels[0].position.Keys.size() == 1);
    // Single-key channel samples its only key.
    FRigChannel& mutableSit = const_cast<FRigChannel&>(sit->channels[0]);
    CHECK(mutableSit.position.Sample(0.5f).y == Catch::Approx(-0.42f).margin(1e-6));
}

TEST_CASE("ScadRig drops channels for unknown bones with a warning", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(
        std::string(kBasicRig) +
        "anim_test = [\n"
        "    [\"bone_ghost\", \"rot\", [[0, [10, 0, 0]]]],\n"
        "    [\"bone_torso\", \"warp\", [[0, [10, 0, 0]]]],\n"
        "];\n",
        asset, &warnings));

    CHECK(AnyWarningContains(warnings, "unknown bone"));
    CHECK(AnyWarningContains(warnings, "unknown channel"));
    REQUIRE(asset.clips.size() == 1);
    CHECK(asset.clips[0].channels.empty());
}

TEST_CASE("ScadRig supports procedural keyframes via list comprehension", "[Unit][ScadRig]")
{
    FRigAsset asset;
    REQUIRE(BuildRigFromSource(
        std::string(kBasicRig) +
        "anim_idle = [\n"
        "    [\"bone_torso\", \"rot\", [for (t = [0 : 0.5 : 2]) [t, [2 * sin(180 * t), 0, 0]]]],\n"
        "];\n",
        asset));

    const FRigClip* idle = asset.FindClip("idle");
    REQUIRE(idle != nullptr);
    CHECK(idle->duration == Catch::Approx(2.0f));
    REQUIRE(idle->channels.size() == 1);
    CHECK(idle->channels[0].rotation.Keys.size() == 5);
}

TEST_CASE("ScadRig marks tint placeholder sections as tintable", "[Unit][ScadRig]")
{
    FRigAsset asset;
    REQUIRE(BuildRigFromSource(
        "ROLECOLOR = [1, 0, 1];\n"
        "module bone_root() {\n"
        "    color(ROLECOLOR) cube([0.3, 0.2, 0.5], center = true);\n"
        "    color([0.2, 0.2, 0.2]) translate([0, 0, -0.5]) cube(0.2, center = true);\n"
        "}\n"
        "bone_root();\n",
        asset));

    REQUIRE(asset.parts.size() == 1);
    const FRigPart& part = asset.parts[0];
    REQUIRE(part.sectionColors.size() == 2);
    REQUIRE(part.sectionTintable.size() == 2);

    int tintable = 0;
    for (size_t i = 0; i < part.sectionTintable.size(); ++i)
    {
        if (part.sectionTintable[i])
        {
            ++tintable;
            CHECK(part.sectionColors[i].r == Catch::Approx(1.0f));
            CHECK(part.sectionColors[i].g == Catch::Approx(0.0f));
            CHECK(part.sectionColors[i].b == Catch::Approx(1.0f));
        }
    }
    CHECK(tintable == 1);
}

TEST_CASE("ScadRig bakes mirror inside the bone body into geometry", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(
        "module part_arm() { translate([0.5, 0, 0]) cube(0.1, center = true); }\n"
        "module bone_arm_l() { part_arm(); }\n"
        "module bone_arm_r() { mirror([1, 0, 0]) part_arm(); }\n"
        "module bone_root() {\n"
        "    translate([-0.2, 0, 1]) bone_arm_l();\n"
        "    translate([ 0.2, 0, 1]) bone_arm_r();\n"
        "}\n"
        "bone_root();\n",
        asset, &warnings));

    CHECK(warnings.empty());
    REQUIRE(asset.bones.size() == 3);
    REQUIRE(asset.parts.size() == 2);

    // bone_arm_r's mirrored geometry sits at -0.5 on engine X (pivot local).
    const Model& right = asset.partModels[asset.parts[1].modelIndex];
    const glm::vec3 center = (right.GetLocalAABBMin() + right.GetLocalAABBMax()) * 0.5f;
    CHECK(center.x == Catch::Approx(-0.5f).margin(1e-4));
    for (const Vertex& vertex : right.CPUVertices())
    {
        CHECK(glm::dot(vertex.Normal, vertex.Position - center) > 0.0f);
    }
}

TEST_CASE("ScadRig loads the shipped agent_basic character", "[Unit][ScadRig]")
{
    FRigAsset asset;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/agent_basic.scad",
                                    ScadRigLoadOptions{}, asset, err, &warnings));

    CHECK(warnings.empty());
    REQUIRE(asset.bones.size() == 7);
    CHECK(asset.bones[0].name == "bone_root");
    CHECK(asset.FindBone("bone_torso") >= 0);
    CHECK(asset.FindBone("bone_head") >= 0);
    CHECK(asset.FindBone("bone_arm_l") >= 0);
    CHECK(asset.FindBone("bone_arm_r") >= 0);
    CHECK(asset.FindBone("bone_leg_l") >= 0);
    CHECK(asset.FindBone("bone_leg_r") >= 0);

    for (const char* clip : {"idle", "walk", "sit", "work"})
    {
        const FRigClip* c = asset.FindClip(clip);
        REQUIRE(c != nullptr);
        CHECK_FALSE(c->channels.empty());
    }
    CHECK(asset.FindClip("walk")->duration == Catch::Approx(0.8f));
    CHECK(asset.FindClip("walk")->loop);
    CHECK_FALSE(asset.FindClip("sit")->loop);

    // Budget: < 500 triangles total.
    size_t triangles = 0;
    for (const Model& model : asset.partModels)
    {
        triangles += model.NumberOfIndices() / 3;
    }
    CHECK(triangles < 500);
    CHECK(triangles > 0);

    // Height ~1.6-1.8 m: head part's local top + accumulated pivots.
    // Cheap sanity: head bone pivot stacks to about 1.38 in engine Y.
    const FRigBone& torso = asset.bones[asset.FindBone("bone_torso")];
    const FRigBone& head = asset.bones[asset.FindBone("bone_head")];
    CHECK(torso.bindT.y + head.bindT.y == Catch::Approx(1.38f).margin(0.05f));

    // Tintable sections exist (torso + sleeves).
    int tintableSections = 0;
    for (const FRigPart& part : asset.parts)
    {
        for (bool t : part.sectionTintable)
        {
            if (t) ++tintableSections;
        }
    }
    CHECK(tintableSections >= 3);
}

TEST_CASE("ScadRig loads the shipped NextRA soldier", "[Unit][ScadRig][NextRA]")
{
    FRigAsset asset;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/next_ra_soldier.scad",
                                    ScadRigLoadOptions{}, asset, err, &warnings));

    CHECK(warnings.empty());
    REQUIRE(asset.bones.size() == 7);
    CHECK(asset.bones[0].name == "bone_root");

    for (const char* clip : {"idle", "walk", "fire"})
    {
        const FRigClip* c = asset.FindClip(clip);
        REQUIRE(c != nullptr);
        CHECK_FALSE(c->channels.empty());
    }
    CHECK(asset.FindClip("walk")->duration == Catch::Approx(0.8f));
    CHECK(asset.FindClip("walk")->loop);
    CHECK_FALSE(asset.FindClip("fire")->loop);

    size_t triangles = 0;
    int tintableSections = 0;
    for (const Model& model : asset.partModels)
    {
        triangles += model.NumberOfIndices() / 3;
    }
    for (const FRigPart& part : asset.parts)
    {
        for (bool tintable : part.sectionTintable)
        {
            if (tintable) ++tintableSections;
        }
    }
    CHECK(triangles > 0);
    CHECK(triangles < 600);
    CHECK(tintableSections >= 4);

    const FRigBone& torso = asset.bones[asset.FindBone("bone_torso")];
    const FRigBone& head = asset.bones[asset.FindBone("bone_head")];
    CHECK(torso.bindT.y + head.bindT.y == Catch::Approx(1.44f).margin(0.02f));
}

TEST_CASE("ScadRig loads the articulated NextDayz survivor contract", "[Unit][ScadRig][NextDayz]")
{
    FRigAsset asset;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/nextdayz_survivor.scad",
                                    ScadRigLoadOptions{}, asset, err, &warnings));

    for (const std::string& warning : warnings)
    {
        UNSCOPED_INFO("warning: " << warning);
    }
    CHECK(warnings.empty());
    REQUIRE(asset.bones.size() == 20);
    for (const char* bone : {
             "bone_root", "bone_pelvis", "bone_torso", "bone_head",
             "bone_upperarm_l", "bone_forearm_l", "bone_hand_l",
             "bone_upperarm_r", "bone_forearm_r", "bone_hand_r", "bone_weapon_socket",
             "bone_ik_weapon", "bone_ik_hand_r", "bone_ik_hand_l",
             "bone_thigh_l", "bone_calf_l", "bone_foot_l",
             "bone_thigh_r", "bone_calf_r", "bone_foot_r"})
    {
        CHECK(asset.FindBone(bone) >= 0);
    }

    const int32_t socket = asset.FindBone("bone_weapon_socket");
    const int32_t ikWeapon = asset.FindBone("bone_ik_weapon");
    REQUIRE(socket >= 0);
    REQUIRE(ikWeapon >= 0);
    CHECK(asset.bones[socket].parent == asset.FindBone("bone_hand_r"));
    CHECK(asset.bones[ikWeapon].parent == asset.FindBone("bone_torso"));
    CHECK(asset.bones[asset.FindBone("bone_ik_hand_r")].parent == ikWeapon);
    CHECK(asset.bones[asset.FindBone("bone_ik_hand_l")].parent == ikWeapon);
    for (const FRigPart& part : asset.parts)
    {
        const std::string& owner = asset.bones[part.bone].name;
        CHECK(owner != "bone_weapon_socket");
        CHECK(owner.rfind("bone_ik_", 0) != 0);
    }

    // The firing side is the character's right: engine -X when facing +Z, the
    // side the over-the-shoulder camera sits on.
    CHECK(asset.bones[asset.FindBone("bone_upperarm_r")].bindT.x < 0.0f);
    CHECK(asset.bones[asset.FindBone("bone_thigh_r")].bindT.x < 0.0f);

    const std::vector<std::string> loopClips = {
        "stand_idle",
        "stand_walk_f", "stand_walk_b", "stand_walk_l", "stand_walk_r",
        "stand_run_f", "stand_run_b", "stand_run_l", "stand_run_r",
        "stand_sprint_f", "stand_sprint_b", "stand_sprint_l", "stand_sprint_r",
        "crouch_idle", "crouch_walk_f", "crouch_walk_b", "crouch_walk_l", "crouch_walk_r",
        "jump_air_loop",
        "weapon_ready", "weapon_sprint", "weapon_low", "aim_rifle_down", "aim_rifle_center", "aim_rifle_up"};
    for (const std::string& clipName : loopClips)
    {
        const FRigClip* clip = asset.FindClip(clipName);
        REQUIRE(clip != nullptr);
        CHECK(clip->loop);
        CHECK_FALSE(clip->channels.empty());
    }
    for (const char* clipName : {
             "jump_up", "jump_down", "vault", "climb_up",
             "reload_rifle", "switch_weapon", "recoil_rifle", "loot_ground"})
    {
        const FRigClip* clip = asset.FindClip(clipName);
        REQUIRE(clip != nullptr);
        CHECK_FALSE(clip->loop);
        CHECK(clip->duration > 0.0f);
    }

    auto checkDirectionalDurations = [&asset](std::string_view prefix)
    {
        const FRigClip* forward = asset.FindClip(std::string(prefix) + "_f");
        REQUIRE(forward != nullptr);
        for (const char* direction : {"_b", "_l", "_r"})
        {
            const FRigClip* clip = asset.FindClip(std::string(prefix) + direction);
            REQUIRE(clip != nullptr);
            CHECK(clip->duration == Catch::Approx(forward->duration).epsilon(0.02));
        }
    };
    checkDirectionalDurations("stand_walk");
    checkDirectionalDurations("stand_run");
    checkDirectionalDurations("stand_sprint");
    checkDirectionalDurations("crouch_walk");

    const int32_t root = asset.FindBone("bone_root");
    for (const FRigClip& clip : asset.clips)
    {
        for (const FRigChannel& channel : clip.channels)
        {
            if (channel.bone != root)
            {
                continue;
            }
            for (const auto& key : channel.position.Keys)
            {
                CHECK(std::abs(key.Value.x) < 0.02f);
                CHECK(std::abs(key.Value.z) < 0.02f);
            }
        }
    }

    const FRigClip* recoil = asset.FindClip("recoil_rifle");
    REQUIRE(recoil != nullptr);
    for (const FRigChannel& channel : recoil->channels)
    {
        if (channel.rotation.Keys.empty())
        {
            continue;
        }
        CHECK(glm::abs(glm::dot(channel.rotation.Keys.front().Value, glm::quat(1, 0, 0, 0))) ==
              Catch::Approx(1.0f).margin(1.0e-5f));
        CHECK(glm::abs(glm::dot(channel.rotation.Keys.back().Value, glm::quat(1, 0, 0, 0))) ==
              Catch::Approx(1.0f).margin(1.0e-5f));
    }

    size_t triangles = 0;
    for (const Model& model : asset.partModels)
    {
        triangles += model.NumberOfIndices() / 3;
    }
    INFO("NextDayz survivor triangles: " << triangles);
    CHECK(triangles > 0);
    CHECK(triangles < 1500);
}

namespace
{
    // Samples one clip at `time` into bind-relative offsets (same rules as the animators).
    NextGameplay::FRigPose SampleSurvivorClip(const FRigAsset& asset, const FRigClip& clip, float time)
    {
        NextGameplay::FRigPose pose;
        pose.Reset(asset.bones.size());
        for (const FRigChannel& channel : clip.channels)
        {
            NextGameplay::FRigBonePose& bone = pose.bones[channel.bone];
            if (!channel.position.Keys.empty())
            {
                bone.t = channel.position.Sample(time);
            }
            if (!channel.rotation.Keys.empty())
            {
                bone.r = glm::normalize(channel.rotation.Sample(time));
            }
            if (!channel.scale.Keys.empty())
            {
                bone.s = channel.scale.Sample(time);
            }
        }
        return pose;
    }

    // Lowest boot-sole corner in rig space (engine Y up, ground at 0).
    float SoleHeight(const std::vector<glm::mat4>& transforms, int32_t foot, float halfWidth = 0.085f,
                     float toe = 0.225f, float heel = 0.085f)
    {
        float lowest = 1.0e9f;
        for (const glm::vec3& corner : {glm::vec3(halfWidth, 0.0f, toe), glm::vec3(-halfWidth, 0.0f, toe),
                                        glm::vec3(halfWidth, 0.0f, -heel), glm::vec3(-halfWidth, 0.0f, -heel)})
        {
            lowest = std::min(lowest, glm::vec3(transforms[foot] * glm::vec4(corner, 1.0f)).y);
        }
        return lowest;
    }
}

TEST_CASE("NextDayz survivor gaits plant the boots and holds keep both hands on the rifle",
          "[Unit][ScadRig][NextDayz]")
{
    FRigAsset asset;
    std::string err;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/nextdayz_survivor.scad", ScadRigLoadOptions{}, asset,
                                    err));
    const int32_t footL = asset.FindBone("bone_foot_l");
    const int32_t footR = asset.FindBone("bone_foot_r");
    std::vector<glm::mat4> transforms;

    const std::vector<std::pair<std::string, float>> gaits = {
        {"stand_walk", 0.60f}, {"stand_run", 0.33f}, {"stand_sprint", 0.24f}, {"crouch_walk", 0.62f}};
    for (const auto& [prefix, duty] : gaits)
    {
        for (const char* direction : {"_f", "_b", "_l", "_r"})
        {
            const std::string name = prefix + direction;
            const FRigClip* clip = asset.FindClip(name);
            REQUIRE(clip != nullptr);
            constexpr int samples = 64;
            float lowest = 1.0e9f;
            int plantedL = 0;
            int plantedR = 0;
            for (int i = 0; i < samples; ++i)
            {
                const NextGameplay::FRigPose pose =
                    SampleSurvivorClip(asset, *clip, clip->duration * static_cast<float>(i) / samples);
                NextGameplay::ComputeRigComponentTransforms(asset, pose, transforms);
                const float soleL = SoleHeight(transforms, footL);
                const float soleR = SoleHeight(transforms, footR);
                lowest = std::min({lowest, soleL, soleR});
                plantedL += soleL < 0.02f ? 1 : 0;
                plantedR += soleR < 0.02f ? 1 : 0;
            }
            INFO(name << ": lowest sole " << lowest << ", planted L " << plantedL << " R " << plantedR << " of "
                      << samples);
            CHECK(lowest > -0.02f);
            CHECK(lowest < 0.02f);
            // A boot that floats through part of its stance reads as skating.
            CHECK(plantedL >= static_cast<int>(samples * duty * 0.7f));
            CHECK(plantedR >= static_cast<int>(samples * duty * 0.7f));
        }
    }

    const int32_t socket = asset.FindBone("bone_weapon_socket");
    const int32_t ikWeapon = asset.FindBone("bone_ik_weapon");
    const int32_t handL = asset.FindBone("bone_hand_l");
    const int32_t ikHandL = asset.FindBone("bone_ik_hand_l");
    for (const char* name : {"weapon_ready", "aim_rifle_center", "aim_rifle_up", "aim_rifle_down", "weapon_sprint"})
    {
        const FRigClip* clip = asset.FindClip(name);
        REQUIRE(clip != nullptr);
        const NextGameplay::FRigPose pose = SampleSurvivorClip(asset, *clip, 0.0f);
        NextGameplay::ComputeRigComponentTransforms(asset, pose, transforms);
        const float weaponGap = glm::length(NextGameplay::RigBonePosition(transforms, socket) -
                                            NextGameplay::RigBonePosition(transforms, ikWeapon));
        const float weaponTwist = std::abs(glm::dot(NextGameplay::RigBoneRotation(transforms, socket),
                                                    NextGameplay::RigBoneRotation(transforms, ikWeapon)));
        const float supportGap = glm::length(NextGameplay::RigBonePosition(transforms, handL) -
                                             NextGameplay::RigBonePosition(transforms, ikHandL));
        INFO(name << ": weapon gap " << weaponGap << ", twist dot " << weaponTwist << ", support gap "
                  << supportGap);
        CHECK(weaponGap < 0.02f);
        CHECK(weaponTwist > 0.995f);
        CHECK(supportGap < 0.04f);
    }
}

TEST_CASE("NextDayz infected gaits plant the feet and actions stay on the ground", "[Unit][ScadRig][NextDayz]")
{
    FRigAsset asset;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/nextdayz_infected.scad", ScadRigLoadOptions{}, asset,
                                    err, &warnings));
    CHECK(warnings.empty());
    CHECK(asset.bones.size() == 17);
    for (const char* bone : {"bone_root", "bone_pelvis", "bone_torso", "bone_head", "bone_jaw", "bone_upperarm_l",
                             "bone_forearm_l", "bone_hand_l", "bone_upperarm_r", "bone_forearm_r", "bone_hand_r",
                             "bone_thigh_l", "bone_calf_l", "bone_foot_l", "bone_thigh_r", "bone_calf_r",
                             "bone_foot_r"})
    {
        INFO(bone);
        CHECK(asset.FindBone(bone) >= 0);
    }
    CHECK(asset.bones[asset.FindBone("bone_jaw")].parent == asset.FindBone("bone_head"));
    // Right side at -X, like the survivor.
    CHECK(asset.bones[asset.FindBone("bone_thigh_r")].bindT.x < 0.0f);
    CHECK(asset.bones[asset.FindBone("bone_upperarm_r")].bindT.x < 0.0f);
    for (const char* name : {"idle", "walk", "walk_limp", "run", "hobble", "reach", "alert", "attack_lunge",
                             "attack_swipe", "stagger", "death_back", "death_forward", "death_crumple", "attack",
                             "hit", "die"})
    {
        INFO(name);
        CHECK(asset.FindClip(name) != nullptr);
    }

    const int32_t footL = asset.FindBone("bone_foot_l");
    const int32_t footR = asset.FindBone("bone_foot_r");
    const auto sole = [&](const std::vector<glm::mat4>& transforms, int32_t foot)
    {
        return SoleHeight(transforms, foot, 0.075f, 0.21f, 0.08f);
    };
    std::vector<glm::mat4> transforms;
    const std::vector<std::pair<std::string, float>> gaits = {
        {"walk", 0.68f}, {"walk_limp", 0.70f}, {"run", 0.38f}, {"hobble", 0.52f}};
    for (const auto& [name, duty] : gaits)
    {
        const FRigClip* clip = asset.FindClip(name);
        REQUIRE(clip != nullptr);
        CHECK(clip->loop);
        constexpr int samples = 64;
        float lowest = 1.0e9f;
        int plantedL = 0;
        int plantedR = 0;
        for (int i = 0; i < samples; ++i)
        {
            const NextGameplay::FRigPose pose =
                SampleSurvivorClip(asset, *clip, clip->duration * static_cast<float>(i) / samples);
            NextGameplay::ComputeRigComponentTransforms(asset, pose, transforms);
            const float soleL = sole(transforms, footL);
            const float soleR = sole(transforms, footR);
            lowest = std::min({lowest, soleL, soleR});
            plantedL += soleL < 0.02f ? 1 : 0;
            plantedR += soleR < 0.02f ? 1 : 0;
        }
        INFO(name << ": lowest sole " << lowest << ", planted L " << plantedL << " R " << plantedR);
        CHECK(lowest > -0.02f);
        CHECK(lowest < 0.02f);
        CHECK(plantedL >= static_cast<int>(samples * duty * 0.7f));
        CHECK(plantedR >= static_cast<int>(samples * duty * 0.7f));
    }

    // Full-body actions never push a foot through the ground.
    for (const char* name : {"idle", "alert", "attack_lunge", "attack_swipe", "stagger", "death_back",
                             "death_forward", "death_crumple"})
    {
        const FRigClip* clip = asset.FindClip(name);
        REQUIRE(clip != nullptr);
        float lowest = 1.0e9f;
        float lowestTime = 0.0f;
        for (int i = 0; i <= 48; ++i)
        {
            const float time = clip->duration * i / 48.0f;
            const NextGameplay::FRigPose pose = SampleSurvivorClip(asset, *clip, time);
            NextGameplay::ComputeRigComponentTransforms(asset, pose, transforms);
            const float height = std::min(sole(transforms, footL), sole(transforms, footR));
            if (height < lowest)
            {
                lowest = height;
                lowestTime = time;
            }
        }
        INFO(name << ": lowest sole " << lowest << " at " << lowestTime << " s");
        CHECK(lowest > -0.03f);
    }

    // Deaths end lying down: pelvis and head on the ground, not floating.
    const int32_t pelvis = asset.FindBone("bone_pelvis");
    const int32_t head = asset.FindBone("bone_head");
    for (const char* name : {"death_back", "death_forward", "death_crumple"})
    {
        const FRigClip* clip = asset.FindClip(name);
        REQUIRE(clip != nullptr);
        CHECK_FALSE(clip->loop);
        const NextGameplay::FRigPose pose = SampleSurvivorClip(asset, *clip, clip->duration);
        NextGameplay::ComputeRigComponentTransforms(asset, pose, transforms);
        const glm::vec3 pelvisPosition = NextGameplay::RigBonePosition(transforms, pelvis);
        const glm::vec3 headPosition = NextGameplay::RigBonePosition(transforms, head);
        INFO(name << ": pelvis y " << pelvisPosition.y << ", head y " << headPosition.y);
        CHECK(pelvisPosition.y > 0.05f);
        CHECK(pelvisPosition.y < 0.25f);
        CHECK(headPosition.y > 0.0f);
        CHECK(headPosition.y < 0.30f);
    }

    // Both attacks put a hand well in front of the body on the strike frame.
    const int32_t handL = asset.FindBone("bone_hand_l");
    const int32_t handR = asset.FindBone("bone_hand_r");
    for (const auto& [name, strike] : std::vector<std::pair<std::string, float>>{{"attack_lunge", 0.45f},
                                                                                  {"attack_swipe", 0.42f}})
    {
        const FRigClip* clip = asset.FindClip(name);
        REQUIRE(clip != nullptr);
        const NextGameplay::FRigPose pose = SampleSurvivorClip(asset, *clip, strike);
        NextGameplay::ComputeRigComponentTransforms(asset, pose, transforms);
        const glm::vec3 left = NextGameplay::RigBonePosition(transforms, handL);
        const glm::vec3 right = NextGameplay::RigBonePosition(transforms, handR);
        const glm::vec3 lead = left.z > right.z ? left : right;
        INFO(name << ": hand reach " << lead.z << " at height " << lead.y);
        CHECK(lead.z > 0.55f);
        // Chest height of the player it is grabbing for, not its knees.
        CHECK(lead.y > 0.85f);
    }

    size_t triangles = 0;
    for (const Model& model : asset.partModels)
    {
        triangles += model.NumberOfIndices() / 3;
    }
    INFO("NextDayz infected triangles: " << triangles);
    CHECK(triangles < 1500);
}

// kit_char 组装角色：use <../lib/kit_char.scad> 闭包 + 库函数 clip 的完整链路。
static void CheckKitCharacter(const char* path, int expectedTintable)
{
    FRigAsset asset;
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig(path, ScadRigLoadOptions{}, asset, err, &warnings));

    CHECK(warnings.empty());
    REQUIRE(asset.bones.size() == 7);
    CHECK(asset.bones[0].name == "bone_root");
    for (const char* bone : {"bone_torso", "bone_head", "bone_arm_l", "bone_arm_r",
                             "bone_leg_l", "bone_leg_r"})
    {
        CHECK(asset.FindBone(bone) >= 0);
    }

    // clip 来自 kit 函数（anim_x = ch_clip_x()），与 agent_basic 语义一致。
    for (const char* clip : {"idle", "walk", "sit", "work", "wave"})
    {
        const FRigClip* c = asset.FindClip(clip);
        REQUIRE(c != nullptr);
        CHECK_FALSE(c->channels.empty());
    }
    CHECK(asset.FindClip("walk")->duration == Catch::Approx(0.8f));
    CHECK(asset.FindClip("walk")->loop);
    CHECK_FALSE(asset.FindClip("sit")->loop);

    // 骨架标准 pivot：torso(0.84) + head(0.54) = 1.38（引擎 Y）。
    const FRigBone& torso = asset.bones[asset.FindBone("bone_torso")];
    const FRigBone& head = asset.bones[asset.FindBone("bone_head")];
    CHECK(torso.bindT.y + head.bindT.y == Catch::Approx(1.38f).margin(0.02f));

    size_t triangles = 0;
    for (const Model& model : asset.partModels)
    {
        triangles += model.NumberOfIndices() / 3;
    }
    CHECK(triangles > 0);
    CHECK(triangles < 900);

    int tintableSections = 0;
    for (const FRigPart& part : asset.parts)
    {
        for (bool tintable : part.sectionTintable)
        {
            if (tintable) ++tintableSections;
        }
    }
    CHECK(tintableSections >= expectedTintable);
}

TEST_CASE("ScadRig loads the kit_char worker character", "[Unit][ScadRig][KitChar]")
{
    CheckKitCharacter("assets/scad/characters/worker.scad", 3); // 背心 + 双手套袖
}

TEST_CASE("ScadRig loads the kit_char citizen character", "[Unit][ScadRig][KitChar]")
{
    CheckKitCharacter("assets/scad/characters/citizen.scad", 3); // 连衣裙 + 双袖
}

TEST_CASE("NextDayz loot catalog has a world model for every item", "[Unit][ScadRig][NextDayz]")
{
    using namespace NextDayz;
    FRigAsset asset;
    std::string error;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/nextdayz_loot_items.scad", {}, asset, error,
                                            &warnings));
    for (const FItemDef& definition : AllItemDefs())
    {
        INFO(definition.id);
        const int32_t bone = asset.FindBone("bone_item_" + std::string(definition.id));
        REQUIRE(bone >= 0);
        REQUIRE(std::any_of(asset.parts.begin(), asset.parts.end(),
                            [bone](const FRigPart& part) { return part.bone == bone; }));
    }
}
