#include "Engine/Common/CoreMinimal.hpp"

#include <catch2/catch_all.hpp>

#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Data/RigAsset.hpp"
#include "Engine/Assets/Loaders/ProceduralTexture.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadLexer.h"
#include "Modules/ScadLoader/FScadParser.h"
#include "Modules/ScadLoader/FScadRig.h"
#include "Modules/ScadLoader/FScadTexturePrints.h"
#include "Gameplay/Rig/RigPoseOps.h"
#include "Data/ItemDefs.hpp"
#include "Weapons/WeaponDefs.hpp"
#include "Weapons/AttachmentDefs.hpp"
#include <algorithm>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

    // A named print from the SCAD print library (tex_<name>() in gk_texgraph.scad).
    FProceduralTexture PrintNamed(std::string_view name)
    {
        std::string error;
        const auto texture = MakeTexturePrint(name, error);
        INFO(error);
        REQUIRE(texture);
        return *texture;
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

TEST_CASE("ScadRig keeps gk_material surfaces as their own sections", "[Unit][ScadRig]")
{
    FRigAsset asset;
    REQUIRE(BuildRigFromSource(
        "module bone_root() {\n"
        "    gk_material([0.2, 0.2, 0.2], roughness = 0.3, metalness = 0.9) cube(0.1, center = true);\n"
        "    color([0.2, 0.2, 0.2]) translate([0, 0, 0.3]) cube(0.1, center = true);\n"
        "}\n"
        "bone_root();\n",
        asset));

    // Same RGB, different surface: two sections, not one merged Lambertian.
    REQUIRE(asset.parts.size() == 1);
    const FRigPart& part = asset.parts[0];
    REQUIRE(part.sectionColors.size() == 2);
    REQUIRE(part.sectionSurfaces.size() == 2);

    std::vector<FMaterial> materials;
    int metal = 0;
    int lambert = 0;
    for (size_t section = 0; section < part.sectionSurfaces.size(); ++section)
    {
        const glm::vec2 surface = part.SectionSurface(section);
        const Material& material =
            materials[SceneBuilder::AddRigSectionMaterial(materials, part, section)].gpuMaterial_;
        if (surface.y > 0.5f)
        {
            ++metal;
            CHECK(surface.x == Catch::Approx(0.3f).margin(1e-3));
            CHECK(surface.y == Catch::Approx(0.9f).margin(1e-3));
            CHECK(material.MaterialModel == Material::Enum::Mixture);
            CHECK(material.Metalness == Catch::Approx(0.9f).margin(1e-3));
        }
        else
        {
            ++lambert;
            CHECK(surface.x == Catch::Approx(1.0f));
            CHECK(material.MaterialModel == Material::Enum::Lambertian);
        }
    }
    CHECK(metal == 1);
    CHECK(lambert == 1);
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
             "jump_up", "jump_down", "vault", "mantle", "climb", "climb_over",
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
    // Lofted body forms (nextdayz_body.scad): tapered limbs, a shaped head and hands.
    // The box body was under 1500; this stays a low-poly budget, just not a toy one.
    CHECK(triangles < 3200);
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

TEST_CASE("NextDayz wardrobe covers every clothing item with rigid local bone parts", "[Unit][ScadRig][NextDayz][Clothing]")
{
    FRigAsset body;
    FRigAsset wardrobe;
    std::string error;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/nextdayz_survivor.scad", {}, body, error));
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/nextdayz_clothing.scad", {}, wardrobe, error, &warnings));
    CHECK(warnings.empty());
    std::unordered_map<std::string, size_t> partCounts;
    std::unordered_map<std::string, glm::vec3> boundsMin;
    std::unordered_map<std::string, glm::vec3> boundsMax;
    for (const FRigPart& part : wardrobe.parts)
    {
        const FRigBone& bone = wardrobe.bones[part.bone];
        const std::string& name = bone.name;
        INFO(name);
        REQUIRE(name.starts_with("bone_"));
        const size_t separator = name.find("__");
        REQUIRE(separator != std::string::npos);
        const std::string itemId = name.substr(5, separator - 5);
        const NextDayz::FItemDef* definition = NextDayz::FindItemDef(itemId);
        REQUIRE(definition);
        CHECK(definition->kind == NextDayz::EItemKind::Clothing);
        const std::string target = name.substr(separator + 2);
        CHECK(body.FindBone(target == "harness" ? "bone_torso" : "bone_" + target) >= 0);
        CHECK(glm::length(bone.bindT) == Catch::Approx(0.0f));
        CHECK(glm::length(bone.bindS - glm::vec3(1.0f)) == Catch::Approx(0.0f));
        CHECK(std::abs(bone.bindR.w) == Catch::Approx(1.0f));
        REQUIRE(part.sectionColors.size() <= 16);
        CHECK(std::none_of(part.sectionTintable.begin(), part.sectionTintable.end(), [](bool tint) { return tint; }));
        ++partCounts[itemId];
        const auto& vertices = wardrobe.partModels[part.modelIndex].CPUVertices();
        REQUIRE_FALSE(vertices.empty());
        glm::vec3 lo(std::numeric_limits<float>::max());
        glm::vec3 hi(std::numeric_limits<float>::lowest());
        for (const auto& vertex : vertices)
        {
            lo = glm::min(lo, vertex.Position);
            hi = glm::max(hi, vertex.Position);
        }
        boundsMin[name] = lo;
        boundsMax[name] = hi;
        if (target.starts_with("foot_")) CHECK(lo.y >= -0.0001f);
    }
    for (const NextDayz::FItemDef& definition : NextDayz::AllItemDefs())
    {
        if (definition.kind != NextDayz::EItemKind::Clothing) continue;
        INFO(definition.id);
        const size_t expected = definition.equipSlot == NextDayz::EEquipSlot::Torso ? 5 :
                                definition.equipSlot == NextDayz::EEquipSlot::Legs ? 7 :
                                definition.equipSlot == NextDayz::EEquipSlot::Back ? 2 : 1;
        CHECK(partCounts[std::string(definition.id)] == expected);
    }
    // Coat, carrier and pack occupy successively deeper silhouettes. Packs
    // must differ in size as well as colour. Coordinates here are engine Y-up.
    CHECK(boundsMin["bone_vest_military__torso"].z < boundsMin["bone_jacket__torso"].z);
    CHECK(boundsMax["bone_vest_military__torso"].z > boundsMax["bone_hoodie__torso"].z);
    CHECK(boundsMax["bone_backpack_small__torso"].z < -0.20f);
    CHECK(boundsMin["bone_backpack__torso"].z < boundsMin["bone_backpack_small__torso"].z);
    CHECK(boundsMin["bone_backpack_military__torso"].z < boundsMin["bone_backpack__torso"].z);
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

TEST_CASE("NextDayz melee clips keep grips attached through wind-up contact and recovery", "[Unit][ScadRig][NextDayz][Melee]")
{
    FRigAsset asset;
    std::string error;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/nextdayz_survivor.scad", {}, asset, error, &warnings));
    REQUIRE(warnings.empty());
    const int32_t socket = asset.FindBone("bone_weapon_socket");
    const int32_t weapon = asset.FindBone("bone_ik_weapon");
    const int32_t handL = asset.FindBone("bone_hand_l");
    const int32_t targetL = asset.FindBone("bone_ik_hand_l");
    std::vector<glm::mat4> transforms;
    for (const char* id : {"knife", "crowbar", "axe"})
    {
        INFO(id);
        const NextDayz::FWeaponDef* definition = NextDayz::FindWeaponDef(id);
        REQUIRE(definition);
        const float contact = definition->meleeStrikeTime01;
        const std::string prefix = std::string("melee_") + id;
        const FRigClip* guard = asset.FindClip(prefix + "_ready");
        const FRigClip* low = asset.FindClip(prefix + "_low");
        const FRigClip* attack = asset.FindClip(prefix + "_attack");
        REQUIRE(guard);
        REQUIRE(low);
        REQUIRE(attack);
        CHECK(guard->loop);
        CHECK(low->loop);
        CHECK_FALSE(attack->loop);
        CHECK(attack->duration == Catch::Approx(1.0f));
        for (const FRigClip* hold : {low, guard})
        {
            INFO(hold->name);
            const auto pose = SampleSurvivorClip(asset, *hold, 0.0f);
            NextGameplay::ComputeRigComponentTransforms(asset, pose, transforms);
            CHECK(glm::length(NextGameplay::RigBonePosition(transforms, socket) -
                              NextGameplay::RigBonePosition(transforms, weapon)) < 0.025f);
            CHECK(glm::length(NextGameplay::RigBonePosition(transforms, handL) -
                              NextGameplay::RigBonePosition(transforms, targetL)) < 0.04f);
            if (definition->id == "axe")
            {
                // The heavy head stays up, and both palms wrap the haft with
                // enough separation to control it instead of cupping a gun grip.
                const glm::mat4 toWeapon = glm::inverse(transforms[weapon]);
                const int32_t handR = asset.FindBone("bone_hand_r");
                const glm::vec4 palm(0.0f, -0.06f, 0.01f, 1.0f);
                const glm::vec3 palmL(toWeapon * transforms[handL] * palm);
                const glm::vec3 palmR(toWeapon * transforms[handR] * palm);
                const glm::vec3 haft = glm::normalize(glm::vec3(transforms[weapon][2]));
                CHECK(haft.y > 0.65f);
                CHECK(glm::length(glm::vec2(palmL.x, palmL.y)) < 0.025f);
                CHECK(palmL.z > 0.15f);
                CHECK(palmL.z < 0.35f);
                CHECK(glm::length(palmL - palmR) > 0.22f);
            }
        }
        glm::vec3 start{};
        for (float time : {0.0f, contact * 0.6f, contact, contact + 0.16f, 1.0f})
        {
            INFO(time);
            const auto pose = SampleSurvivorClip(asset, *attack, time);
            NextGameplay::ComputeRigComponentTransforms(asset, pose, transforms);
            const glm::vec3 position = NextGameplay::RigBonePosition(transforms, weapon);
            CHECK(glm::length(NextGameplay::RigBonePosition(transforms, socket) - position) < 0.025f);
            CHECK(glm::length(NextGameplay::RigBonePosition(transforms, handL) -
                              NextGameplay::RigBonePosition(transforms, targetL)) < 0.04f);
            if (time == 0.0f) start = position;
            if (time == contact) CHECK(glm::length(position - start) > 0.15f);
            if (time == 1.0f) CHECK(glm::length(position - start) < 0.01f);
        }
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
    CHECK(warnings.empty());
    for (const FItemDef& definition : AllItemDefs())
    {
        INFO(definition.id);
        const int32_t bone = asset.FindBone("bone_item_" + std::string(definition.id));
        REQUIRE(bone >= 0);
        REQUIRE(std::any_of(asset.parts.begin(), asset.parts.end(),
                            [bone](const FRigPart& part) { return part.bone == bone; }));
        if (definition.kind == EItemKind::Clothing)
        {
            // LootSystem injects a single model per item, and places its origin
            // on the ground. A missing/split/sunken variant is not usable loot.
            CHECK(std::count_if(asset.parts.begin(), asset.parts.end(),
                               [bone](const FRigPart& part) { return part.bone == bone; }) == 1);
            for (const FRigPart& part : asset.parts)
            {
                if (part.bone != bone) continue;
                CHECK(part.sectionColors.size() <= 16);
                float lowest = std::numeric_limits<float>::max();
                for (const Vertex& vertex : asset.partModels[part.modelIndex].CPUVertices())
                {
                    lowest = std::min(lowest, vertex.Position.y);
                }
                CHECK(lowest >= -0.002f);
                CHECK(lowest < 0.02f);
            }
        }
    }
}

TEST_CASE("NextDayz weapon catalog has a PBR held model for every weapon", "[Unit][ScadRig][NextDayz]")
{
    using namespace NextDayz;
    FRigAsset asset;
    std::string error;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/nextdayz_weapons.scad", {}, asset, error, &warnings));
    CHECK(warnings.empty());
    for (const FWeaponDef& weapon : kWeapons)
    {
        INFO(weapon.id);
        const int32_t bone = asset.FindBone("bone_wpn_" + std::string(weapon.id));
        REQUIRE(bone >= 0);
        // The game injects one model per weapon: everything must fit one part (<= 16 sections).
        const auto parts = std::count_if(asset.parts.begin(), asset.parts.end(),
                                         [bone](const FRigPart& part) { return part.bone == bone; });
        REQUIRE(parts == 1);
        const FRigPart& part = *std::find_if(asset.parts.begin(), asset.parts.end(),
                                             [bone](const FRigPart& p) { return p.bone == bone; });
        CHECK(std::any_of(part.sectionSurfaces.begin(), part.sectionSurfaces.end(),
                          [](const glm::vec2& surface) { return surface.y > 0.5f; }));

        // Weapon frame: +Z is the muzzle; the muzzle landmark sits on the model's tip.
        float tip = -1.0f;
        for (const Vertex& vertex : asset.partModels[part.modelIndex].CPUVertices())
        {
            tip = std::max(tip, vertex.Position.z);
        }
        CHECK(tip == Catch::Approx(WeaponModelPoints(weapon.id).muzzleForward).margin(0.012f));
    }
}

TEST_CASE("NextDayz attachment catalog provides every mount as a single PBR model", "[Unit][ScadRig][NextDayz][Attachments]")
{
    using namespace NextDayz;
    FRigAsset asset;
    std::string error;
    std::vector<std::string> warnings;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/nextdayz_attachments.scad", {}, asset, error, &warnings));
    CHECK(warnings.empty());
    for (const auto& partDef : kAttachments)
    {
        INFO(partDef.id);
        const int bone = asset.FindBone("bone_att_" + std::string(partDef.id));
        REQUIRE(bone >= 0);
        REQUIRE(std::count_if(asset.parts.begin(), asset.parts.end(), [bone](const auto& part) { return part.bone == bone; }) == 1);
        const auto& part = *std::find_if(asset.parts.begin(), asset.parts.end(), [bone](const auto& entry) { return entry.bone == bone; });
        CHECK(part.sectionColors.size() <= 16);
        CHECK_FALSE(asset.partModels[part.modelIndex].CPUVertices().empty());
        CHECK(std::any_of(part.sectionSurfaces.begin(), part.sectionSurfaces.end(), [](const auto& surface) { return surface.x > 0.3f; }));
    }
    REQUIRE(asset.FindBone("bone_att_svd_scope") >= 0);
}

// ---------------------------------------------------------------------------
// gk_texture(): procedural prints (Engine/Assets/Loaders/ProceduralTexture)
// ---------------------------------------------------------------------------

TEST_CASE("Procedural prints bake deterministically and wrap without a seam", "[Unit][ProceduralTexture]")
{
    constexpr uint32_t size = 128;
    for (std::string_view pattern : TexturePrintNames())
    {
        INFO(pattern);
        FProceduralTexture texture = PrintNamed(pattern);
        texture.seed = 3;
        const std::vector<uint8_t> a = ProceduralTexture::Bake(texture, size);
        const std::vector<uint8_t> b = ProceduralTexture::Bake(texture, size);
        REQUIRE(a.size() == size * size * 4);
        CHECK(a == b);
        texture.seed = 4;
        CHECK(ProceduralTexture::Bake(texture, size) != a);

        // The step across the tile edge (last column -> first, last row -> first)
        // looks like any interior step: no visible seam when the print repeats.
        const auto luma = [&](uint32_t x, uint32_t y)
        {
            const uint8_t* p = &a[(static_cast<size_t>(y) * size + x) * 4];
            return 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2];
        };
        double interior = 0.0;
        double wrap = 0.0;
        for (uint32_t y = 0; y < size; ++y)
        {
            // Both directions, like the wrap: grain-like prints differ far more across than along.
            for (uint32_t x = 0; x + 1 < size; ++x)
                interior += std::abs(luma(x + 1, y) - luma(x, y)) + std::abs(luma(y, x + 1) - luma(y, x));
            wrap += std::abs(luma(0, y) - luma(size - 1, y));
            wrap += std::abs(luma(y, 0) - luma(y, size - 1));
        }
        interior /= 2.0 * static_cast<double>(size) * (size - 1);
        wrap /= 2.0 * size;
        CHECK(wrap < interior * 2.5 + 2.0);
    }
}

TEST_CASE("Ground and building prints stay a low-contrast modulation", "[Unit][ProceduralTexture]")
{
    // These prints multiply the flat colour of terrain biomes and building
    // parts. Bright on average and with a narrow spread they add grain, while
    // the facet shading keeps carrying the low-poly form.
    constexpr uint32_t size = 128;
    for (const char* pattern : {"meadow", "dirt", "gravel", "asphalt", "rock", "plaster", "brick", "tiles",
                                "planks", "stone", "concrete"})
    {
        INFO(pattern);
        FProceduralTexture texture = PrintNamed(pattern);
        const std::vector<uint8_t> pixels = ProceduralTexture::Bake(texture, size);
        double sum = 0.0, sumSq = 0.0;
        for (size_t i = 0; i < pixels.size(); i += 4)
        {
            const double luma = 0.3 * pixels[i] + 0.59 * pixels[i + 1] + 0.11 * pixels[i + 2];
            sum += luma;
            sumSq += luma * luma;
        }
        const double count = static_cast<double>(size) * size;
        const double mean = sum / count;
        const double spread = std::sqrt(std::max(0.0, sumSq / count - mean * mean));
        CAPTURE(mean, spread);
        CHECK(mean > 160.0);
        CHECK(spread < 40.0);
        CHECK(spread > 3.0); // and it is not blank
    }
}

TEST_CASE("Procedural prints bake a gentle seamless relief and their surface factors", "[Unit][ProceduralTexture]")
{
    // At the size the engine bakes: thread-scale reliefs need a few texels per thread.
    constexpr uint32_t size = ProceduralTexture::kDefaultSize;
    const auto decode = [](const uint8_t* p)
    {
        return glm::vec3(p[0], p[1], p[2]) / 255.0f * 2.0f - 1.0f;
    };
    const auto meanTilt = [&](const std::vector<uint8_t>& normal)
    {
        double sum = 0.0;
        for (size_t i = 0; i < normal.size(); i += 4)
        {
            const glm::vec3 n = decode(&normal[i]);
            sum += glm::degrees(std::acos(std::clamp(static_cast<double>(n.z) / glm::length(n), -1.0, 1.0)));
        }
        return sum / static_cast<double>(normal.size() / 4);
    };
    for (std::string_view pattern : TexturePrintNames())
    {
        INFO(pattern);
        FProceduralTexture texture = PrintNamed(pattern);
        texture.seed = 3;
        const FProceduralMaps maps = ProceduralTexture::BakeMaps(texture, size);
        CHECK(maps.albedo == ProceduralTexture::Bake(texture, size));
        REQUIRE(maps.hasNormal);
        REQUIRE(maps.normal.size() == size * size * 4);

        // Unit normals facing out of the surface, never tipped past 55 degrees.
        double wrap = 0.0;
        double interior = 0.0;
        size_t badNormals = 0;
        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t x = 0; x < size; ++x)
            {
                const glm::vec3 n = decode(&maps.normal[(static_cast<size_t>(y) * size + x) * 4]);
                badNormals += std::abs(glm::length(n) - 1.0f) > 0.02f || n.z <= 0.55f ? 1 : 0;
                if (x + 1 < size)
                    interior += glm::length(decode(&maps.normal[(static_cast<size_t>(y) * size + x + 1) * 4]) - n);
            }
            wrap += glm::length(decode(&maps.normal[(static_cast<size_t>(y) * size) * 4]) -
                                decode(&maps.normal[(static_cast<size_t>(y) * size + size - 1) * 4]));
            wrap += glm::length(decode(&maps.normal[static_cast<size_t>(y) * 4]) -
                                decode(&maps.normal[(static_cast<size_t>(size - 1) * size + y) * 4]));
        }
        CHECK(badNormals == 0);
        // The relief repeats without a seam, like the colour.
        CHECK(wrap / (2.0 * size) < interior / (static_cast<double>(size) * (size - 1)) * 2.5 + 0.02);
        // Grain inside a facet, not a new shape: a few degrees on average.
        const double tilt = meanTilt(maps.normal);
        CAPTURE(tilt);
        CHECK(tilt > 1.0);
        CHECK(tilt < 22.0);

        // relief scales the slopes; 0 switches the normal map off.
        texture.relief = 2.0f;
        CHECK(meanTilt(ProceduralTexture::BakeMaps(texture, size).normal) > tilt * 1.15); // steep bevels saturate at the 40 degree cap
        texture.relief = 0.0f;
        const FProceduralMaps flat = ProceduralTexture::BakeMaps(texture, size);
        CHECK_FALSE(flat.hasNormal);
        CHECK(flat.normal.empty());
        CHECK(flat.albedo == maps.albedo);
        FProceduralTexture defaultRelief = PrintNamed(texture.pattern);
        defaultRelief.seed = 3;
        CHECK(texture.Hash() != defaultRelief.Hash());
    }

    // Uniform surfaces need no MRA map; worn ones vary in roughness and metal.
    const auto factors = [&](const char* pattern)
    {
        FProceduralTexture texture = PrintNamed(pattern);
        return ProceduralTexture::BakeMaps(texture, size);
    };
    for (const char* pattern : {"woodland", "canvas", "meadow", "plaster", "brick", "concrete"})
    {
        INFO(pattern);
        CHECK_FALSE(factors(pattern).hasMra);
    }
    for (const char* pattern : {"grain", "gravel", "tiles", "stone", "woodgrain", "worn"})
    {
        INFO(pattern);
        const FProceduralMaps maps = factors(pattern);
        REQUIRE(maps.hasMra);
        uint8_t lowestRough = 255;
        for (size_t i = 0; i < maps.mra.size(); i += 4) lowestRough = std::min(lowestRough, maps.mra[i + 1]);
        CHECK(lowestRough < 230);
    }
    // Chipped paint and rust are dielectric over bare metal: the metalness
    // factor splits into both ends, and the bare metal is the glossier part.
    for (const char* pattern : {"chipped", "rust"})
    {
        INFO(pattern);
        const FProceduralMaps maps = factors(pattern);
        REQUIRE(maps.hasMra);
        size_t metal = 0, dielectric = 0;
        double metalRough = 0.0, dielectricRough = 0.0;
        for (size_t i = 0; i < maps.mra.size(); i += 4)
        {
            if (maps.mra[i + 2] > 240) { ++metal; metalRough += maps.mra[i + 1]; }
            if (maps.mra[i + 2] < 10) { ++dielectric; dielectricRough += maps.mra[i + 1]; }
        }
        const double total = static_cast<double>(size) * size;
        CHECK(metal / total > 0.05);
        CHECK(dielectric / total > 0.05);
        CHECK(metalRough / std::max<size_t>(metal, 1) < dielectricRough / std::max<size_t>(dielectric, 1));
    }
}

TEST_CASE("ScadRig box-projected tangents follow the print's UVs on every face", "[Unit][ScadRig][ProceduralTexture]")
{
    // A tilted, spun box exercises all six projection planes and both
    // handednesses; the tangent frame must agree with the UVs or the relief
    // would light from the wrong side.
    FRigAsset asset;
    REQUIRE(BuildRigFromSource(
        "module bone_root() {\n"
        "    gk_texture(\"brick\", scale = 0.2)\n"
        "        rotate([17, -23, 38]) cube(0.5, center = true);\n"
        "    gk_texture(\"tiles\", scale = 0.3)\n"
        "        translate([1, 0, 0]) rotate([0, 0, 90]) sphere(0.3, $fn = 12);\n"
        "}\n"
        "bone_root();\n",
        asset));
    REQUIRE(asset.parts.size() == 1);
    const Model& model = asset.partModels[asset.parts[0].modelIndex];
    const auto& vertices = model.CPUVertices();
    const auto& indices = model.CPUIndices();
    REQUIRE(indices.size() >= 36);
    size_t checked = 0;
    size_t wrong = 0;
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const Vertex& a = vertices[indices[i]];
        const Vertex& b = vertices[indices[i + 1]];
        const Vertex& c = vertices[indices[i + 2]];
        const glm::vec3 e1 = b.Position - a.Position, e2 = c.Position - a.Position;
        const glm::vec2 t1 = b.TexCoord - a.TexCoord, t2 = c.TexCoord - a.TexCoord;
        const float det = t1.x * t2.y - t1.y * t2.x;
        if (std::abs(det) < 1e-6f) continue;
        const glm::vec3 dPdu = glm::normalize((e1 * t2.y - e2 * t1.y) / det);
        const glm::vec3 dPdv = glm::normalize((e2 * t1.x - e1 * t2.x) / det);
        for (const Vertex* v : {&a, &b, &c})
        {
            const glm::vec3 n = glm::normalize(v->Normal);
            const glm::vec3 t(v->Tangent);
            const bool orthonormal = std::abs(glm::dot(t, n)) < 1e-3f && std::abs(glm::length(t) - 1.0f) < 1e-3f;
            const bool alongU = glm::dot(t, dPdu) > 0.5f;
            const bool handed = glm::dot(v->Tangent.w * glm::cross(n, t), dPdv) > 0.5f;
            wrong += orthonormal && alongU && handed ? 0 : 1;
        }
        ++checked;
    }
    CHECK(wrong == 0);
    CHECK(checked * 3 >= indices.size() * 9 / 10);
}

namespace
{
    FTexExpr TxNum(double v)
    {
        FTexExpr e;
        e.kind = FTexExpr::EKind::Number;
        e.number = v;
        return e;
    }
    FTexExpr TxList(std::string op, std::vector<FTexExpr> args)
    {
        FTexExpr e;
        e.kind = FTexExpr::EKind::List;
        FTexExpr head;
        head.kind = FTexExpr::EKind::String;
        head.text = std::move(op);
        e.items.push_back(std::move(head));
        for (FTexExpr& a : args) e.items.push_back(std::move(a));
        return e;
    }
    FTexExpr TxColor(float r, float g, float b)
    {
        FTexExpr e;
        e.kind = FTexExpr::EKind::List;
        e.items = {TxNum(r), TxNum(g), TxNum(b)};
        return e;
    }
    FTexExpr TxSurface(FTexExpr color, FTexExpr height, FTexExpr rough = TxNum(1), FTexExpr metal = TxNum(1))
    {
        return TxList("surface", {std::move(color), std::move(height), std::move(rough), std::move(metal), TxNum(10), TxNum(1)});
    }

    std::string ReadAssetText(const char* relative)
    {
        std::ifstream file(Utilities::FileHelper::GetPlatformFilePath(relative), std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }
}

TEST_CASE("Texture graphs compile, validate their nodes and share equal subtrees", "[Unit][ProceduralTexture][TextureGraph]")
{
    std::string error;
    // The same fbm used twice compiles to one node.
    const FTexExpr fbm = TxList("fbm", {TxNum(4), TxNum(4), TxNum(3), TxNum(1)});
    const auto graph = FTextureGraph::Compile(
        TxSurface(TxList("mul", {fbm, TxColor(1, 0.5f, 0.2f)}), TxList("band", {fbm, TxNum(0.5), TxNum(0.1)})), error);
    REQUIRE(graph);
    CHECK(error.empty());
    // fbm, colour, mul, band, the shared constant 1 and surface.
    CHECK(graph->NodeCount() == 6);
    CHECK(graph->Canonical().find("(fbm 4 4 3 1)") != std::string::npos);

    // Equal graphs share a canonical text (the content key).
    const auto again = FTextureGraph::Compile(
        TxSurface(TxList("mul", {fbm, TxColor(1, 0.5f, 0.2f)}), TxList("band", {fbm, TxNum(0.5), TxNum(0.1)})), error);
    REQUIRE(again);
    CHECK(again->Canonical() == graph->Canonical());

    const auto fails = [&](const FTexExpr& root, const char* needle)
    {
        std::string message;
        const auto bad = FTextureGraph::Compile(root, message);
        INFO(message);
        CHECK_FALSE(bad);
        CHECK(message.find(needle) != std::string::npos);
    };
    fails(TxSurface(TxList("plaid", {}), TxNum(0)), "unknown node 'plaid'");
    fails(TxSurface(TxList("fbm", {TxNum(4)}), TxNum(0)), "'fbm' takes");
    fails(TxSurface(TxList("fbm", {TxNum(0), TxNum(4), TxNum(3), TxNum(0)}), TxNum(0)), "cellsU must be 1..256");
    fails(TxSurface(TxList("cells", {TxNum(8), TxNum(0), TxList("x", {})}), TxNum(0)), "has no mode");
    fails(TxList("fbm", {TxNum(4), TxNum(4), TxNum(3), TxNum(0)}), "root must be");
    fails(TxSurface(TxSurface(TxNum(1), TxNum(0)), TxNum(0)), "only be the root");
    fails(TxSurface(TxList("top", {fbm, TxNum(1.5)}), TxNum(0)), "fraction must be 0..1");
}

TEST_CASE("Texture graphs bake seamless, seeded, recolourable prints with exact coverage", "[Unit][ProceduralTexture][TextureGraph]")
{
    constexpr uint32_t size = ProceduralTexture::kDefaultSize;
    std::string error;
    // 30 % red blotches over blue, a blotch relief, glossy metal in the blotches.
    const FTexExpr blotch = TxList("top", {TxList("fbm", {TxNum(5), TxNum(5), TxNum(4), TxNum(2)}), TxNum(0.3)});
    FProceduralTexture texture;
    texture.pattern = "graph";
    texture.graph = FTextureGraph::Compile(
        TxSurface(TxList("select", {blotch, TxList("palette", {TxNum(0), TxColor(1, 0, 0)}), TxColor(0, 0, 1)}),
                  blotch, TxList("select", {blotch, TxNum(0.4), TxNum(1)}), TxList("select", {blotch, TxNum(1), TxNum(0)})),
        error);
    INFO(error);
    REQUIRE(texture.graph);

    const FProceduralMaps maps = ProceduralTexture::BakeMaps(texture, size);
    CHECK(maps.albedo == ProceduralTexture::BakeMaps(texture, size).albedo);
    CHECK(maps.hasNormal);
    REQUIRE(maps.hasMra);
    size_t red = 0;
    size_t metalRed = 0;
    for (size_t i = 0; i < maps.albedo.size(); i += 4)
    {
        if (maps.albedo[i] > 200 && maps.albedo[i + 2] < 50)
        {
            ++red;
            metalRed += maps.mra[i + 2] > 240 && maps.mra[i + 1] < 110 ? 1 : 0;
        }
    }
    const double total = static_cast<double>(size) * size;
    CHECK(red / total == Catch::Approx(0.3).margin(0.005)); // coverage by rank, not by luck
    CHECK(metalRed == red);

    // Seamless: the step across the tile edge looks like any interior step.
    const auto luma = [&](uint32_t x, uint32_t y)
    {
        return static_cast<double>(maps.albedo[(static_cast<size_t>(y) * size + x) * 4]);
    };
    double interior = 0.0;
    double wrap = 0.0;
    for (uint32_t y = 0; y < size; ++y)
    {
        for (uint32_t x = 0; x + 1 < size; ++x)
            interior += std::abs(luma(x + 1, y) - luma(x, y)) + std::abs(luma(y, x + 1) - luma(y, x));
        wrap += std::abs(luma(0, y) - luma(size - 1, y)) + std::abs(luma(y, 0) - luma(y, size - 1));
    }
    CHECK(wrap / (2.0 * size) < interior / (2.0 * size * (size - 1)) * 2.5 + 2.0);

    // The palette node takes the author's colour; the seed re-rolls the noise.
    FProceduralTexture green = texture;
    green.palette = {{0, 1, 0}};
    const std::vector<uint8_t> recoloured = ProceduralTexture::Bake(green, size);
    size_t greenCount = 0;
    for (size_t i = 0; i < recoloured.size(); i += 4) greenCount += recoloured[i + 1] > 200 && recoloured[i] < 50 ? 1 : 0;
    CHECK(greenCount == red);
    CHECK(green.Hash() != texture.Hash());
    FProceduralTexture reseeded = texture;
    reseeded.seed = 9;
    CHECK(ProceduralTexture::Bake(reseeded, size) != maps.albedo);
}

TEST_CASE("gk_texgraph.scad prints load through gk_texture without engine changes", "[Unit][ScadRig][ProceduralTexture][TextureGraph]")
{
    const std::string library = ReadAssetText("assets/scad/lib/gk_texgraph.scad");
    REQUIRE(library.find("function tx_surface") != std::string::npos);
    const std::vector<std::string> recipes = {"tex_corrugated()", "tex_bark()", "tex_tigerstripe()", "tex_splinter()",
                                              "tex_flagstone()", "tex_brickwall()"};
    std::string source = library + "\nmodule bone_root() {\n";
    for (size_t i = 0; i < recipes.size(); ++i)
    {
        source += fmt::format("    gk_material([{0}, 1, 1], roughness = 0.8, metalness = 0.9) gk_texture({1}, scale = 0.5)"
                              " translate([{2}, 0, 0]) cube(0.3, center = true);\n",
                              0.5 + 0.05 * static_cast<double>(i), recipes[i], i);
    }
    // A broken graph warns and leaves its geometry unprinted.
    source += "    gk_texture(tx_surface(tx_fbm(0), 0)) translate([0, 2, 0]) cube(0.3, center = true);\n}\nbone_root();\n";

    const SceneEvalResult evaluated = EvalRigScene(source);
    CHECK(AnyWarningContains(evaluated.warnings, "cellsU must be 1..256"));
    CHECK_FALSE(AnyWarningContains(evaluated.warnings, "unknown function"));
    FRigAsset asset;
    std::string rigError;
    REQUIRE(FScadRigLoader::BuildRig(evaluated, {}, asset, rigError));
    size_t graphs = 0;
    for (const FRigPart& part : asset.parts)
    {
        for (const auto& texture : part.sectionTextures)
        {
            if (!texture || !texture->graph) continue;
            ++graphs;
            CHECK(texture->pattern == "graph");
            const FProceduralMaps maps = ProceduralTexture::BakeMaps(*texture, ProceduralTexture::kDefaultSize);
            CHECK(maps.hasNormal);
        }
    }
    CHECK(graphs == recipes.size());
}

TEST_CASE("Camouflage prints use their palette and keep the historical layer mix", "[Unit][ProceduralTexture]")
{
    constexpr uint32_t size = 128;
    // A two-colour palette makes every pixel nameable despite the light weave relief.
    FProceduralTexture woodland = PrintNamed("woodland");
    woodland.palette = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
    const std::vector<uint8_t> pixels = ProceduralTexture::Bake(woodland, size);
    std::array<size_t, 4> counts{};
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        const int r = pixels[i], g = pixels[i + 1], b = pixels[i + 2];
        if (r < 40 && g < 40 && b < 40) ++counts[3];
        else if (r >= g && r >= b) ++counts[0];
        else if (g >= b) ++counts[1];
        else ++counts[2];
    }
    const double total = static_cast<double>(size) * size;
    // Ground, green and brown each a large share; black branches the smallest.
    CHECK(counts[0] / total > 0.12);
    CHECK(counts[1] / total > 0.20);
    CHECK(counts[2] / total > 0.20);
    CHECK(counts[3] / total > 0.04);
    CHECK(counts[3] / total < 0.16);

    // DBDU: the black and white "chocolate chip" rocks are there.
    FProceduralTexture desert = PrintNamed("desert");
    const std::vector<uint8_t> chips = ProceduralTexture::Bake(desert, 256);
    size_t black = 0;
    size_t white = 0;
    for (size_t i = 0; i < chips.size(); i += 4)
    {
        if (chips[i] < 80 && chips[i + 1] < 80) ++black; // linear 0.05 is ~63 in sRGB
        if (chips[i] > 215 && chips[i + 1] > 215 && chips[i + 2] > 205) ++white;
    }
    CHECK(black > 200);
    CHECK(white > 120);

    // The content key follows the bake, not the tiling scale.
    FProceduralTexture scaled = desert;
    scaled.scale = 2.0f;
    CHECK(scaled.Hash() == desert.Hash());
    scaled.palette = {{0.1f, 0.1f, 0.1f}};
    CHECK(scaled.Hash() != desert.Hash());
}

TEST_CASE("ScadRig gk_texture gives its own textured section with box-projected UVs", "[Unit][ScadRig][ProceduralTexture]")
{
    FRigAsset asset;
    std::vector<std::string> warnings;
    REQUIRE(BuildRigFromSource(
        "module bone_root() {\n"
        "    gk_texture(\"woodland\", scale = 0.25, seed = 2)\n"
        "        gk_material([1, 1, 1], roughness = 1) cube(0.5, center = true);\n"
        "    gk_material([1, 1, 1], roughness = 0.6) gk_texture(\"grain\", scale = 0.1)\n"
        "        translate([1, 0, 0]) cube(0.2, center = true);\n"
        "    color([1, 1, 1]) translate([0, 0, 1]) cube(0.2, center = true);\n"
        "    color([1, 1, 1]) gk_texture(\"plaid\") translate([0, 1, 0]) cube(0.1, center = true);\n"
        "}\n"
        "bone_root();\n",
        asset, &warnings));
    REQUIRE(asset.parts.size() == 1);
    const FRigPart& part = asset.parts[0];
    // Same white color three ways: woodland print, leather grain, plain (+ the
    // unknown pattern, which falls back to plain geometry and merges with it).
    REQUIRE(part.sectionColors.size() == 3);
    REQUIRE(part.sectionTextures.size() == 3);
    int woodland = 0;
    int grain = 0;
    int plain = 0;
    uint32_t woodlandSection = 0;
    for (size_t section = 0; section < part.sectionTextures.size(); ++section)
    {
        const auto& texture = part.sectionTextures[section];
        if (!texture)
        {
            ++plain;
            continue;
        }
        if (texture->pattern == "woodland")
        {
            ++woodland;
            woodlandSection = static_cast<uint32_t>(section);
            CHECK(texture->scale == Catch::Approx(0.25f));
            CHECK(texture->seed == 2u);
            // A print under gk_material keeps the diffuse (Lambertian) surface.
            CHECK(part.SectionSurface(section).x == Catch::Approx(1.0f));
        }
        if (texture->pattern == "grain")
        {
            ++grain;
            // gk_texture inside gk_material keeps the leather's roughness.
            CHECK(part.SectionSurface(section).x == Catch::Approx(0.6f));
        }
    }
    CHECK(woodland == 1);
    CHECK(grain == 1);
    CHECK(plain == 1);

    // Textured triangles carry tile-space UVs (the 0.5 m cube spans two 0.25 m tiles).
    float maxUv = 0.0f;
    for (const Vertex& vertex : asset.partModels[part.modelIndex].CPUVertices())
    {
        if (vertex.MaterialIndex != woodlandSection) continue;
        maxUv = std::max({maxUv, std::abs(vertex.TexCoord.x), std::abs(vertex.TexCoord.y)});
    }
    CHECK(maxUv == Catch::Approx(1.0f).margin(1e-3f));

    // Without a GPU texture pool the material stays untextured but keeps its surface.
    std::vector<FMaterial> materials;
    for (size_t section = 0; section < part.sectionColors.size(); ++section)
    {
        const Material& material = materials[SceneBuilder::AddRigSectionMaterial(materials, part, section)].gpuMaterial_;
        CHECK(material.DiffuseTextureId == -1);
    }
}

TEST_CASE("NextDayz heritage fabrics are diffuse and the camouflage is printed, not modelled", "[Unit][ScadRig][NextDayz][Clothing]")
{
    using namespace NextDayz;
    FRigAsset wardrobe;
    std::string error;
    REQUIRE(FScadRigLoader::LoadRig("assets/scad/characters/nextdayz_clothing.scad", {}, wardrobe, error));
    const std::unordered_map<std::string, std::string> expectedPrint = {
        {"jacket_m81", "woodland"}, {"pants_m81", "woodland"}, {"helmet_pasgt", "woodland"},
        {"jacket_emr", "digital"}, {"pants_emr", "digital"}, {"hat_boonie_emr", "digital"},
        {"jacket_dbdu", "desert"}, {"pants_dbdu", "desert"},
        {"jacket_flora", "flora"}, {"pants_flora", "flora"},
        {"jacket_strichtarn", "rain"}, {"pants_strichtarn", "rain"}};
    std::unordered_map<std::string, bool> found;
    for (const FRigPart& part : wardrobe.parts)
    {
        const std::string& name = wardrobe.bones[part.bone].name;
        const std::string itemId = name.substr(5, name.find("__") - 5);
        INFO(name);
        for (size_t section = 0; section < part.sectionTextures.size(); ++section)
        {
            const auto& texture = part.sectionTextures[section];
            if (!texture) continue;
            // Woven and printed cloth is Lambertian: a specular lobe on fabric reads as plastic.
            // (Leather grain and the worn-metal prints on helmets are specular on purpose.)
            static const std::unordered_set<std::string> cloth = {"woodland", "digital", "desert", "flora",
                                                                  "rain",     "canvas",  "twill"};
            if (cloth.contains(texture->pattern))
            {
                CHECK(part.SectionSurface(section).x == Catch::Approx(1.0f));
                CHECK(part.SectionSurface(section).y == Catch::Approx(0.0f));
            }
            if (const auto it = expectedPrint.find(itemId); it != expectedPrint.end() && texture->pattern == it->second)
            {
                found[itemId] = true;
            }
        }
        // Strichtarn strokes used to be thousands of slivers; the print keeps the parts light.
        if (itemId == "jacket_strichtarn" && name.ends_with("__torso"))
        {
            CHECK(wardrobe.partModels[part.modelIndex].CPUVertices().size() < 6000);
        }
    }
    for (const auto& [item, pattern] : expectedPrint)
    {
        INFO(item << " expects " << pattern);
        CHECK(found[item]);
    }
}
