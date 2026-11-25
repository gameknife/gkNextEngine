#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include <glm/vec2.hpp>

// The sites of a published map, read from the generation record next to its
// scene (published/v<N>/generation.json), so the safehouse can offer and judge
// contracts without loading the map. The economy design §6.2 planned a separate
// pois.json; the published generation record already carries every field it
// would hold (id, name, type, centre, pad size, risk).
namespace NextDayz
{
    struct FPoi
    {
        std::string id;
        std::string name;        // matches the nd_poi anchor's name in the scene
        std::string kind;        // nd_poi_kind: hospital | military | fortress | town | industrial | farm | village
        uint32_t risk = 1;
        glm::vec2 center{};      // game (x, z); SCAD (x, y) is (x, -y)
        float radius = 10.0f;    // half the pad diagonal + 10 m: entering this counts as a visit
    };

    // Map id -> its sites, ordered by map id so board rolls are reproducible.
    using FPoiIndex = std::map<std::string, std::vector<FPoi>>;

    // The site kind nd_pcg_encounter gives its nd_poi (nextdayz_pcg.scad nd_poi_kind).
    std::string PoiKind(std::string_view siteType);
    bool LoadPoiIndex(const std::filesystem::path& generation, std::vector<FPoi>& pois, std::string& error);
    const FPoi* FindPoi(std::span<const FPoi> pois, std::string_view name);
    // Sites whose range contains `position` (x, z).
    std::vector<const FPoi*> PoisAt(std::span<const FPoi> pois, const glm::vec2& position);
}
