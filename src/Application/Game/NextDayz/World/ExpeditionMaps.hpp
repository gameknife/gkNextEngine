#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include <glm/vec3.hpp>

namespace NextDayz
{
    inline constexpr std::string_view LegacyRiverlandScene = "assets/scad/proc/coldwar/riverland_1km.scad";
    struct FCampaignSave;
    struct FMapWorldSave;

    struct FExpeditionMap
    {
        std::string id;
        std::string name;
        std::string region;
        std::string description;
        std::string terrain;
        std::string scene;
        std::string draftScene;
        std::string profile;
        double latitude = 0.0;
        double longitude = 0.0;
        float sizeM = 1000.0f;
        uint32_t version = 1;
        bool published = false;
    };

    bool LoadExpeditionMaps(const std::filesystem::path& path, std::vector<FExpeditionMap>& maps, std::string& error);
    const FExpeditionMap* FindExpeditionMap(const std::vector<FExpeditionMap>& maps, std::string_view idOrScene);
    bool MigrateRiverlandCampaign(FCampaignSave& saved, const FExpeditionMap& map,
                                 const FMapWorldSave& freshWorld, const glm::vec3& safePosition);
    bool UpgradeExpeditionCampaign(FCampaignSave& saved, const FExpeditionMap& map,
                                   const FMapWorldSave& freshWorld, const glm::vec3& safePosition);
}
