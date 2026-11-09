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
        // Registry "kind": "training". A practice ground: entered on purpose from
        // the safehouse, never drawn as a random assignment, not on the atlas.
        bool training = false;
    };

    inline bool IsTrainingMap(const FExpeditionMap& map) { return map.training; }
    // Maps a random assignment may send the player to.
    inline bool IsDispatchable(const FExpeditionMap& map, bool authoring)
    {
        return !map.training && (map.published || authoring);
    }

    bool LoadExpeditionMaps(const std::filesystem::path& path, std::vector<FExpeditionMap>& maps, std::string& error);
    const FExpeditionMap* FindExpeditionMap(const std::vector<FExpeditionMap>& maps, std::string_view idOrScene);
    bool MigrateRiverlandCampaign(FCampaignSave& saved, const FExpeditionMap& map,
                                 const FMapWorldSave& freshWorld, const glm::vec3& safePosition);
    // Moves a save whose region left the registry to `map`'s safehouse, keeping
    // gear, storage, gold and history; the old world is archived under retiredKey.
    bool RetireExpeditionCampaign(FCampaignSave& saved, const FExpeditionMap& map, const FMapWorldSave& freshWorld,
                                  const glm::vec3& safePosition, std::string_view retiredKey, std::string_view outcome);
    // The id of a removed NextDayz region a saved scene belongs to; empty when the
    // scene is not under assets/scad/proc/nextdayz/ or a registry map still owns it.
    std::string RetiredExpeditionMapId(const std::vector<FExpeditionMap>& maps, std::string_view scene);
    bool UpgradeExpeditionCampaign(FCampaignSave& saved, const FExpeditionMap& map,
                                   const FMapWorldSave& freshWorld, const glm::vec3& safePosition);
}
