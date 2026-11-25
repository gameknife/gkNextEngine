#include "Engine/Common/CoreMinimal.hpp"
#include "Campaign.hpp"
#include "Economy.hpp"

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

    bool FCampaign::Put(FItemInstance item)
    {
        const auto* def = FindItemDef(item.defId);
        if (!def || item.count <= 0) return false;
        item.containerId = 0;
        item.placement = {};
        // Plain units (no rounds loaded, no parts) top up stacks of the same item,
        // as the Master's AddToStorage does; the stack count is what the warehouse holds.
        const bool plain = item.loadedAmmo == 0 && item.attachments == FWeaponAttachments{};
        if (plain)
            for (auto& stack : storage)
            {
                if (item.count == 0) return true;
                if (stack.defId != item.defId || stack.loadedAmmo != 0 || stack.attachments != FWeaponAttachments{} ||
                    stack.count >= def->maxStack) continue;
                const int moved = std::min(item.count, def->maxStack - stack.count);
                stack.count += moved;
                item.count -= moved;
            }
        if (item.count == 0) return true;
        if (nextStorageId == std::numeric_limits<uint64_t>::max()) return false;
        item.instanceId = nextStorageId++;
        storage.push_back(std::move(item));
        return true;
    }

    bool FCampaign::Store(Inventory& inventory, FItemInstanceId id, int count)
    {
        if (inField || nextStorageId == std::numeric_limits<uint64_t>::max()) return false;
        if (const auto* item = inventory.FindInstance(id); !item || IsContractItem(item->defId)) return false;
        auto removed = inventory.TryRemove(id, count);
        if (!removed) return false;
        return Put(std::move(*removed));
    }

    bool FCampaign::Take(Inventory& inventory, FItemInstanceId id, int count, bool wear)
    {
        if (inField) return false;
        const auto found = std::find_if(storage.begin(), storage.end(), [id](const auto& item)
        { return item.instanceId == id; });
        if (found == storage.end() || count <= 0 || count > found->count) return false;
        const auto* def = FindItemDef(found->defId);
        if (!def) return false;
        Inventory transaction = inventory;
        FItemInstanceId newId = 0;
        if (!transaction.TryAdd(found->defId, std::string(def->displayName), def->kind, count, &newId,
                              wear ? kWornContainer : 0)) return false;
        if (def->kind == EItemKind::Weapon && !transaction.SetWeaponState(newId, found->loadedAmmo, found->attachments)) return false;
        inventory = std::move(transaction);
        found->count -= count;
        if (found->count == 0) storage.erase(found);
        return true;
    }

    void FCampaign::StoreAll(Inventory& inventory)
    {
        // Container cargo is flattened in storage; wearing the clothing again recreates its capacity.
        for (auto item : inventory.Instances())
        {
            // Contract parcels are settled before this; one left over has no contract.
            if (IsContractItem(item.defId)) continue;
            Put(std::move(item));
        }
        inventory.Clear();
    }

    bool FCampaign::Sell(FItemInstanceId id, int count, const FEconomyDefs& economy)
    {
        if (inField) return false;
        const auto found = std::find_if(storage.begin(), storage.end(), [id](const auto& item)
        { return item.instanceId == id; });
        if (found == storage.end() || count <= 0 || count > found->count) return false;
        const uint64_t price = UnitSellValue(*found, economy);
        if (price == 0 || price > std::numeric_limits<uint64_t>::max() / static_cast<uint64_t>(count)) return false;
        const uint64_t value = price * static_cast<uint64_t>(count);
        if (value > std::numeric_limits<uint64_t>::max() - gold) return false;
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
