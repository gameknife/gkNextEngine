#include "Engine/Common/CoreMinimal.hpp"

#include "WorldAnchorRegistry.hpp"
#include "Modules/ScadLoader/FScadShared.h"

namespace NextDayz
{
    namespace
    {
        std::optional<std::pair<EWorldAnchorType, std::string>> Parse(std::string_view name)
        {
            if (name == "nd_extraction") return std::pair{EWorldAnchorType::Extraction, std::string("helipad")};
            if (name == "cw_prop_well" || name == "cw_prop_water_pump")
            {
                return std::pair{EWorldAnchorType::Well, std::string("water")};
            }
            if (name == "nd_poi")
            {
                return std::pair{EWorldAnchorType::Poi, std::string("poi")};
            }
            // Map shorthand: the profile comes from the call's metadata (p = ...).
            if (name == "nd_loot")
            {
                return std::pair{EWorldAnchorType::Loot, std::string("residential")};
            }
            // A display: one named item that is always there (training ground).
            if (name == "nd_item")
            {
                return std::pair{EWorldAnchorType::Loot, std::string("military")};
            }
            if (name == "nd_zed")
            {
                return std::pair{EWorldAnchorType::Zombie, std::string("civilian")};
            }
            constexpr std::string_view prefix = "nd_spawn_";
            if (!name.starts_with(prefix))
            {
                return std::nullopt;
            }
            name.remove_prefix(prefix.size());
            if (name == "player_safe")
            {
                return std::pair{EWorldAnchorType::PlayerSafe, std::string("safe")};
            }
            constexpr std::string_view zombie = "zombie_";
            if (name.starts_with(zombie))
            {
                name.remove_prefix(zombie.size());
                return std::pair{EWorldAnchorType::Zombie, std::string(name)};
            }
            constexpr std::string_view loot = "loot_";
            if (name.starts_with(loot))
            {
                name.remove_prefix(loot.size());
                return std::pair{EWorldAnchorType::Loot, std::string(name)};
            }
            return std::nullopt;
        }
    }

    bool IsWorldAnchor(std::string_view name)
    {
        return Parse(name).has_value();
    }

    std::optional<FWorldAnchor> ReadWorldAnchor(std::string_view name, std::string_view sourceMetadata,
                                               const glm::mat4& world, uint32_t id)
    {
        const auto parsed = Parse(name);
        if (!parsed)
        {
            return std::nullopt;
        }
        FWorldAnchor anchor{parsed->first, parsed->second, {}, glm::vec3(world[3]), id};
        if (!sourceMetadata.empty())
        {
            const Assets::Scad::FMetadata metadata = Assets::Scad::ParseScadMetadata(sourceMetadata);
            anchor.group = std::max(1, static_cast<int>(Assets::Scad::MetadataNumber(metadata, "group", 1.0)));
            if (anchor.type == EWorldAnchorType::Loot || anchor.type == EWorldAnchorType::Zombie)
            {
                anchor.profile = Assets::Scad::MetadataString(metadata, "p", anchor.profile);
            }
            if (anchor.type == EWorldAnchorType::Loot && name == "nd_item")
            {
                anchor.item = Assets::Scad::MetadataString(metadata, "id", "");
                anchor.count = std::max(1, static_cast<int>(Assets::Scad::MetadataNumber(metadata, "n", 1.0)));
                // Local +X in world space: the yaw of angleAxis(yaw, +Y) that produced it.
                const glm::vec3 axis = glm::vec3(world * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
                anchor.yaw = std::atan2(-axis.z, axis.x);
            }
            if (anchor.type == EWorldAnchorType::Poi)
            {
                anchor.label = Assets::Scad::MetadataString(metadata, "name", "Unknown");
                anchor.profile = Assets::Scad::MetadataString(metadata, "kind", "village");
            }
            if (anchor.type == EWorldAnchorType::Extraction)
            {
                anchor.label = Assets::Scad::MetadataString(metadata, "name", "Helipad");
                anchor.profile = Assets::Scad::MetadataString(metadata, "id", anchor.label);
            }
        }
        return anchor;
    }
}
