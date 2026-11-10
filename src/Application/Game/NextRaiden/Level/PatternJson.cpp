#include "Application/Game/NextRaiden/Level/PatternJson.hpp"

#include <algorithm>
#include <array>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace NextRaiden
{
    namespace
    {
        constexpr std::array<std::string_view, 14> kFireFields = {
            "pattern",  "count",    "arcDegrees", "spinDegrees", "rateDegreesPerSec", "sweepDegrees", "burst",
            "burstGap", "interval", "bullet",     "speed",       "lead",              "damage",       "_comment"};
    }

    bool ReadPatternSpec(const json& object, std::string_view context, FPatternSpec& spec,
                         std::vector<std::string>& outErrors)
    {
        if (!object.is_object())
        {
            outErrors.push_back(fmt::format("{}: not an object", context));
            return false;
        }

        bool ok = true;
        for (const auto& entry : object.items())
        {
            if (std::find(kFireFields.begin(), kFireFields.end(), std::string_view(entry.key())) == kFireFields.end())
            {
                outErrors.push_back(fmt::format("{}: unknown field '{}'", context, entry.key()));
                ok = false;
            }
        }
        if (!ok)
        {
            return false;
        }

        if (object.contains("pattern"))
        {
            const std::string name = object.at("pattern").get<std::string>();
            if (!ParsePatternKind(name, spec.Kind))
            {
                outErrors.push_back(fmt::format("{}: unknown pattern '{}'", context, name));
                return false;
            }
        }
        if (object.contains("bullet"))
        {
            const std::string name = object.at("bullet").get<std::string>();
            if (!ParseBulletVisual(name, spec.Visual))
            {
                outErrors.push_back(fmt::format("{}: unknown bullet '{}'", context, name));
                return false;
            }
        }

        spec.Count = object.value("count", spec.Count);
        spec.ArcDegrees = object.value("arcDegrees", spec.ArcDegrees);
        spec.SpinDegrees = object.value("spinDegrees", spec.SpinDegrees);
        spec.RateDegreesPerSec = object.value("rateDegreesPerSec", spec.RateDegreesPerSec);
        spec.SweepDegrees = object.value("sweepDegrees", spec.SweepDegrees);
        spec.Burst = object.value("burst", spec.Burst);
        spec.BurstGapSeconds = object.value("burstGap", spec.BurstGapSeconds);
        spec.IntervalSeconds = object.value("interval", spec.IntervalSeconds);
        spec.Speed = object.value("speed", spec.Speed);
        spec.LeadSeconds = object.value("lead", spec.LeadSeconds);
        spec.Damage = object.value("damage", spec.Damage);
        return true;
    }
}
