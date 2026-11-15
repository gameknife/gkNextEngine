#include "Engine/Common/CoreMinimal.hpp"

#include <catch2/catch_all.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Modules/ScadLoader/FScadRig.h"

#include <string>
#include <vector>

// Asset contracts of the Brotato3D characters (Application/Game/Brotato3D):
//   * every zombie variant shares the walker's skeleton, because Brotato3D copies the
//     walker's clips onto the others by bone name (FZombieRigResource::Load);
//   * the bones FZombieRigVisual severs, and the clips it plays, exist;
//   * the hero, weapon and vehicle catalogs carry what the game looks up by name.

using namespace Assets;

namespace
{
    FRigAsset LoadBrotatoRig(const std::string& path)
    {
        FRigAsset asset;
        std::string err;
        std::vector<std::string> warnings;
        INFO(path);
        REQUIRE(FScadRigLoader::LoadRig(path, ScadRigLoadOptions{}, asset, err, &warnings));
        CHECK(warnings.empty());
        return asset;
    }

    const FRigChannel* FindChannel(const FRigAsset& asset, const FRigClip& clip, const char* bone)
    {
        const int32_t index = asset.FindBone(bone);
        for (const FRigChannel& channel : clip.channels)
        {
            if (channel.bone == index)
            {
                return &channel;
            }
        }
        return nullptr;
    }
}

TEST_CASE("Brotato3D zombie variants share the walker skeleton and its clips", "[Unit][ScadRig][Brotato3D]")
{
    const FRigAsset walker = LoadBrotatoRig("assets/scad/characters/brotato_zombie_walker.scad");
    for (const char* clip : {"idle", "walk", "run", "crawl", "alert", "attack_lunge", "attack_swipe", "stagger",
                             "death_back", "death_forward", "death_crumple"})
    {
        INFO(clip);
        CHECK(walker.FindClip(clip) != nullptr);
    }
    // The bones a limb is severed at (Brotato3DZombieVisual.cpp limbBoneNames).
    for (const char* bone : {"bone_head", "bone_upperarm_l", "bone_upperarm_r", "bone_forearm_l", "bone_forearm_r",
                             "bone_thigh_l", "bone_thigh_r", "bone_calf_l", "bone_calf_r", "bone_torso"})
    {
        INFO(bone);
        CHECK(walker.FindBone(bone) >= 0);
    }

    // Crawling is prone: the pelvis drops most of the way to the ground.
    const FRigClip* crawl = walker.FindClip("crawl");
    REQUIRE(crawl != nullptr);
    CHECK(crawl->loop);
    const FRigChannel* pelvis = FindChannel(walker, *crawl, "bone_pelvis");
    REQUIRE(pelvis != nullptr);
    REQUIRE(!pelvis->position.Keys.empty());
    CHECK(pelvis->position.Sample(0.0f).y < -0.6f);

    for (const char* variant : {"brute", "spitter", "runner", "linebacker", "bloater", "witch", "hurler", "warden"})
    {
        INFO(variant);
        const FRigAsset asset = LoadBrotatoRig(fmt::format("assets/scad/characters/brotato_zombie_{}.scad", variant));
        CHECK(asset.clips.empty());   // clips come from the walker at load time
        REQUIRE(asset.bones.size() == walker.bones.size());
        for (size_t index = 0; index < walker.bones.size(); ++index)
        {
            const FRigBone& expected = walker.bones[index];
            const int32_t found = asset.FindBone(expected.name);
            INFO(expected.name);
            REQUIRE(found >= 0);
            // Same pivots, or the adopted clips would bend the limbs at the wrong place.
            CHECK(glm::length(asset.bones[found].bindT - expected.bindT) < 1.0e-4f);
            CHECK(asset.bones[found].parent == expected.parent);
        }
        CHECK(!asset.parts.empty());
    }
}

TEST_CASE("Brotato3D hero, weapon and vehicle catalogs carry what the game looks up", "[Unit][ScadRig][Brotato3D]")
{
    const FRigAsset hero = LoadBrotatoRig("assets/scad/characters/brotato_survivor.scad");
    for (const char* clip : {"stand_idle", "stand_run_f", "stand_run_b", "stand_run_l", "stand_run_r", "weapon_ready",
                             "weapon_aim", "recoil_rifle", "dash"})
    {
        INFO(clip);
        CHECK(hero.FindClip(clip) != nullptr);
    }
    CHECK(hero.FindBone("bone_weapon_socket") >= 0);
    // The jacket takes the selected character's colour.
    bool tintable = false;
    for (const FRigPart& part : hero.parts)
    {
        for (const bool section : part.sectionTintable)
        {
            tintable = tintable || section;
        }
    }
    CHECK(tintable);

    // One model per weapons.json id.
    const FRigAsset weapons = LoadBrotatoRig("assets/scad/characters/brotato_weapons.scad");
    for (const char* id : {"smg", "shotgun", "sniper", "flamethrower", "rocket", "laser"})
    {
        INFO(id);
        const int32_t bone = weapons.FindBone(fmt::format("bone_wpn_{}", id));
        REQUIRE(bone >= 0);
        CHECK(std::any_of(weapons.parts.begin(), weapons.parts.end(),
                          [bone](const FRigPart& part) { return part.bone == bone; }));
    }

    // The bus faces engine +Z and is about 11 m long.
    const FRigAsset vehicles = LoadBrotatoRig("assets/scad/characters/brotato_vehicles.scad");
    const int32_t bus = vehicles.FindBone("bone_veh_bus");
    REQUIRE(bus >= 0);
    const auto part = std::find_if(vehicles.parts.begin(), vehicles.parts.end(),
                                   [bus](const FRigPart& candidate) { return candidate.bone == bus; });
    REQUIRE(part != vehicles.parts.end());
    const Model& model = vehicles.partModels[part->modelIndex];
    const glm::vec3 size = model.GetLocalAABBMax() - model.GetLocalAABBMin();
    CHECK(size.z > 10.0f);
    CHECK(size.z < 12.5f);
    CHECK(size.x < 3.5f);
}
