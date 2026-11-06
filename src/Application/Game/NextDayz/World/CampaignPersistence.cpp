#include "Engine/Common/CoreMinimal.hpp"
#include "Campaign.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

#include <cmath>
#include <nlohmann/json.hpp>

namespace glm
{
    inline void to_json(nlohmann::json& json, const vec3& value)
    {
        json = {value.x, value.y, value.z};
    }
    inline void from_json(const nlohmann::json& json, vec3& value)
    {
        if (!json.is_array() || json.size() != 3) throw std::runtime_error("Invalid position");
        value = {json.at(0).get<float>(), json.at(1).get<float>(), json.at(2).get<float>()};
        if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
            throw std::runtime_error("Non-finite position");
    }
}

namespace NextDayz
{
    namespace
    {
        FWeaponAttachments ReadAttachments(const nlohmann::json& json)
        {
            FWeaponAttachments result{};
            if (!json.contains("attachments")) return result; // v1..v3
            const auto& parts = json.at("attachments");
            if (!parts.is_array() || parts.size() != result.size()) throw std::runtime_error("Invalid attachment slots");
            for (size_t slot = 0; slot < result.size(); ++slot)
            {
                if (!parts[slot].is_number_integer() || parts[slot].get<int64_t>() < 0 ||
                    parts[slot].get<uint64_t>() >= static_cast<uint64_t>(EWeaponAttachment::Count))
                    throw std::runtime_error("Invalid attachment type");
                result[slot] = static_cast<EWeaponAttachment>(parts[slot].get<int>());
            }
            return result;
        }
    }
    void to_json(nlohmann::json& json, const FItemInstance& item)
    {
        json = {{"instanceId", item.instanceId}, {"defId", item.defId}, {"count", item.count},
                {"containerId", item.containerId}, {"loadedAmmo", item.loadedAmmo}, {"attachments", item.attachments}};
    }
    void from_json(const nlohmann::json& json, FItemInstance& item)
    {
        json.at("instanceId").get_to(item.instanceId);
        json.at("defId").get_to(item.defId);
        json.at("count").get_to(item.count);
        json.at("containerId").get_to(item.containerId);
        json.at("loadedAmmo").get_to(item.loadedAmmo);
        item.attachments = ReadAttachments(json);
    }
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FInventoryContainer, containerId, ownerItemInstanceId, displayName,
                                     capacity, active, weaponOnly)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FInventoryState, instances, containers, nextInstanceId, nextContainerId)
    void to_json(nlohmann::json& json, const FLootStack& item)
    {
        json = {{"itemId", item.itemId}, {"count", item.count}, {"loadedAmmo", item.loadedAmmo}, {"attachments", item.attachments}};
    }
    void from_json(const nlohmann::json& json, FLootStack& item)
    {
        json.at("itemId").get_to(item.itemId);
        json.at("count").get_to(item.count);
        json.at("loadedAmmo").get_to(item.loadedAmmo);
        item.attachments = ReadAttachments(json);
    }
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FLootSlot, worldPos, profile, category, state, committedAt, cooldownUntil,
                                     offscreenSince, roll)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FSavedLootEntry, id, source, position, yaw, contents, state, slot)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FLootWorldState, seconds, directorRng, rollRng, pileSerial, entries)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FFlightState, phase, pointId, pad, remaining, boarded)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FCampaign, inField, gold, sorties, extractions, nextStorageId, storage, flight,
                                     mapSorties, lastMapId, lastOutcome, sortieId)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FSurvivalSnapshot, health, hunger, hydration, bleedingWounds, cold,
                                     painkillerSeconds, lifeState, damageSequence, woundSequence, lastDamageSource)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TimeSystem::FState, minutes, nextWeatherChange, weatherRng, weather, blend)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FMapWorldSave, world, time, version)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FCampaignSave, map, campaign, inventory, world, survival, time, position,
                                     yaw, pitch, firstPerson, weaponSlots, activeSlot, previousSlot, survivalSeconds, sessionRng,
                                     survivalRng, quickSlots, lanternLit, lanternSeconds, hasCacheMarker, cacheMarker,
                                     cacheSeconds, maps, mapVersion)

    namespace
    {
        bool Valid(const FCampaignSave& saved)
        {
            Inventory inventory;
            if (saved.map.empty() || !inventory.Restore(saved.inventory) || saved.activeSlot < 0 ||
                saved.activeSlot >= 3 || saved.previousSlot < -1 || saved.previousSlot >= 3 ||
                saved.campaign.nextStorageId == 0 ||
                !std::isfinite(saved.world.seconds) || saved.world.seconds < 0 ||
                !std::isfinite(saved.yaw) || !std::isfinite(saved.pitch) ||
                !std::isfinite(saved.survivalSeconds) || saved.survivalSeconds < 0 ||
                !std::isfinite(saved.time.minutes) || !std::isfinite(saved.time.nextWeatherChange) ||
                saved.time.weather > EWeather::Fog || !std::isfinite(saved.time.blend)) return false;
            std::unordered_set<FItemInstanceId> equipped;
            for (int slot = 0; slot < kWeaponSlotCount; ++slot)
            {
                const FItemInstanceId id = saved.weaponSlots[slot];
                if (!id) continue;
                const auto* instance = inventory.FindInstance(id);
                const auto* item = instance ? FindItemDef(instance->defId) : nullptr;
                const auto* weapon = item ? FindWeaponDef(item->weaponDefId) : nullptr;
                if (!weapon || !equipped.insert(id).second || (slot == kSidearmSlot && !weapon->IsSidearm())) return false;
            }
            const auto& body = saved.survival;
            if (!std::isfinite(body.health) || !std::isfinite(body.hunger) || !std::isfinite(body.hydration) ||
                !std::isfinite(body.cold) || !std::isfinite(body.painkillerSeconds) ||
                body.health < 0 || body.health > 100 || body.hunger < 0 || body.hunger > 100 ||
                body.hydration < 0 || body.hydration > 100 || body.bleedingWounds < 0 ||
                body.bleedingWounds > 20 || body.lifeState > EPlayerLifeState::Dead) return false;
            std::unordered_set<uint64_t> ids;
            for (const auto& item : saved.campaign.storage)
            {
                const auto* def = FindItemDef(item.defId);
                if (!def || item.instanceId == 0 || item.instanceId >= saved.campaign.nextStorageId ||
                    !ids.insert(item.instanceId).second || item.count <= 0 || item.count > def->maxStack ||
                    item.loadedAmmo < 0 || (item.loadedAmmo != 0 && def->kind != EItemKind::Weapon) ||
                    !ValidAttachments(item.defId, item.attachments) ||
                    (def->kind == EItemKind::Weapon && item.loadedAmmo > WeaponMagazineCapacity(item.defId, item.attachments))) return false;
            }
            const auto& flight = saved.campaign.flight;
            if (flight.phase > EFlightPhase::Departing || !std::isfinite(flight.remaining) || flight.remaining < 0 ||
                (flight.phase != EFlightPhase::Idle && (flight.pointId.empty() || !saved.campaign.inField)) ||
                (flight.boarded && flight.phase != EFlightPhase::Departing)) return false;
            std::unordered_set<std::string> keys;
            for (const auto& entry : saved.world.entries)
            {
                if (entry.id.empty() || !keys.insert(entry.id).second || entry.source > ELootSource::Pile ||
                    entry.state > FLootEntry::EState::Empty || entry.slot.category >= ELootCategory::Count ||
                    entry.slot.profile >= ELootProfile::Count || entry.slot.state > ELootSlotState::Cooldown ||
                    !std::isfinite(entry.slot.committedAt) || !std::isfinite(entry.slot.cooldownUntil) ||
                    !std::isfinite(entry.slot.offscreenSince) || !std::isfinite(entry.yaw)) return false;
                for (const auto& stack : entry.contents)
                {
                    const auto* def = FindItemDef(stack.itemId);
                    if (!def || stack.count <= 0 || stack.count > 10000 || stack.loadedAmmo < 0 ||
                        (stack.loadedAmmo != 0 && def->kind != EItemKind::Weapon) ||
                        !ValidAttachments(stack.itemId, stack.attachments) ||
                        (def->kind == EItemKind::Weapon && stack.loadedAmmo > WeaponMagazineCapacity(stack.itemId, stack.attachments))) return false;
                }
            }
            if (saved.mapVersion == 0 || saved.maps.size() > 1024) return false;
            for (const auto& [id, state] : saved.maps)
            {
                if (id.empty() || state.version == 0) return false;
                // Reuse the same full validation for inactive maps as for the loaded one.
                auto mapSnapshot = saved;
                mapSnapshot.maps.clear();
                mapSnapshot.world = state.world;
                mapSnapshot.time = state.time;
                if (!Valid(mapSnapshot)) return false;
            }
            uint64_t counted = 0;
            for (const auto& [id, count] : saved.campaign.mapSorties)
            {
                if (id.empty() || count > saved.campaign.sorties - std::min(counted, saved.campaign.sorties)) return false;
                counted += count;
            }
            return true;
        }

        bool ReadOne(const std::filesystem::path& path, FCampaignSave& save, std::string& error)
        {
            try
            {
                if (std::filesystem::file_size(path) > 64 * 1024 * 1024) throw std::runtime_error("Save too large");
                std::ifstream stream(path, std::ios::binary);
                auto document = nlohmann::json::parse(stream);
                const int version = document.at("version").get<int>();
                if (version < 1 || version > 4) throw std::runtime_error("Unsupported save version");
                if (version == 1)
                {
                    auto& state = document.at("state");
                    state["maps"] = nlohmann::json::object();
                    state["mapVersion"] = 1;
                    auto& campaign = state.at("campaign");
                    campaign["mapSorties"] = nlohmann::json::object();
                    campaign["lastMapId"] = "";
                    campaign["lastOutcome"] = "";
                    campaign["sortieId"] = campaign.at("sorties");
                }
                if (version < 3)
                {
                    auto& state = document.at("state");
                    auto& slots = state.at("weaponSlots");
                    if (!slots.is_array() || slots.size() != 2) throw std::runtime_error("Invalid legacy weapon slots");
                    slots.push_back(0);
                    state["previousSlot"] = -1;
                }
                auto candidate = document.at("state").get<FCampaignSave>();
                if (!Valid(candidate)) throw std::runtime_error("Invalid campaign state");
                save = std::move(candidate);
                return true;
            }
            catch (const std::exception& ex) { error = ex.what(); return false; }
        }
    }

    bool ReadCampaignSave(const std::filesystem::path& path, FCampaignSave& save, std::string& error)
    {
        error.clear();
        if (ReadOne(path, save, error)) return true;
        const std::string primaryError = error;
        if (ReadOne(path.string() + ".bak", save, error))
        {
            error = "Recovered backup: " + primaryError;
            return true;
        }
        error = primaryError;
        return false;
    }

    bool WriteCampaignSave(const std::filesystem::path& path, const FCampaignSave& save, std::string& error)
    {
        error.clear();
        const std::filesystem::path temporary = path.string() + ".tmp";
        const std::filesystem::path backup = path.string() + ".bak";
        bool moved = false;
        try
        {
            if (!Valid(save)) throw std::runtime_error("Refusing invalid campaign snapshot");
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
            {
                std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                stream.exceptions(std::ios::failbit | std::ios::badbit);
                stream << nlohmann::json{{"version", 4}, {"state", save}}.dump(2);
                stream.flush();
                stream.close();
            }
            if (std::filesystem::exists(path))
            {
                std::filesystem::remove(backup);
                std::filesystem::rename(path, backup);
                moved = true;
            }
            std::filesystem::rename(temporary, path);
            return true;
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
            std::error_code ignored;
            if (moved && !std::filesystem::exists(path, ignored)) std::filesystem::rename(backup, path, ignored);
            return false;
        }
    }

    bool LoadExtractionConfig(const std::filesystem::path& path, FExtractionConfig& config, std::string& error)
    {
        try
        {
            std::ifstream stream(path);
            const auto document = nlohmann::json::parse(stream);
            FExtractionConfig candidate;
            candidate.arrivalSeconds = document.at("arrivalSeconds").get<float>();
            candidate.landingSeconds = document.at("landingSeconds").get<float>();
            candidate.boardingSeconds = document.at("boardingSeconds").get<float>();
            candidate.departureSeconds = document.at("departureSeconds").get<float>();
            candidate.callRadius = document.at("callRadius").get<float>();
            candidate.doorRadius = document.at("doorRadius").get<float>();
            candidate.autosaveSeconds = document.at("autosaveSeconds").get<float>();
            candidate.sellPrices = document.at("sellPrices").get<std::unordered_map<std::string, int>>();
            for (const float value : {candidate.arrivalSeconds, candidate.landingSeconds, candidate.boardingSeconds,
                 candidate.departureSeconds, candidate.callRadius, candidate.doorRadius, candidate.autosaveSeconds})
                if (!std::isfinite(value) || value < 0.1f || value > 3600.0f)
                    throw std::runtime_error("Extraction tuning out of range");
            for (const auto& [id, value] : candidate.sellPrices)
                if (!FindItemDef(id) || value <= 0 || value > 1000000) throw std::runtime_error("Invalid sell price");
            config = std::move(candidate);
            return true;
        }
        catch (const std::exception& ex) { error = ex.what(); return false; }
    }
}
