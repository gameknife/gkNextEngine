#include "Engine/Common/CoreMinimal.hpp"
#include "Contracts.hpp"
#include "Economy.hpp"
#include "Application/Game/NextDayz/Data/DeterministicRng.hpp"

#include <limits>

namespace NextDayz
{
    namespace
    {
        uint64_t Roll(FDeterministicRng& rng, uint64_t min, uint64_t max)
        {
            const uint64_t span = max - min + 1;
            return min + (span > std::numeric_limits<uint32_t>::max() ? rng.NextU32() : rng.Range(static_cast<uint32_t>(span)));
        }

        bool UsesTemplate(const std::vector<FContract>& contracts, std::string_view id)
        {
            return std::any_of(contracts.begin(), contracts.end(), [id](const auto& contract) { return contract.templateId == id; });
        }

        bool UsesItem(const std::vector<FContract>& contracts, std::string_view item)
        {
            return std::any_of(contracts.begin(), contracts.end(), [item](const auto& contract)
            {
                return std::any_of(contract.items.begin(), contract.items.end(), [item](const auto& entry) { return entry.item == item; });
            });
        }

        bool UsesParcel(const std::vector<FContract>& contracts, std::string_view parcel)
        {
            return std::any_of(contracts.begin(), contracts.end(), [parcel](const auto& contract) { return contract.parcel == parcel; });
        }

        std::vector<const FPoi*> Candidates(const FContractTemplate& entry, const std::vector<FPoi>& pois)
        {
            std::vector<const FPoi*> result;
            for (const auto& poi : pois)
            {
                const bool kind = entry.poiKinds.empty() ||
                    std::find(entry.poiKinds.begin(), entry.poiKinds.end(), poi.kind) != entry.poiKinds.end();
                if (kind && poi.risk >= entry.riskMin && poi.risk <= entry.riskMax) result.push_back(&poi);
            }
            return result;
        }

        // Maps where the template finds enough sites, in map-id order.
        std::vector<std::pair<const std::string*, std::vector<const FPoi*>>> Placements(const FContractTemplate& entry,
                                                                                       const FPoiIndex& pois)
        {
            std::vector<std::pair<const std::string*, std::vector<const FPoi*>>> result;
            for (const auto& [mapId, sites] : pois)
            {
                auto candidates = Candidates(entry, sites);
                if (candidates.size() >= entry.poiCount) result.emplace_back(&mapId, std::move(candidates));
            }
            return result;
        }

        bool Eligible(const FCampaign& campaign, const FEconomyDefs& defs, const FContractTemplate& entry,
                      const FPoiIndex& pois, bool fieldOnly)
        {
            if (entry.tier > AllowedContractTier(campaign, defs)) return false;
            if (fieldOnly && !IsFieldContract(entry.type)) return false;
            if (UsesTemplate(campaign.board, entry.id) || UsesTemplate(campaign.contracts, entry.id)) return false;
            if (entry.type == EContractType::Deliver)
                return std::none_of(entry.items.begin(), entry.items.end(), [&](const auto& item)
                { return UsesItem(campaign.board, item.item); });
            if (!entry.parcel.empty() && (UsesParcel(campaign.board, entry.parcel) || UsesParcel(campaign.contracts, entry.parcel)))
                return false;
            return !Placements(entry, pois).empty();
        }

        FContract RollContract(FCampaign& campaign, const FEconomyDefs& defs, const FContractTemplate& entry,
                               const FPoiIndex& pois, FDeterministicRng& rng)
        {
            FContract contract;
            contract.id = ++campaign.contractSerial;
            contract.templateId = entry.id;
            contract.type = entry.type;
            contract.tier = entry.tier;
            contract.offeredAt = FinishedSorties(campaign);
            if (entry.type == EContractType::Deliver)
            {
                double value = 0.0;
                for (const auto& range : entry.items)
                {
                    const int count = static_cast<int>(Roll(rng, static_cast<uint64_t>(range.min), static_cast<uint64_t>(range.max)));
                    contract.items.push_back({range.item, count});
                    value += static_cast<double>(SellPrice(range.item, defs)) * count;
                }
                contract.reward = static_cast<uint64_t>(std::llround(value * defs.board.deliverMultiplier)) + entry.bonus;
                return contract;
            }
            auto placements = Placements(entry, pois);
            auto& [mapId, candidates] = placements[rng.Range(static_cast<uint32_t>(placements.size()))];
            contract.mapId = *mapId;
            for (uint32_t pick = 0; pick < entry.poiCount; ++pick)
            {
                const uint32_t index = pick + rng.Range(static_cast<uint32_t>(candidates.size()) - pick);
                std::swap(candidates[pick], candidates[index]);
                contract.targets.push_back(candidates[pick]->name);
            }
            contract.parcel = entry.parcel;
            contract.reward = Roll(rng, entry.rewardMin, entry.rewardMax);
            if (!entry.itemRewards.empty())
                contract.itemReward = entry.itemRewards[rng.Range(static_cast<uint32_t>(entry.itemRewards.size()))];
            return contract;
        }

        uint32_t ReputationGain(const FEconomyDefs& defs, uint32_t tier)
        {
            return tier >= 1 && tier <= defs.reputationGain.size() ? defs.reputationGain[tier - 1] : 0;
        }

        // Loses reputation but never a tier (design §3.1).
        int64_t Penalize(FCampaign& campaign, const FEconomyDefs& defs)
        {
            const uint32_t floor = defs.reputationTiers.empty() ? 0
                : defs.reputationTiers[std::min<size_t>(ReputationTier(campaign.reputation, defs), defs.reputationTiers.size() - 1)];
            const uint32_t before = campaign.reputation;
            campaign.reputation = std::max(floor, campaign.reputation - std::min(campaign.reputation, defs.reputationPenalty));
            return static_cast<int64_t>(campaign.reputation) - static_cast<int64_t>(before);
        }

        void Complete(FCampaign& campaign, const FEconomyDefs& defs, const FContract& contract, FContractReport& report)
        {
            campaign.gold += std::min(contract.reward, std::numeric_limits<uint64_t>::max() - campaign.gold);
            const uint32_t gain = ReputationGain(defs, contract.tier);
            campaign.reputation += std::min(gain, std::numeric_limits<uint32_t>::max() - campaign.reputation);
            if (!contract.itemReward.empty() && AddToStorage(campaign, contract.itemReward, 1))
                report.itemRewards.push_back(contract.itemReward);
            campaign.sortie.contractGold += contract.reward;
            ++campaign.sortie.contractsCompleted;
            ++campaign.contractsCompleted;
            report.completed.push_back(contract.templateId);
            report.gold += contract.reward;
            report.reputation += gain;
        }

        // Removes `count` units of `item` from storage, plain stacks first.
        void TakeFromStorage(FCampaign& campaign, std::string_view item, int count)
        {
            for (const bool plainPass : {true, false})
                for (auto& stack : campaign.storage)
                {
                    if (count == 0) break;
                    const bool plain = stack.loadedAmmo == 0 && stack.attachments == FWeaponAttachments{};
                    if (stack.defId != item || plain != plainPass) continue;
                    const int taken = std::min(count, stack.count);
                    stack.count -= taken;
                    count -= taken;
                }
            std::erase_if(campaign.storage, [](const auto& stack) { return stack.count <= 0; });
        }
    }

    const char* ContractResultMessage(EContractResult result)
    {
        switch (result)
        {
        case EContractResult::Ok: return "Done.";
        case EContractResult::InField: return "Contracts are handled from the safehouse.";
        case EContractResult::NotFound: return "That contract is no longer on offer.";
        case EContractResult::WrongState: return "That contract does not work that way.";
        case EContractResult::TooMany: return "You are already working on as many field contracts as you can.";
        case EContractResult::MissingItems: return "Your storage does not hold everything this delivery needs.";
        }
        return "";
    }

    bool IsFieldContract(EContractType type)
    {
        return type == EContractType::Recon || type == EContractType::Fetch;
    }

    uint32_t AllowedContractTier(const FCampaign& campaign, const FEconomyDefs& defs)
    {
        return std::max(1u, ReputationTier(campaign.reputation, defs));
    }

    uint32_t BoardSlots(const FCampaign& campaign, const FEconomyDefs& defs)
    {
        if (defs.board.slots.empty()) return 0;
        return defs.board.slots[std::min<size_t>(ReputationTier(campaign.reputation, defs), defs.board.slots.size() - 1)];
    }

    void RefreshBoard(FCampaign& campaign, const FEconomyDefs& defs, const FPoiIndex& pois)
    {
        const uint64_t cycle = TraderCycle(campaign);
        if (campaign.boardCycle == cycle || campaign.inField) return;
        campaign.boardCycle = cycle;
        const uint64_t finished = FinishedSorties(campaign);
        std::erase_if(campaign.board, [&](const FContract& contract)
        {
            if (finished - std::min(finished, contract.offeredAt) >= defs.board.replaceAfter) return true;
            if (!FindContractTemplate(defs, contract.templateId)) return true;
            if (!IsFieldContract(contract.type)) return false;
            // An offer whose region or site left the game is withdrawn.
            const auto map = pois.find(contract.mapId);
            return map == pois.end() || std::any_of(contract.targets.begin(), contract.targets.end(),
                [&](const auto& site) { return !FindPoi(map->second, site); });
        });
        FDeterministicRng rng;
        rng.RestoreState(campaign.boardRng);
        const uint32_t slots = BoardSlots(campaign, defs);
        for (int attempt = 0; campaign.board.size() < slots && attempt < 32; ++attempt)
        {
            const bool hasField = std::any_of(campaign.board.begin(), campaign.board.end(),
                [](const auto& contract) { return IsFieldContract(contract.type); });
            std::vector<const FContractTemplate*> eligible;
            for (const auto& entry : defs.contracts)
                if (Eligible(campaign, defs, entry, pois, !hasField)) eligible.push_back(&entry);
            if (eligible.empty() && !hasField)
                for (const auto& entry : defs.contracts)
                    if (Eligible(campaign, defs, entry, pois, false)) eligible.push_back(&entry);
            if (eligible.empty()) break;
            uint32_t total = 0;
            for (const auto* entry : eligible) total += entry->weight;
            uint32_t pick = rng.Range(total);
            const FContractTemplate* chosen = eligible.back();
            for (const auto* entry : eligible)
            {
                if (pick < entry->weight) { chosen = entry; break; }
                pick -= entry->weight;
            }
            campaign.board.push_back(RollContract(campaign, defs, *chosen, pois, rng));
        }
        campaign.boardRng = rng.State();
    }

    bool OfferContract(FCampaign& campaign, const FEconomyDefs& defs, const FPoiIndex& pois, std::string_view templateId)
    {
        const auto* entry = FindContractTemplate(defs, templateId);
        if (!entry || campaign.inField || (IsFieldContract(entry->type) && Placements(*entry, pois).empty())) return false;
        if (UsesTemplate(campaign.board, entry->id)) return true;     // already on offer
        if (UsesTemplate(campaign.contracts, entry->id)) return false;
        if (!entry->parcel.empty() && (UsesParcel(campaign.board, entry->parcel) || UsesParcel(campaign.contracts, entry->parcel)))
            return false;
        FDeterministicRng rng;
        rng.RestoreState(campaign.boardRng);
        campaign.board.push_back(RollContract(campaign, defs, *entry, pois, rng));
        campaign.boardRng = rng.State();
        return true;
    }

    const FContract* FindContract(const FCampaign& campaign, uint64_t id)
    {
        for (const auto* list : {&campaign.board, &campaign.contracts})
            for (const auto& contract : *list)
                if (contract.id == id) return &contract;
        return nullptr;
    }

    int StoredCount(const FCampaign& campaign, std::string_view item)
    {
        int count = 0;
        for (const auto& stack : campaign.storage)
            if (stack.defId == item) count += stack.count;
        return count;
    }

    bool CanHandIn(const FCampaign& campaign, const FContract& contract)
    {
        return contract.type == EContractType::Deliver && std::all_of(contract.items.begin(), contract.items.end(),
            [&](const auto& item) { return StoredCount(campaign, item.item) >= item.count; });
    }

    int SortiesLeft(const FCampaign& campaign, const FEconomyDefs& defs, const FContract& contract)
    {
        const uint64_t finished = FinishedSorties(campaign);
        const bool accepted = contract.state == EContractState::Accepted;
        const uint64_t since = finished - std::min(finished, accepted ? contract.acceptedAt : contract.offeredAt);
        const uint64_t limit = accepted ? defs.board.expireAfter : defs.board.replaceAfter;
        return static_cast<int>(limit - std::min(limit, since));
    }

    EContractResult AcceptContract(FCampaign& campaign, const FEconomyDefs& defs, uint64_t id)
    {
        if (campaign.inField) return EContractResult::InField;
        const auto found = std::find_if(campaign.board.begin(), campaign.board.end(), [id](const auto& c) { return c.id == id; });
        if (found == campaign.board.end()) return EContractResult::NotFound;
        if (!IsFieldContract(found->type)) return EContractResult::WrongState;
        if (campaign.contracts.size() >= defs.board.maxActive) return EContractResult::TooMany;
        FContract contract = std::move(*found);
        campaign.board.erase(found);
        contract.state = EContractState::Accepted;
        contract.acceptedAt = FinishedSorties(campaign);
        campaign.contracts.push_back(std::move(contract));
        return EContractResult::Ok;
    }

    EContractResult AbandonContract(FCampaign& campaign, const FEconomyDefs& defs, uint64_t id, FContractReport& report)
    {
        if (campaign.inField) return EContractResult::InField;
        const auto found = std::find_if(campaign.contracts.begin(), campaign.contracts.end(), [id](const auto& c) { return c.id == id; });
        if (found == campaign.contracts.end()) return EContractResult::NotFound;
        if (!found->parcel.empty()) report.purgeParcels.push_back(found->parcel);
        report.expired.push_back(found->templateId);
        campaign.contracts.erase(found);
        report.reputation += Penalize(campaign, defs);
        return EContractResult::Ok;
    }

    EContractResult HandInContract(FCampaign& campaign, const FEconomyDefs& defs, uint64_t id, FContractReport& report)
    {
        if (campaign.inField) return EContractResult::InField;
        const auto found = std::find_if(campaign.board.begin(), campaign.board.end(), [id](const auto& c) { return c.id == id; });
        if (found == campaign.board.end()) return EContractResult::NotFound;
        if (found->type != EContractType::Deliver) return EContractResult::WrongState;
        if (!CanHandIn(campaign, *found)) return EContractResult::MissingItems;
        const FContract contract = std::move(*found);
        campaign.board.erase(found);
        for (const auto& item : contract.items) TakeFromStorage(campaign, item.item, item.count);
        Complete(campaign, defs, contract, report);
        return EContractResult::Ok;
    }

    void RecordVisit(FSortieState& sortie, std::string_view site)
    {
        if (std::find(sortie.visitedPois.begin(), sortie.visitedPois.end(), site) == sortie.visitedPois.end())
            sortie.visitedPois.emplace_back(site);
    }

    bool ReconComplete(const FContract& contract, const FSortieState& sortie, std::string_view mapId)
    {
        return contract.type == EContractType::Recon && contract.mapId == mapId && !contract.targets.empty() &&
            std::all_of(contract.targets.begin(), contract.targets.end(), [&](const auto& site)
            { return std::find(sortie.visitedPois.begin(), sortie.visitedPois.end(), site) != sortie.visitedPois.end(); });
    }

    FContractReport SettleContracts(FCampaign& campaign, const FEconomyDefs& defs, Inventory& carried, std::string_view mapId)
    {
        FContractReport report;
        std::vector<FContract> remaining;
        for (auto& contract : campaign.contracts)
        {
            bool done = ReconComplete(contract, campaign.sortie, mapId);
            if (contract.type == EContractType::Fetch)
                if (const auto* parcel = carried.FindFirst(contract.parcel))
                    done = carried.TryRemove(parcel->instanceId, 1).has_value();
            if (done) Complete(campaign, defs, contract, report);
            else remaining.push_back(std::move(contract));
        }
        campaign.contracts = std::move(remaining);
        return report;
    }

    void ExpireContracts(FCampaign& campaign, const FEconomyDefs& defs, FContractReport& report)
    {
        const uint64_t finished = FinishedSorties(campaign);
        std::vector<FContract> remaining;
        for (auto& contract : campaign.contracts)
        {
            if (finished - std::min(finished, contract.acceptedAt) < defs.board.expireAfter)
            {
                remaining.push_back(std::move(contract));
                continue;
            }
            if (!contract.parcel.empty()) report.purgeParcels.push_back(contract.parcel);
            report.expired.push_back(contract.templateId);
            report.reputation += Penalize(campaign, defs);
        }
        campaign.contracts = std::move(remaining);
    }

    std::vector<FParcelTask> ActiveParcels(const FCampaign& campaign)
    {
        std::vector<FParcelTask> result;
        for (const auto& contract : campaign.contracts)
            if (contract.type == EContractType::Fetch && !contract.parcel.empty() && !contract.targets.empty())
                result.push_back({contract.parcel, contract.mapId, contract.targets.front()});
        return result;
    }

    bool ParcelIsActive(const FCampaign& campaign, std::string_view parcel)
    {
        return UsesParcel(campaign.contracts, parcel);
    }

    int PurgeItemFromWorld(FLootWorldState& world, std::string_view item)
    {
        int removed = 0;
        for (auto& entry : world.entries)
        {
            const size_t before = entry.contents.size();
            std::erase_if(entry.contents, [item](const auto& stack) { return stack.itemId == item; });
            const size_t gone = before - entry.contents.size();
            if (gone == 0) continue;
            removed += static_cast<int>(gone);
            if (!entry.contents.empty()) continue;
            if (entry.source == ELootSource::Pile)
            {
                entry.state = FLootEntry::EState::Empty;
                continue;
            }
            // An emptied spot cools down and restocks as if it had been looted.
            entry.state = FLootEntry::EState::Cooldown;
            entry.slot.state = ELootSlotState::Cooldown;
            entry.slot.committedAt = world.seconds;
            entry.slot.cooldownUntil = world.seconds;
            entry.slot.offscreenSince = -1.0;
        }
        return removed;
    }
}
