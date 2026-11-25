#include "Engine/Common/CoreMinimal.hpp"
#include "PoiIndex.hpp"

#include <cmath>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

namespace NextDayz
{
    std::string PoiKind(std::string_view type)
    {
        if (type == "medical") return "hospital";
        if (type == "military" || type.starts_with("tg_")) return "military";
        if (type == "hillcastle" || type == "castle") return "fortress";
        if (type == "square" || type == "street") return "town";
        if (type == "jzd" || type == "factory" || type == "industry") return "industrial";
        if (type == "homestead" || type == "farm") return "farm";
        return "village";
    }

    bool LoadPoiIndex(const std::filesystem::path& generation, std::vector<FPoi>& pois, std::string& error)
    {
        try
        {
            std::ifstream stream(generation, std::ios::binary);
            if (!stream) throw std::runtime_error("cannot open " + generation.string());
            const auto document = nlohmann::json::parse(stream);
            std::vector<FPoi> result;
            for (const auto& site : document.at("sites"))
            {
                FPoi poi;
                poi.id = site.at("id").get<std::string>();
                poi.name = site.at("name").get<std::string>();
                poi.kind = PoiKind(site.at("kind").get<std::string>());
                poi.risk = std::clamp(site.value("risk", 1u), 1u, 3u);
                const auto& at = site.at("at");
                const auto& size = site.at("size");
                poi.center = {at.at(0).get<float>(), -at.at(1).get<float>()};
                poi.radius = 0.5f * std::hypot(size.at(0).get<float>(), size.at(1).get<float>()) + 10.0f;
                if (poi.name.empty() || !std::isfinite(poi.center.x) || !std::isfinite(poi.center.y) || !std::isfinite(poi.radius))
                    throw std::runtime_error("invalid site " + poi.id);
                result.push_back(std::move(poi));
            }
            pois = std::move(result);
            error.clear();
            return true;
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
            return false;
        }
    }

    const FPoi* FindPoi(std::span<const FPoi> pois, std::string_view name)
    {
        const auto found = std::find_if(pois.begin(), pois.end(), [name](const auto& poi) { return poi.name == name; });
        return found == pois.end() ? nullptr : &*found;
    }

    std::vector<const FPoi*> PoisAt(std::span<const FPoi> pois, const glm::vec2& position)
    {
        std::vector<const FPoi*> result;
        for (const auto& poi : pois)
            if (glm::distance(poi.center, position) <= poi.radius) result.push_back(&poi);
        return result;
    }
}
