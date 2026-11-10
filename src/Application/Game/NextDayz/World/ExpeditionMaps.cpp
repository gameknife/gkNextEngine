#include "Engine/Common/CoreMinimal.hpp"
#include "ExpeditionMaps.hpp"
#include "Campaign.hpp"

#include <cmath>
#include <nlohmann/json.hpp>

namespace NextDayz
{
    bool UpgradeExpeditionCampaign(FCampaignSave& saved, const FExpeditionMap& map,
                                   const FMapWorldSave& freshWorld, const glm::vec3& safePosition)
    {
        if (!map.published || saved.mapVersion >= map.version || freshWorld.version != map.version) return false;
        saved.maps[fmt::format("{}@v{}",map.id,saved.mapVersion)] = {saved.world,saved.time,saved.mapVersion};
        saved.maps[map.id] = freshWorld;
        saved.map = map.scene;
        saved.mapVersion = map.version;
        saved.world = freshWorld.world;
        saved.time = freshWorld.time;
        saved.position = safePosition;
        saved.yaw = saved.pitch = 0.0f;
        saved.campaign.inField = false;
        saved.campaign.flight = {};
        saved.campaign.lastMapId = map.id;
        saved.campaign.lastOutcome = "RETURNED / REGION UPDATED";
        saved.survival = {};
        saved.survivalSeconds = 0;
        saved.hasCacheMarker = saved.lanternLit = false;
        saved.cacheSeconds = 0;
        return true;
    }

    bool RetireExpeditionCampaign(FCampaignSave& saved, const FExpeditionMap& map, const FMapWorldSave& freshWorld,
                                  const glm::vec3& safePosition, std::string_view retiredKey, std::string_view outcome)
    {
        if (retiredKey.empty() || !map.published || map.version == 0 || IsTrainingMap(map)) return false;
        const auto existing = saved.maps.find(map.id);
        if (existing != saved.maps.end() && existing->second.version != map.version) return false;
        const FMapWorldSave destination = existing == saved.maps.end() ? freshWorld : existing->second;
        if (destination.version != map.version) return false;

        saved.maps[std::string(retiredKey)] = {saved.world, saved.time, saved.mapVersion};
        saved.maps[map.id] = destination;
        saved.map = map.scene;
        saved.mapVersion = map.version;
        saved.world = destination.world;
        saved.time = destination.time;
        saved.position = safePosition;
        saved.yaw = saved.pitch = 0.0f;
        saved.campaign.inField = false;
        saved.campaign.flight = {};
        saved.campaign.lastMapId = std::string(retiredKey);
        saved.campaign.lastOutcome = std::string(outcome);
        saved.survival = {};
        saved.survivalSeconds = 0.0;
        saved.hasCacheMarker = false;
        saved.cacheSeconds = 0.0f;
        saved.lanternLit = false;
        return true;
    }

    bool MigrateRiverlandCampaign(FCampaignSave& saved, const FExpeditionMap& map,
                                 const FMapWorldSave& freshWorld, const glm::vec3& safePosition)
    {
        if (saved.map != LegacyRiverlandScene) return false;
        return RetireExpeditionCampaign(saved, map, freshWorld, safePosition, LegacyRiverlandScene,
                                        "RETURNED / RIVERLAND RETIRED");
    }

    std::string RetiredExpeditionMapId(const std::vector<FExpeditionMap>& maps, std::string_view scene)
    {
        constexpr std::string_view root = "assets/scad/proc/nextdayz/";
        if (!scene.starts_with(root) || !scene.ends_with(".scad") || FindExpeditionMap(maps, scene)) return {};
        const std::string_view rest = scene.substr(root.size());
        const std::string_view id = rest.substr(0, rest.find('/'));
        if (id.empty() || id.size() == rest.size() ||
            !std::all_of(id.begin(), id.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'; }))
            return {};
        // A registered id at a path it no longer uses is a stale revision, not a retired region.
        if (std::any_of(maps.begin(), maps.end(), [id](const auto& map) { return map.id == id; })) return {};
        return std::string(id);
    }

    bool LoadExpeditionMaps(const std::filesystem::path& path, std::vector<FExpeditionMap>& maps, std::string& error)
    {
        try
        {
            std::ifstream input(path);
            const auto document = nlohmann::json::parse(input);
            if (document.at("version").get<int>() != 1) throw std::runtime_error("Unsupported map registry");
            std::vector<FExpeditionMap> candidate;
            std::unordered_set<std::string> ids, scenes;
            for (const auto& entry : document.at("maps"))
            {
                FExpeditionMap map;
                map.id = entry.at("id").get<std::string>();
                map.name = entry.at("name").get<std::string>();
                map.region = entry.at("region").get<std::string>();
                map.description = entry.at("description").get<std::string>();
                map.terrain = entry.at("terrain").get<std::string>();
                map.scene = entry.at("scene").get<std::string>();
                map.draftScene = entry.value("draftScene", map.scene);
                map.profile = entry.at("profile").get<std::string>();
                const auto kind = entry.value("kind", std::string("expedition"));
                if (kind != "expedition" && kind != "training") throw std::runtime_error("Invalid map kind");
                map.training = kind == "training";
                // A training ground is no real place: it has no centre and stays off the atlas.
                if (!map.training || entry.contains("center"))
                {
                    map.latitude = entry.at("center").at(0).get<double>();
                    map.longitude = entry.at("center").at(1).get<double>();
                }
                map.sizeM = entry.at("sizeM").get<float>();
                map.version = entry.at("mapVersion").get<uint32_t>();
                const auto status = entry.at("status").get<std::string>();
                if (status != "draft" && status != "published") throw std::runtime_error("Invalid map status");
                map.published = status == "published";
                if (map.id.empty() || map.name.empty() || map.version == 0 ||
                    !ids.insert(map.id).second || !scenes.insert(map.scene).second ||
                    !std::isfinite(map.latitude) || std::abs(map.latitude) > 60.0 ||
                    !std::isfinite(map.longitude) || std::abs(map.longitude) > 180.0 ||
                    !std::isfinite(map.sizeM) || map.sizeM < 100.0f || map.sizeM > 1000.0f ||
                    !map.scene.starts_with("assets/scad/") || !map.scene.ends_with(".scad") ||
                    map.scene.find("..") != std::string::npos)
                    throw std::runtime_error("Invalid or duplicate expedition map");
                candidate.push_back(std::move(map));
            }
            if (candidate.empty()) throw std::runtime_error("Empty expedition map registry");
            maps = std::move(candidate);
            error.clear();
            return true;
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
            return false;
        }
    }

    const FExpeditionMap* FindExpeditionMap(const std::vector<FExpeditionMap>& maps, std::string_view idOrScene)
    {
        const auto found = std::find_if(maps.begin(), maps.end(), [idOrScene](const auto& map)
        {
            if (map.id == idOrScene || map.scene == idOrScene || map.draftScene == idOrScene) return true;
            const auto prefix = std::filesystem::path(map.draftScene).parent_path().generic_string() + "/published/v";
            constexpr std::string_view suffix = "/scene.scad";
            if (!idOrScene.starts_with(prefix) || !idOrScene.ends_with(suffix) ||
                idOrScene.size() <= prefix.size()+suffix.size()) return false;
            const auto revision = idOrScene.substr(prefix.size(),idOrScene.size()-prefix.size()-suffix.size());
            return std::all_of(revision.begin(),revision.end(),[](char ch) { return ch >= '0' && ch <= '9'; });
        });
        return found == maps.end() ? nullptr : &*found;
    }
}
