#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/Net/MasterApi.hpp"

#include <nlohmann/json.hpp>

namespace NextDayz::Master
{
    using nlohmann::json;

    namespace
    {
        json StacksToJson(const std::vector<FLootStack>& stacks)
        {
            json array = json::array();
            for (const FLootStack& stack : stacks)
            {
                json item{{"id", stack.itemId}, {"count", stack.count}};
                if (stack.loadedAmmo != 0) item["loaded"] = stack.loadedAmmo;
                bool fitted = false;
                json attachments = json::array();
                for (const EWeaponAttachment attachment : stack.attachments)
                {
                    attachments.push_back(static_cast<int>(attachment));
                    fitted = fitted || attachment != EWeaponAttachment{};
                }
                if (fitted) item["attachments"] = attachments;
                array.push_back(std::move(item));
            }
            return array;
        }

        std::vector<FLootStack> StacksFromJson(const json& array)
        {
            std::vector<FLootStack> stacks;
            if (!array.is_array()) return stacks;
            for (const json& item : array)
            {
                FLootStack stack;
                stack.itemId = item.value("id", std::string());
                stack.count = item.value("count", 0);
                stack.loadedAmmo = item.value("loaded", 0);
                if (const auto it = item.find("attachments"); it != item.end() && it->is_array())
                {
                    for (size_t slot = 0; slot < it->size() && slot < stack.attachments.size(); ++slot)
                        stack.attachments[slot] = static_cast<EWeaponAttachment>((*it)[slot].get<int>());
                }
                if (!stack.itemId.empty() && stack.count > 0) stacks.push_back(std::move(stack));
            }
            return stacks;
        }

        glm::vec3 Vec3(const json& array)
        {
            if (!array.is_array() || array.size() < 3) return glm::vec3(0.0f);
            return {array[0].get<float>(), array[1].get<float>(), array[2].get<float>()};
        }

        FCharacter CharacterFromJson(const json& value)
        {
            FCharacter character;
            character.map = value.value("map", std::string());
            if (const auto it = value.find("position"); it != value.end()) character.position = Vec3(*it);
            character.yaw = value.value("yaw", 0.0f);
            if (const auto it = value.find("survival"); it != value.end() && !it->is_null()) character.survivalJson = it->dump();
            if (const auto it = value.find("inventory"); it != value.end()) character.inventory = StacksFromJson(*it);
            character.sortie = value.value("sortie", uint64_t(0));
            character.fresh = value.value("fresh", true);
            return character;
        }

        const char* OutcomeName(EOutcome outcome)
        {
            switch (outcome)
            {
            case EOutcome::Left: return "left";
            case EOutcome::Dead: return "dead";
            case EOutcome::Extracted: return "extracted";
            case EOutcome::Field: break;
            }
            return "field";
        }

        template <class TParse>
        bool Parse(std::string_view text, TParse&& parse)
        {
            try
            {
                parse(json::parse(text));
                return true;
            }
            catch (const std::exception&)
            {
                return false;
            }
        }
    }

    std::string LoginRequest(std::string_view name)
    {
        return json{{"name", name}}.dump();
    }

    std::string DeployRequest(std::string_view mapId, const std::vector<FBring>& bring)
    {
        json items = json::array();
        for (const FBring& b : bring) items.push_back({{"index", b.index}, {"count", b.count}});
        return json{{"map", mapId}, {"bring", items}}.dump();
    }

    std::string RegisterRequest(std::string_view dsId, std::string_view key, uint16_t port, std::string_view map)
    {
        return json{{"id", dsId}, {"key", key}, {"port", port}, {"map", map}}.dump();
    }

    std::string HeartbeatRequest(std::string_view dsId, std::string_view key, const std::vector<uint64_t>& players,
                                 bool ready)
    {
        return json{{"id", dsId}, {"key", key}, {"players", players}, {"ready", ready}}.dump();
    }

    std::string DsRequest(std::string_view dsId, std::string_view key)
    {
        return json{{"id", dsId}, {"key", key}}.dump();
    }

    std::string RedeemRequest(std::string_view dsId, std::string_view key, std::string_view ticket)
    {
        return json{{"id", dsId}, {"key", key}, {"ticket", ticket}}.dump();
    }

    std::string CheckpointRequest(std::string_view dsId, std::string_view key, uint64_t seq,
                                  std::string_view worldJson, const std::vector<FCheckpointCharacter>& characters)
    {
        json array = json::array();
        for (const FCheckpointCharacter& c : characters)
        {
            json item{{"playerId", c.playerId}, {"sortie", c.sortie}, {"outcome", OutcomeName(c.outcome)},
                      {"position", {c.position.x, c.position.y, c.position.z}}, {"yaw", c.yaw},
                      {"inventory", StacksToJson(c.inventory)}};
            item["survival"] = c.survivalJson.empty() ? json() : json::parse(c.survivalJson, nullptr, false);
            array.push_back(std::move(item));
        }
        json body{{"id", dsId}, {"key", key}, {"seq", seq}, {"characters", array}};
        body["world"] = worldJson.empty() ? json() : json::parse(worldJson, nullptr, false);
        return body.dump();
    }

    bool ParseLogin(std::string_view text, uint64_t& playerId, std::string& token)
    {
        return Parse(text, [&](const json& value)
        {
            playerId = value.at("playerId").get<uint64_t>();
            token = value.at("token").get<std::string>();
        });
    }

    bool ParseProfile(std::string_view text, FProfile& out)
    {
        return Parse(text, [&](const json& value)
        {
            out = {};
            out.playerId = value.at("playerId").get<uint64_t>();
            out.name = value.value("name", std::string());
            out.gold = value.value("gold", int64_t(0));
            out.storage = StacksFromJson(value.value("storage", json::array()));
            if (const auto it = value.find("character"); it != value.end() && it->is_object())
                out.character = CharacterFromJson(*it);
            out.sorties = value.value("sorties", uint64_t(0));
            out.extractions = value.value("extractions", uint64_t(0));
            out.deaths = value.value("deaths", uint64_t(0));
            out.lastOutcome = value.value("lastOutcome", std::string());
            out.playing = value.value("playing", std::string());
            for (const json& map : value.value("maps", json::array()))
            {
                out.maps.push_back({map.value("id", std::string()), map.value("name", std::string()),
                                    map.value("scene", std::string()), map.value("training", false)});
            }
        });
    }

    bool ParseDeploy(std::string_view text, FDeploy& out)
    {
        return Parse(text, [&](const json& value)
        {
            out.state = value.at("state").get<std::string>();
            out.server = value.value("server", std::string());
            out.host = value.value("host", std::string());
            out.port = value.value("port", uint16_t(0));
            out.ticket = value.value("ticket", std::string());
            out.map = value.value("map", std::string());
            out.scene = value.value("scene", std::string());
            out.resume = value.value("resume", false);
        });
    }

    bool ParseRedeem(std::string_view text, FRedeem& out)
    {
        return Parse(text, [&](const json& value)
        {
            out.playerId = value.at("playerId").get<uint64_t>();
            out.name = value.value("name", std::string());
            out.sortie = value.at("sortie").get<uint64_t>();
            out.character = CharacterFromJson(value.at("character"));
        });
    }

    bool ParseHeartbeat(std::string_view text, FHeartbeatReply& out)
    {
        return Parse(text, [&](const json& value)
        {
            out.stop = value.value("stop", false);
            out.pvp = value.value("pvp", false);
            out.kick = value.value("kick", std::vector<uint64_t>{});
        });
    }

    bool ParseWorld(std::string_view text, std::string& worldJson)
    {
        return Parse(text, [&](const json& value)
        {
            const auto it = value.find("blob");
            worldJson = it == value.end() || it->is_null() ? std::string() : it->dump();
        });
    }
}
