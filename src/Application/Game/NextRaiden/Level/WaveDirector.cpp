#include "Application/Game/NextRaiden/Level/WaveDirector.hpp"

#include <algorithm>
#include <array>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Application/Game/NextRaiden/Level/PatternJson.hpp"
#include "Application/Game/NextRaiden/Player/ScrollCamera.hpp"
#include "Application/Game/NextRaiden/World/EnemyPool.hpp"
#include "Engine/Runtime/Utilities/JsonHelpers.hpp"

using json = nlohmann::json;

namespace NextRaiden
{
    namespace
    {
        /// Enemies enter this far beyond the top edge, so a formation is already in formation
        /// by the time it becomes visible.
        constexpr float kSpawnMargin = 8.0f;

        constexpr std::array<std::string_view, 16> kWaveFields = {
            "t",     "kind",  "count",     "formation", "x",        "spacing",  "path",    "speed",
            "amplitude", "period", "hoverDepth", "hoverSeconds", "fire", "drop", "dropAll", "_comment"};

        template <size_t N>
        bool CheckFields(const json& object, const std::array<std::string_view, N>& allowed, std::string_view context,
                         std::vector<std::string>& outErrors)
        {
            bool ok = true;
            for (const auto& entry : object.items())
            {
                const std::string& key = entry.key();
                if (std::find(allowed.begin(), allowed.end(), std::string_view(key)) == allowed.end())
                {
                    outErrors.push_back(fmt::format("{}: unknown field '{}'", context, key));
                    ok = false;
                }
            }
            return ok;
        }
    }

    bool ParseFormation(std::string_view name, EFormation& out)
    {
        if (name == "line") { out = EFormation::Line; return true; }
        if (name == "vee") { out = EFormation::Vee; return true; }
        if (name == "column") { out = EFormation::Column; return true; }
        if (name == "arc") { out = EFormation::Arc; return true; }
        return false;
    }

    void BuildFormation(EFormation formation, int count, float spacing, std::vector<glm::vec2>& out)
    {
        out.clear();
        count = std::max(1, count);
        out.reserve(static_cast<size_t>(count));

        // x is across the screen, y is depth (negative = further ahead, i.e. higher up).
        for (int i = 0; i < count; ++i)
        {
            const float centred = static_cast<float>(i) - static_cast<float>(count - 1) * 0.5f;
            switch (formation)
            {
            case EFormation::Line:
                out.emplace_back(centred * spacing, 0.0f);
                break;
            case EFormation::Vee:
                // Point leading, wings trailing: the leader is the one the player meets first.
                out.emplace_back(centred * spacing, std::abs(centred) * spacing * 0.7f);
                break;
            case EFormation::Column:
                out.emplace_back(0.0f, static_cast<float>(i) * spacing);
                break;
            case EFormation::Arc:
                out.emplace_back(centred * spacing, centred * centred * spacing * 0.22f);
                break;
            case EFormation::Count:
            default:
                out.emplace_back(centred * spacing, 0.0f);
                break;
            }
        }
    }

    bool FWaveDirector::Load(const std::string& path, std::vector<std::string>& outErrors)
    {
        events_.clear();
        cursor_ = 0;

        json document;
        if (!NextJson::TryLoadFile(path, document) || !document.is_object() || !document.contains("waves"))
        {
            outErrors.push_back(fmt::format("{}: missing or has no 'waves' array", path));
            return false;
        }

        const json& waves = document.at("waves");
        if (!waves.is_array())
        {
            outErrors.push_back(fmt::format("{}: 'waves' is not an array", path));
            return false;
        }

        int index = 0;
        for (const json& entry : waves)
        {
            const std::string context = fmt::format("wave[{}]", index++);
            if (!entry.is_object())
            {
                outErrors.push_back(fmt::format("{}: not an object", context));
                continue;
            }
            if (!CheckFields(entry, kWaveFields, context, outErrors))
            {
                continue;
            }

            FWaveEvent event;
            event.Time = entry.value("t", 0.0f);

            if (entry.contains("kind"))
            {
                const std::string name = entry.at("kind").get<std::string>();
                if (!ParseEnemyKind(name, event.Kind))
                {
                    outErrors.push_back(fmt::format("{}: unknown kind '{}'", context, name));
                    continue;
                }
            }
            if (entry.contains("path"))
            {
                const std::string name = entry.at("path").get<std::string>();
                if (!ParsePathKind(name, event.Path))
                {
                    outErrors.push_back(fmt::format("{}: unknown path '{}'", context, name));
                    continue;
                }
            }
            if (entry.contains("formation"))
            {
                const std::string name = entry.at("formation").get<std::string>();
                if (!ParseFormation(name, event.Formation))
                {
                    outErrors.push_back(fmt::format("{}: unknown formation '{}'", context, name));
                    continue;
                }
            }
            if (entry.contains("drop"))
            {
                const std::string name = entry.at("drop").get<std::string>();
                if (!ParseItemKind(name, event.Drop))
                {
                    outErrors.push_back(fmt::format("{}: unknown drop '{}'", context, name));
                    continue;
                }
            }

            event.Count = std::max(1, entry.value("count", 1));
            event.X = entry.value("x", 0.0f);
            event.Spacing = entry.value("spacing", 4.0f);
            event.Speed = entry.value("speed", 0.0f);
            event.PathAmplitude = entry.value("amplitude", event.PathAmplitude);
            event.PathPeriod = entry.value("period", event.PathPeriod);
            event.HoverDepth = entry.value("hoverDepth", event.HoverDepth);
            event.HoverSeconds = entry.value("hoverSeconds", event.HoverSeconds);
            event.DropOnLeaderOnly = !entry.value("dropAll", false);

            if (entry.contains("fire"))
            {
                if (!entry.at("fire").is_object())
                {
                    outErrors.push_back(fmt::format("{}: 'fire' is not an object", context));
                    continue;
                }
                if (!ReadPatternSpec(entry.at("fire"), fmt::format("{}.fire", context), event.Fire, outErrors))
                {
                    continue;
                }
            }
            else
            {
                event.CanFire = false;
            }

            events_.push_back(event);
        }

        std::stable_sort(events_.begin(), events_.end(),
                         [](const FWaveEvent& a, const FWaveEvent& b) { return a.Time < b.Time; });
        return !events_.empty();
    }

    void FWaveDirector::LoadEvents(std::vector<FWaveEvent> events)
    {
        events_ = std::move(events);
        std::stable_sort(events_.begin(), events_.end(),
                         [](const FWaveEvent& a, const FWaveEvent& b) { return a.Time < b.Time; });
        cursor_ = 0;
    }

    void FWaveDirector::Reset()
    {
        cursor_ = 0;
    }

    void FWaveDirector::SkipTo(float stageTime)
    {
        while (cursor_ < events_.size() && events_[cursor_].Time <= stageTime)
        {
            ++cursor_;
        }
    }

    void FWaveDirector::Update(float stageTime, const FScrollCamera& camera, FEnemyPool& enemies)
    {
        std::vector<glm::vec2> offsets;
        while (cursor_ < events_.size() && events_[cursor_].Time <= stageTime)
        {
            const FWaveEvent& event = events_[cursor_++];
            BuildFormation(event.Formation, event.Count, event.Spacing, offsets);

            const float entryZ = camera.FlightFarZ() - kSpawnMargin;
            for (size_t i = 0; i < offsets.size(); ++i)
            {
                FEnemySpawn spawn;
                spawn.Kind = event.Kind;
                spawn.Path = event.Path;
                // Formation depth offsets push members *back* up the screen, so the leader
                // enters first and the rest follow it in.
                spawn.Position = glm::vec3(event.X + offsets[i].x, camera.FlightAltitude(), entryZ - offsets[i].y);
                spawn.Speed = event.Speed;
                spawn.PathAmplitude = event.PathAmplitude;
                spawn.PathPeriod = event.PathPeriod;
                spawn.HoverDepth = event.HoverDepth;
                spawn.HoverSeconds = event.HoverSeconds;
                spawn.Fire = event.Fire;
                spawn.CanFire = event.CanFire;
                spawn.Drop = (event.DropOnLeaderOnly && i != 0) ? EItemKind::None : event.Drop;
                if (!enemies.Spawn(spawn))
                {
                    // Pool exhausted. Not fatal, but it means the stage is asking for more
                    // aircraft than the rack was sized for, which is a tuning bug worth seeing.
                    SPDLOG_WARN("[NextRaiden] wave at t={:.1f}: {} pool is full, dropped one spawn", event.Time,
                                EnemyKindName(event.Kind));
                    break;
                }
            }
        }
    }
}
