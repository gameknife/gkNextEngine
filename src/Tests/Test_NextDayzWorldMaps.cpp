#include "Engine/Common/CoreMinimal.hpp"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "Application/Game/NextDayz/World/Campaign.hpp"
#include "Application/Game/NextDayz/World/ExpeditionMaps.hpp"
#include "Application/Game/NextDayz/Data/ItemDefs.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/ScadLoader/FScadLoader.h"

TEST_CASE("NextDayz retired Riverland progress returns to the world safehouse without losing equipment", "[NextDayz][Startup]")
{
    using namespace NextDayz;
    FExpeditionMap map;
    map.id = "cz_loket_riverbend";
    map.scene = "assets/scad/proc/nextdayz/cz_loket_riverbend/published/v2/scene.scad";
    map.version = 2;
    map.published = true;
    FCampaignSave saved;
    saved.map = LegacyRiverlandScene;
    saved.campaign.inField = true;
    saved.campaign.gold = 37;
    saved.campaign.sorties = saved.campaign.sortieId = 4;
    saved.campaign.extractions = 1;
    saved.campaign.flight = {EFlightPhase::Inbound, "old_pad", {}, 12, false};
    saved.campaign.storage = {{1,"bandage",2,0,0}};
    saved.campaign.nextStorageId = 2;
    saved.world.seconds = 123;
    saved.time.minutes = 555;
    Inventory inventory;
    FItemInstanceId gun = 0, pack = 0;
    REQUIRE(inventory.TryAdd("backpack", "Backpack", EItemKind::Clothing, 1, &pack, kWornContainer));
    REQUIRE(inventory.TryAdd("ak", "AK", EItemKind::Weapon, 1, &gun));
    REQUIRE(inventory.SetLoadedAmmo(gun, 17));
    REQUIRE(inventory.TryAdd("food_can", "Food", EItemKind::Consumable, 2));
    saved.inventory = inventory.Capture();
    saved.weaponSlots[0] = gun;
    saved.hasCacheMarker = saved.lanternLit = true;
    FMapWorldSave fresh;
    fresh.version = map.version;
    fresh.world.seconds = 7;
    fresh.time.minutes = 480;

    SECTION("in-flight and safehouse saves both enter the new safehouse")
    {
        for (bool inField : {true, false})
        {
            auto candidate = saved;
            candidate.campaign.inField = inField;
            if (!inField) candidate.campaign.flight = {};
            REQUIRE(MigrateRiverlandCampaign(candidate, map, fresh, {1,10,2}));
            CHECK(candidate.map == map.scene);
            CHECK(candidate.mapVersion == 2);
            CHECK_FALSE(candidate.campaign.inField);
            CHECK(candidate.campaign.flight.phase == EFlightPhase::Idle);
            CHECK(candidate.campaign.gold == 37);
            CHECK(candidate.campaign.sorties == 4);
            CHECK(candidate.campaign.extractions == 1);
            CHECK(candidate.campaign.storage.size() == 1);
            CHECK(candidate.campaign.storage.front().count == 2);
            CHECK(candidate.maps.at(std::string(LegacyRiverlandScene)).world.seconds == 123);
            CHECK(candidate.maps.at(std::string(LegacyRiverlandScene)).time.minutes == 555);
            CHECK(candidate.world.seconds == 7);
            CHECK(candidate.position == glm::vec3(1,10,2));
            CHECK_FALSE(candidate.hasCacheMarker);
            CHECK_FALSE(candidate.lanternLit);
            Inventory carried;
            REQUIRE(carried.Restore(candidate.inventory));
            CHECK(carried.IsWorn(pack));
            CHECK(carried.LoadedAmmo(gun) == 17);
            CHECK(carried.CountOf("food_can") == 2);
            CHECK(candidate.weaponSlots[0] == gun);
            CHECK_FALSE(MigrateRiverlandCampaign(candidate, map, fresh, {}));
            CHECK(candidate.campaign.storage.size() == 1);
        }
    }
    SECTION("an existing destination world is preserved and version mismatches leave the old save intact")
    {
        saved.maps[map.id] = fresh;
        saved.maps[map.id].world.seconds = 321;
        auto incompatible = saved;
        incompatible.maps[map.id].version = 1;
        CHECK_FALSE(MigrateRiverlandCampaign(incompatible, map, fresh, {}));
        CHECK(incompatible.map == LegacyRiverlandScene);
        CHECK(incompatible.campaign.inField);
        REQUIRE(MigrateRiverlandCampaign(saved, map, fresh, {}));
        CHECK(saved.world.seconds == 321);
    }
}

TEST_CASE("NextDayz published revisions preserve shared progress and archive only the replaced region", "[NextDayz][Startup]")
{
    using namespace NextDayz;
    FExpeditionMap map;
    map.id = "cz_loket_riverbend";
    map.scene = "assets/scad/proc/nextdayz/cz_loket_riverbend/published/v3/scene.scad";
    map.draftScene = "assets/scad/proc/nextdayz/cz_loket_riverbend/scene.scad";
    map.version = 3;
    map.published = true;
    FCampaignSave saved;
    saved.map = "assets/scad/proc/nextdayz/cz_loket_riverbend/published/v2/scene.scad";
    saved.mapVersion = 2;
    saved.campaign.inField = true;
    saved.campaign.sorties = 7;
    saved.campaign.gold = 37;
    saved.campaign.mapSorties[map.id] = 4;
    saved.world.seconds = 123;
    saved.maps["other_region"].world.seconds = 77;
    Inventory inventory;
    REQUIRE(inventory.TryAdd("food_can", "Food", EItemKind::Consumable, 2));
    saved.inventory = inventory.Capture();
    CHECK(FindExpeditionMap({map},saved.map) != nullptr);
    CHECK(FindExpeditionMap({map},"assets/scad/proc/nextdayz/cz_loket_riverbend/published/v../scene.scad") == nullptr);
    FMapWorldSave fresh;
    fresh.version = 3;
    fresh.world.seconds = 0;
    REQUIRE(UpgradeExpeditionCampaign(saved,map,fresh,{1,2,3}));
    CHECK(saved.map == map.scene);
    CHECK(saved.mapVersion == 3);
    CHECK_FALSE(saved.campaign.inField);
    CHECK(saved.campaign.gold == 37);
    CHECK(saved.campaign.sorties == 7);
    CHECK(saved.campaign.mapSorties.at(map.id) == 4);
    CHECK(saved.maps.at("cz_loket_riverbend@v2").world.seconds == 123);
    CHECK(saved.maps.at("other_region").world.seconds == 77);
    CHECK(saved.world.seconds == 0);
    Inventory restored;
    REQUIRE(restored.Restore(saved.inventory));
    CHECK(restored.CountOf("food_can") == 2);
    CHECK_FALSE(UpgradeExpeditionCampaign(saved,map,fresh,{}));
}

TEST_CASE("NextDayz PCG encounters have flat entrances native roads and clear vegetation", "[NextDayz][Landscape]")
{
    struct MapCheck { const char* id; int sites, buildings, minTrees, minShrubs; };
    for (const auto& map : {MapCheck{"cz_loket_riverbend",15,108,2000,400}})
    {
        INFO(map.id);
        Assets::EnvironmentSetting environment;
        std::vector<std::shared_ptr<Assets::Node>> nodes;
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;
        REQUIRE(Assets::FScadLoader::LoadScadScene(fmt::format("assets/scad/proc/nextdayz/{}/scene.scad",map.id),
            environment,nodes,models,materials,lights,tracks,skeletons));
        Runtime::TerrainComponent* terrain = nullptr;
        std::set<std::string> kinds, lootProfiles;
        int buildings = 0, trees = 0, shrubs = 0, roadMeshes = 0, sites = 0, loot = 0;
        float maximumGroundGap = 0;
        for (const auto& node : nodes)
        {
            node->RecalcTransform(true);
            if (auto component = node->GetComponent<Runtime::TerrainComponent>()) terrain = component;
        }
        REQUIRE(terrain);
        auto parameter = [](const std::string& metadata, std::string_view name)
        {
            const auto start = metadata.find(std::string(name) + "=");
            REQUIRE(start != std::string::npos);
            const auto value = start + name.size() + 1;
            return metadata.substr(value,metadata.find(';',value)-value);
        };
        for (const auto& node : nodes)
        {
            const auto& name = node->GetName();
            const auto& metadata = node->GetMetadata();
            sites += name == "nd_pcg_encounter";
            roadMeshes += name.starts_with("rd_street") || name.starts_with("street_");
            if (name == "nd_loot") { ++loot; lootProfiles.insert(parameter(metadata,"p")); }
            if (name == "nd_pcg_plant")
            {
                shrubs += parameter(metadata,"kind") == "shrub";
                trees += parameter(metadata,"kind") != "shrub";
                const glm::vec3 pos = node->WorldTransform() * glm::vec4(0,0,0,1);
                CHECK_FALSE(terrain->IsWater(pos.x,pos.z));
                CHECK(terrain->BiomeId(pos.x,pos.z) < 8);
                for (const auto& site : nodes)
                {
                    if (site->GetName() != "nd_pcg_encounter") continue;
                    const auto pad = nlohmann::json::parse("["+parameter(site->GetMetadata(),"pad")+"]");
                    const glm::vec3 local = glm::inverse(site->WorldTransform()) * glm::vec4(pos,1);
                    CHECK((std::abs(local.x) > pad[0].get<float>()/2+0.9f || std::abs(local.z) > pad[1].get<float>()/2+0.9f));
                }
            }
            if (name != "nd_pcg_building") continue;
            ++buildings;
            kinds.insert(parameter(metadata,"kind"));
            const float width = std::stof(parameter(metadata,"L")), depth = std::stof(parameter(metadata,"D"));
            const auto transform = node->WorldTransform();
            const float datum = glm::vec3(transform * glm::vec4(0,0,0,1)).y;
            // Shared pads support the entire floor and the walk-in entrance.
            for (int y=-3;y<=3;++y)
                for (int x=-3;x<=3;++x)
                {
                    const glm::vec3 sample = transform * glm::vec4(x*width/6,0,y*depth/6,1);
                    const float gap = std::abs(datum-terrain->SampleHeight(sample.x,sample.z));
                    maximumGroundGap = std::max(maximumGroundGap,gap);
                    INFO(metadata << " ground gap=" << gap);
                    CHECK(gap < 0.16f);
                }
            const glm::vec3 entrance = transform * glm::vec4(0,0,depth/2+0.7f,1);
            CHECK(std::abs(datum-terrain->SampleHeight(entrance.x,entrance.z)) < 0.16f);
        }
        CHECK(sites == map.sites);
        CHECK(buildings == map.buildings);
        CHECK(kinds.size() >= 10);
        CHECK(loot >= map.buildings*3);
        CHECK(lootProfiles.size() >= 6);
        CHECK(trees >= map.minTrees);
        CHECK(shrubs >= map.minShrubs);
        CHECK(roadMeshes == 0);
        WARN("Maximum building/terrain gap: " << maximumGroundGap << "m; trees=" << trees << "; shrubs=" << shrubs << "; loot=" << loot);
        int roadCells = 0;
        for (auto flags : terrain->GetGridData()->flags) roadCells += (flags & Runtime::TerrainComponent::kFlagRoad) != 0;
        CHECK(roadCells > 200);
    }
}

TEST_CASE("NextDayz geographic maps have independent persistent loot and mission counts", "[NextDayz][Geography]")
{
    using namespace NextDayz;
    FCampaignSave state;
    state.map = "map-b";
    state.inventory = Inventory{}.Capture();
    state.campaign.sorties = state.campaign.sortieId = 3;
    state.campaign.mapSorties = {{"map-a",2},{"map-b",1}};
    state.maps["map-a"].world.seconds = 150;
    state.maps["map-b"].world.seconds = 20;
    state.maps["map-a"].time.minutes = 300;
    state.maps["map-b"].time.minutes = 600;
    state.maps["map-a"].world.pileSerial = 7;
    const auto path = std::filesystem::temp_directory_path() /
        fmt::format("nextdayz-world-{}.json", std::chrono::steady_clock::now().time_since_epoch().count());
    std::string error;
    REQUIRE(WriteCampaignSave(path, state, error));
    FCampaignSave restored;
    REQUIRE(ReadCampaignSave(path, restored, error));
    CHECK(restored.maps.at("map-a").world.seconds == 150);
    CHECK(restored.maps.at("map-b").world.seconds == 20);
    CHECK(restored.maps.at("map-a").time.minutes == 300);
    CHECK(restored.maps.at("map-b").time.minutes == 600);
    CHECK(restored.maps.at("map-a").world.pileSerial == 7);
    CHECK(restored.campaign.mapSorties.at("map-a") == 2);
    CHECK(restored.campaign.mapSorties.at("map-b") == 1);
    // Existing single-map extraction saves upgrade without resetting inventory
    // or the total sortie counter. The new per-map counts start from zero.
    nlohmann::json legacy;
    { std::ifstream input(path); input >> legacy; }
    for (auto& container : legacy["state"]["inventory"]["containers"])
    {
        container["capacity"] = container["weaponOnly"].get<bool>() ? 10 :
            container["width"].get<int>() * container["height"].get<int>();
        container.erase("width");
        container.erase("height");
    }
    auto legacyTwo = legacy;
    legacyTwo["version"] = 2;
    legacyTwo["state"]["weaponSlots"] = nlohmann::json::array({0, 0});
    legacyTwo["state"].erase("previousSlot");
    { std::ofstream output(path); output << legacyTwo; }
    REQUIRE(ReadCampaignSave(path, restored, error));
    CHECK(restored.weaponSlots[2] == 0);
    CHECK(restored.previousSlot == -1);
    CHECK(restored.maps.at("map-a").world.seconds == 150);
    legacy["version"] = 1;
    legacy["state"]["weaponSlots"] = nlohmann::json::array({0, 0});
    legacy["state"].erase("previousSlot");
    legacy["state"].erase("maps");
    legacy["state"].erase("mapVersion");
    for (const auto* key : {"mapSorties","lastMapId","lastOutcome","sortieId"})
        legacy["state"]["campaign"].erase(key);
    { std::ofstream output(path); output << legacy; }
    REQUIRE(ReadCampaignSave(path,restored,error));
    CHECK(restored.campaign.sorties == 3);
    CHECK(restored.campaign.sortieId == 3);
    CHECK(restored.campaign.mapSorties.empty());
    state.maps["map-a"].world.seconds = std::numeric_limits<double>::infinity();
    CHECK_FALSE(WriteCampaignSave(path,state,error));
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("NextDayz geographic SCAD scenes retain finite bounded geometry and gameplay markers", "[NextDayz][Geography][Scad]")
{
    std::vector<NextDayz::FExpeditionMap> maps;
    std::string error;
    REQUIRE(NextDayz::LoadExpeditionMaps(Utilities::FileHelper::ResolvePlatformFilePath(
        "assets/configs/nextdayz_maps.json"),maps,error));
    REQUIRE(maps.size() == 2);
    CHECK(std::count_if(maps.begin(), maps.end(), [](const auto& map) { return map.training; }) == 1);
    for (const auto& map : maps)
    {
        INFO(map.id);
        Assets::EnvironmentSetting environment;
        std::vector<std::shared_ptr<Assets::Node>> nodes;
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;
        REQUIRE(Assets::FScadLoader::LoadScadScene(map.scene,environment,nodes,models,materials,lights,tracks,skeletons));
        int spawns = 0, extractions = 0, buildings = 0;
        for (const auto& node : nodes)
        {
            spawns += node->GetName() == "nd_spawn_player_safe";
            extractions += node->GetName() == "nd_extraction";
            buildings += node->GetName() == "nd_geo_building" || node->GetName() == "nd_pcg_building";
            const auto render = node->GetComponent<Runtime::RenderComponent>();
            if (!render || render->GetModelId() >= models.size()) continue;
            node->RecalcTransform(true);
            glm::vec3 lo(std::numeric_limits<float>::max()), hi(-lo);
            for (const auto& vertex : models[render->GetModelId()].CPUVertices())
            {
                const glm::vec3 p = node->WorldTransform() * glm::vec4(vertex.Position,1);
                lo = glm::min(lo,p); hi = glm::max(hi,p);
            }
            INFO(node->GetName() << " model=" << render->GetModelId() << " bounds=" << lo.x << "," << lo.y << "," << lo.z << " / " << hi.x << "," << hi.y << "," << hi.z);
            CHECK(std::isfinite(lo.x));
            CHECK(hi.y - lo.y < 225.0f);
            CHECK(std::max(std::abs(lo.x),std::abs(hi.x)) < 530.0f);
            CHECK(std::max(std::abs(lo.z),std::abs(hi.z)) < 530.0f);
        }
        CHECK(spawns >= 3);
        CHECK(extractions == 2);
        CHECK(buildings >= (map.training ? 20 : 40));
    }
}

TEST_CASE("NextDayz retired regions return their saves to a safehouse", "[NextDayz][Startup]")
{
    using namespace NextDayz;
    FExpeditionMap loket;
    loket.id = "cz_loket_riverbend";
    loket.scene = "assets/scad/proc/nextdayz/cz_loket_riverbend/published/v7/scene.scad";
    loket.draftScene = "assets/scad/proc/nextdayz/cz_loket_riverbend/scene.scad";
    loket.version = 7;
    loket.published = true;
    FExpeditionMap training = loket;
    training.id = "training_ground";
    training.scene = "assets/scad/proc/nextdayz/training_ground/published/v1/scene.scad";
    training.draftScene = "assets/scad/proc/nextdayz/training_ground/scene.scad";
    training.version = 1;
    training.training = true;
    const std::vector<FExpeditionMap> maps{loket, training};

    CHECK(RetiredExpeditionMapId(maps, "assets/scad/proc/nextdayz/jp_otaru_north_canal/published/v4/scene.scad") == "jp_otaru_north_canal");
    CHECK(RetiredExpeditionMapId(maps, "assets/scad/proc/nextdayz/cn_chengdu_qinglong_lake/scene.scad") == "cn_chengdu_qinglong_lake");
    CHECK(RetiredExpeditionMapId(maps, loket.scene).empty());
    CHECK(RetiredExpeditionMapId(maps, "assets/scad/proc/nextdayz/cz_loket_riverbend/published/v9/extra.scad").empty());
    CHECK(RetiredExpeditionMapId(maps, std::string(LegacyRiverlandScene)).empty());
    CHECK(RetiredExpeditionMapId(maps, "assets/scad/proc/other/x.scad").empty());
    CHECK_FALSE(IsDispatchable(training, false));
    CHECK_FALSE(IsDispatchable(training, true));
    CHECK(IsDispatchable(loket, false));

    FCampaignSave saved;
    saved.map = "assets/scad/proc/nextdayz/jp_otaru_north_canal/published/v4/scene.scad";
    saved.mapVersion = 4;
    saved.campaign.inField = true;
    saved.campaign.gold = 12;
    saved.campaign.flight = {EFlightPhase::Inbound, "old_pad", {}, 3, false};
    saved.world.seconds = 99;
    Inventory inventory;
    REQUIRE(inventory.TryAdd("ak", "AK", EItemKind::Weapon, 1));
    saved.inventory = inventory.Capture();
    FMapWorldSave fresh;
    fresh.version = loket.version;
    CHECK_FALSE(RetireExpeditionCampaign(saved, training, fresh, {}, "jp_otaru_north_canal", "x"));
    REQUIRE(RetireExpeditionCampaign(saved, loket, fresh, {1,2,3}, "jp_otaru_north_canal", "RETURNED / REGION RETIRED"));
    CHECK(saved.map == loket.scene);
    CHECK(saved.mapVersion == 7);
    CHECK_FALSE(saved.campaign.inField);
    CHECK(saved.campaign.flight.phase == EFlightPhase::Idle);
    CHECK(saved.campaign.gold == 12);
    CHECK(saved.campaign.lastMapId == "jp_otaru_north_canal");
    CHECK(saved.maps.at("jp_otaru_north_canal").world.seconds == 99);
    Inventory carried;
    REQUIRE(carried.Restore(saved.inventory));
    CHECK(carried.CountOf("ak") == 1);
}

TEST_CASE("NextDayz training ground displays every item and scores the range", "[NextDayz][Landscape][Training]")
{
    Assets::EnvironmentSetting environment;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;
    REQUIRE(Assets::FScadLoader::LoadScadScene("assets/scad/proc/nextdayz/training_ground/scene.scad",
        environment,nodes,models,materials,lights,tracks,skeletons));
    Runtime::TerrainComponent* terrain = nullptr;
    for (const auto& node : nodes)
    {
        node->RecalcTransform(true);
        if (auto component = node->GetComponent<Runtime::TerrainComponent>()) terrain = component;
    }
    REQUIRE(terrain);
    auto parameter = [](const std::string& metadata, std::string_view name)
    {
        const auto start = metadata.find(std::string(name) + "=");
        if (start == std::string::npos) return std::string();
        const auto value = start + name.size() + 1;
        return metadata.substr(value,metadata.find(';',value)-value);
    };
    std::set<std::string> displayed;
    int sites = 0, buildings = 0, targets = 0, scored = 0, infected = 0, spawns = 0, extractions = 0;
    for (const auto& node : nodes)
    {
        const auto& name = node->GetName();
        const auto& metadata = node->GetMetadata();
        sites += name == "nd_pcg_encounter";
        infected += name == "nd_zed";
        spawns += name == "nd_spawn_player_safe";
        extractions += name == "nd_extraction";
        if (name == "nd_item")
        {
            const std::string id = parameter(metadata,"id");
            INFO("display item " << id);
            CHECK(NextDayz::FindItemDef(id) != nullptr);
            displayed.insert(id);
            // Displays stand on a table, a shelf or the floor, never inside the ground.
            const glm::vec3 pos = node->WorldTransform() * glm::vec4(0,0,0,1);
            CHECK(pos.y > terrain->SampleHeight(pos.x,pos.z) - 0.05f);
        }
        if (name == "nd_target")
        {
            ++targets;
            scored += std::stof(parameter(metadata,"r")) > 0.0f;
            CHECK(std::stof(parameter(metadata,"d")) > 0.0f);
            CHECK((node->GetComponent<Runtime::RenderComponent>() != nullptr || !node->Children().empty()));
        }
        if (name != "nd_pcg_building") continue;
        ++buildings;
        CHECK(parameter(metadata,"region") == "training");
        const float width = std::stof(parameter(metadata,"L")), depth = std::stof(parameter(metadata,"D"));
        const auto transform = node->WorldTransform();
        const float datum = glm::vec3(transform * glm::vec4(0,0,0,1)).y;
        for (int y=-2;y<=2;++y)
            for (int x=-2;x<=2;++x)
            {
                const glm::vec3 sample = transform * glm::vec4(x*width/4,0,y*depth/4,1);
                INFO(metadata);
                CHECK(std::abs(datum-terrain->SampleHeight(sample.x,sample.z)) < 0.16f);
            }
    }
    for (const NextDayz::FItemDef& item : NextDayz::AllItemDefs())
    {
        INFO("missing from the training ground displays: " << item.id);
        CHECK(displayed.contains(std::string(item.id)));
    }
    CHECK(sites == 12);
    CHECK(buildings == 21);
    CHECK(targets >= 17);
    CHECK(scored >= 3);
    CHECK(infected >= 3);
    CHECK(spawns >= 3);
    CHECK(extractions == 2);
}
