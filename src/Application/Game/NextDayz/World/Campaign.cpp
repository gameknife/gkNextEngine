#include "Engine/Common/CoreMinimal.hpp"
#include "Campaign.hpp"

#include <limits>

namespace NextDayz
{
    const char* FlightPhaseName(EFlightPhase phase)
    {
        switch (phase)
        {
        case EFlightPhase::Inbound: return "Inbound";
        case EFlightPhase::Landing: return "Landing";
        case EFlightPhase::Waiting: return "Waiting at helipad";
        case EFlightPhase::Departing: return "Taking off";
        default: return "Idle";
        }
    }

    int SellPrice(std::string_view itemId, const FExtractionConfig& config)
    {
        const auto found = config.sellPrices.find(std::string(itemId));
        if (found != config.sellPrices.end()) return found->second;
        const auto* def = FindItemDef(itemId);
        if (!def) return 0;
        switch (def->kind)
        {
        case EItemKind::Weapon: return 100;
        case EItemKind::Ammo: return 1;
        case EItemKind::Clothing: return 20;
        case EItemKind::Melee: return 15;
        default: return 5;
        }
    }

    bool FCampaign::Store(Inventory& inventory, FItemInstanceId id, int count)
    {
        if (inField || nextStorageId == std::numeric_limits<uint64_t>::max()) return false;
        auto removed = inventory.TryRemove(id, count);
        if (!removed) return false;
        removed->instanceId = nextStorageId++;
        removed->containerId = 0;
        storage.push_back(*removed);
        return true;
    }

    bool FCampaign::Take(Inventory& inventory, FItemInstanceId id, int count, bool wear)
    {
        if (inField) return false;
        const auto found = std::find_if(storage.begin(), storage.end(), [id](const auto& item)
        { return item.instanceId == id; });
        if (found == storage.end() || count <= 0 || count > found->count) return false;
        const auto* def = FindItemDef(found->defId);
        if (!def) return false;
        FItemInstanceId newId = 0;
        if (!inventory.TryAdd(found->defId, std::string(def->displayName), def->kind, count, &newId,
                              wear ? kWornContainer : 0)) return false;
        if (def->kind == EItemKind::Weapon) inventory.SetLoadedAmmo(newId, found->loadedAmmo);
        found->count -= count;
        if (found->count == 0) storage.erase(found);
        return true;
    }

    void FCampaign::StoreAll(Inventory& inventory)
    {
        // Container cargo is flattened in storage; wearing the clothing again recreates its capacity.
        for (auto item : inventory.Instances())
        {
            item.instanceId = nextStorageId++;
            item.containerId = 0;
            storage.push_back(std::move(item));
        }
        inventory.Clear();
    }

    bool FCampaign::Sell(FItemInstanceId id, int count, const FExtractionConfig& config)
    {
        if (inField) return false;
        const auto found = std::find_if(storage.begin(), storage.end(), [id](const auto& item)
        { return item.instanceId == id; });
        if (found == storage.end() || count <= 0 || count > found->count) return false;
        const int price = SellPrice(found->defId, config);
        const uint64_t value = static_cast<uint64_t>(std::max(price, 0)) * static_cast<uint64_t>(count);
        if (price <= 0 || value > std::numeric_limits<uint64_t>::max() - gold) return false;
        gold += value;
        found->count -= count;
        if (found->count == 0) storage.erase(found);
        return true;
    }

    bool FCampaign::Call(Inventory& inventory, FItemInstanceId flareId, std::string pointId,
                         const glm::vec3& pad, const glm::vec3& player, const FExtractionConfig& config)
    {
        const auto* flare = inventory.FindInstance(flareId);
        if (!inField || flight.phase != EFlightPhase::Idle || pointId.empty() || !flare ||
            flare->defId != "signal_flare" || glm::distance(pad, player) > config.callRadius) return false;
        if (!inventory.TryConsumeInstance(flareId)) return false;
        flight = {EFlightPhase::Inbound, std::move(pointId), pad, config.arrivalSeconds, false};
        return true;
    }

    bool FCampaign::Board(const glm::vec3& player, const FExtractionConfig& config)
    {
        if (!inField || flight.phase != EFlightPhase::Waiting || flight.boarded ||
            glm::distance(player, flight.pad + glm::vec3(2.0f, 0.3f, 0.0f)) > config.doorRadius) return false;
        flight.boarded = true;
        flight.phase = EFlightPhase::Departing;
        flight.remaining = config.departureSeconds;
        return true;
    }

    bool FCampaign::TickFlight(float dt, const FExtractionConfig& config)
    {
        if (flight.phase == EFlightPhase::Idle || !inField) return false;
        flight.remaining -= std::max(dt, 0.0f);
        while (flight.phase != EFlightPhase::Idle && flight.remaining <= 0.0f)
        {
            switch (flight.phase)
            {
            case EFlightPhase::Inbound:
                flight.phase = EFlightPhase::Landing;
                flight.remaining += config.landingSeconds;
                break;
            case EFlightPhase::Landing:
                flight.phase = EFlightPhase::Waiting;
                flight.remaining += config.boardingSeconds;
                break;
            case EFlightPhase::Waiting:
                flight.phase = EFlightPhase::Departing;
                flight.remaining += config.departureSeconds;
                break;
            case EFlightPhase::Departing:
            {
                const bool success = flight.boarded;
                flight = {};
                if (success)
                {
                    inField = false;
                    ++extractions;
                }
                return success;
            }
            default: return false;
            }
        }
        return false;
    }
}
