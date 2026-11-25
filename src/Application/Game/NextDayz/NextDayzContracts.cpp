#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/UI/Localization.hpp"
#include "Application/Game/NextDayz/World/Contracts.hpp"
#include "Application/Game/NextDayz/World/Economy.hpp"
#include "NextDayzGameInstance.hpp"

#include <imgui.h>
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/FileHelper.hpp"

// Contracts in the offline campaign (docs/projects/nextdayz/nextdayz-economy-contracts-design.md §5, §9):
// the board on the safehouse's trader page, site visits and parcels in the field,
// the HUD objective and markers.
namespace
{
    const ImVec4 contractAmber(0.89f, 0.71f, 0.40f, 1.0f);
    const ImVec4 contractGreen(0.62f, 0.82f, 0.55f, 1.0f);
    const ImVec4 contractRed(0.92f, 0.52f, 0.40f, 1.0f);

    std::string ContractCompassWord(const glm::vec3& delta)
    {
        static constexpr std::array<const char*, 8> words = {"north", "north-east", "east", "south-east",
                                                             "south", "south-west", "west", "north-west"};
        float bearing = glm::degrees(std::atan2(delta.x, -delta.z));
        bearing = bearing < 0.0f ? bearing + 360.0f : bearing;
        return words[static_cast<size_t>((bearing + 22.5f) / 45.0f) % 8];
    }

    // English display name: Format / Message translate their arguments.
    std::string ItemName(std::string_view item)
    {
        const auto* def = NextDayz::FindItemDef(item);
        return def ? std::string(def->displayName) : std::string(item);
    }

    const char* IssuerName(std::string_view issuer)
    {
        if (issuer == "medical") return "MEDICAL STATION";
        if (issuer == "quartermaster") return "QUARTERMASTER";
        if (issuer == "collector") return "COLLECTOR";
        return "RADIO STATION";
    }
}

const NextDayz::FPoiIndex& NextDayzGameInstance::ContractSites()
{
    if (contractSitesLoaded_) return contractSites_;
    contractSitesLoaded_ = true;
    for (const auto& map : expeditionMaps_)
    {
        if (!map.published || map.training) continue;
        std::vector<NextDayz::FPoi> sites;
        std::string error;
        const auto path = NextDayz::GenerationRecordPath(map).generic_string();
        if (NextDayz::LoadPoiIndex(Utilities::FileHelper::ResolvePlatformFilePath(path.c_str()), sites, error) && !sites.empty())
            contractSites_[map.id] = std::move(sites);
        else SPDLOG_WARN("[NextDayz] no contract sites for {}: {}", map.id, error);
    }
    return contractSites_;
}

const std::vector<NextDayz::FPoi>* NextDayzGameInstance::MapSites(const std::string& mapId)
{
    const auto& sites = ContractSites();
    const auto found = sites.find(mapId);
    return found == sites.end() ? nullptr : &found->second;
}

NextDayz::L10n::FText NextDayzGameInstance::TierUpText(uint32_t tierBefore) const
{
    const uint32_t tier = NextDayz::ReputationTier(campaign_.reputation, economy_);
    if (tier <= tierBefore) return {};
    return NextDayz::L10n::Message(" Reputation tier {} reached: new shelves and contracts are open.", tier);
}

bool NextDayzGameInstance::RefreshContractBoard()
{
    if (!CampaignEnabled()) return false;
    const auto floor = campaign_.warehouseFloor;
    NextDayz::ResolveWarehouse(campaign_, economy_);
    if (campaign_.inField) return floor != campaign_.warehouseFloor;
    if (campaign_.boardRng == 0 && campaign_.boardCycle == 0)
        campaign_.boardRng = sessionRng_.State() ^ 0x636F6E7472616374ULL;   // derived from the campaign's seed
    const uint64_t before = campaign_.boardCycle;
    NextDayz::RefreshBoard(campaign_, economy_, ContractSites());
    return campaign_.boardCycle != before || floor != campaign_.warehouseFloor;
}

void NextDayzGameInstance::TrackContractSites(float dt)
{
    if (!campaign_.inField) return;
    contractSiteClock_ += dt;
    if (contractSiteClock_ < 0.5f) return;
    contractSiteClock_ = 0.0f;
    const auto* sites = MapSites(ActiveMapId());
    if (!sites) return;
    const glm::vec3 feet = player_.Position();
    for (const auto* site : NextDayz::PoisAt(*sites, glm::vec2(feet.x, feet.z)))
    {
        auto& visited = campaign_.sortie.visitedPois;
        if (std::find(visited.begin(), visited.end(), site->name) != visited.end()) continue;
        NextDayz::RecordVisit(campaign_.sortie, site->name);
        const bool target = std::any_of(campaign_.contracts.begin(), campaign_.contracts.end(), [&](const auto& contract)
        {
            return contract.type == NextDayz::EContractType::Recon && contract.mapId == ActiveMapId() &&
                std::find(contract.targets.begin(), contract.targets.end(), site->name) != contract.targets.end();
        });
        if (target) Notify(NextDayz::L10n::Message("Site checked: {}. Extract to report it.", site->name), 3);
    }
}

void NextDayzGameInstance::PlantContractParcels()
{
    // Parcels whose contract has ended must not linger in this world.
    for (const auto& item : NextDayz::AllItemDefs())
        if (NextDayz::IsContractItem(item.id) && !NextDayz::ParcelIsActive(campaign_, item.id))
            loot_.PurgeItem(item.id, GetEngine());
    const auto* sites = MapSites(ActiveMapId());
    for (const auto& task : NextDayz::ActiveParcels(campaign_))
    {
        if (task.mapId != ActiveMapId() || inventory_.CountOf(task.parcel) > 0 || loot_.FindItem(task.parcel)) continue;
        const auto* site = sites ? NextDayz::FindPoi(*sites, task.site) : nullptr;
        if (!site || !loot_.PlantItem(site->center, site->radius, task.parcel, GetEngine()))
            SPDLOG_WARN("[NextDayz] cannot plant {} at {} on {}", task.parcel, task.site, task.mapId);
        else SPDLOG_INFO("[NextDayz] planted {} at {}", task.parcel, task.site);
    }
}

void NextDayzGameInstance::PurgeParcels(const std::vector<std::string>& parcels)
{
    for (const auto& parcel : parcels)
    {
        while (const auto* carried = inventory_.FindFirst(parcel)) inventory_.TryRemove(carried->instanceId);
        if (!cabinScene_) loot_.PurgeItem(parcel, GetEngine());
        for (auto& [id, state] : mapStates_) NextDayz::PurgeItemFromWorld(state.world, parcel);
        if (cabinCampaign_)
        {
            NextDayz::PurgeItemFromWorld(cabinCampaign_->world, parcel);
            for (auto& [id, state] : cabinCampaign_->maps) NextDayz::PurgeItemFromWorld(state.world, parcel);
        }
    }
}

std::string NextDayzGameInstance::ContractTitle(const NextDayz::FContract& contract) const
{
    const auto* entry = NextDayz::FindContractTemplate(economy_, contract.templateId);
    return entry ? std::string(NextDayz::L10n::Tr(entry->title)) : contract.templateId;
}

NextDayz::L10n::FText NextDayzGameInstance::ContractReportText(const NextDayz::FContractReport& report) const
{
    NextDayz::L10n::FText text;
    if (!report.completed.empty())
        text += NextDayz::L10n::Message(" Contracts completed: {} (+{} gold, reputation {:+}).",
                                        report.completed.size(), report.gold, report.reputation);
    for (const auto& item : report.itemRewards)
        text += NextDayz::L10n::Message(" Reward in storage: {}.", ItemName(item));
    if (!report.expired.empty())
        text += NextDayz::L10n::Message(" Contracts ended unfinished: {}.", report.expired.size());
    return text;
}

std::string NextDayzGameInstance::ContractObjective() const
{
    const glm::vec3 feet = player_.Position();
    const auto sitesIt = contractSites_.find(ActiveMapId());
    for (const auto& contract : campaign_.contracts)
    {
        if (contract.mapId != ActiveMapId()) continue;
        if (contract.type == NextDayz::EContractType::Fetch)
        {
            const std::string name = ItemName(contract.parcel);
            if (inventory_.CountOf(contract.parcel) > 0)
                return NextDayz::L10n::Format("Contract: {} is in your pack. Extract with it.", name);
            if (const auto at = loot_.FindItem(contract.parcel))
                return NextDayz::L10n::Format("Contract: fetch {} ({:.0f}m {})", name,
                    glm::length(glm::vec2(at->x - feet.x, at->z - feet.z)), ContractCompassWord(*at - feet));
            continue;
        }
        const auto& visited = campaign_.sortie.visitedPois;
        for (const auto& target : contract.targets)
        {
            if (std::find(visited.begin(), visited.end(), target) != visited.end()) continue;
            const NextDayz::FPoi* site = sitesIt == contractSites_.end() ? nullptr : NextDayz::FindPoi(sitesIt->second, target);
            if (!site) return NextDayz::L10n::Format("Contract: recon {}", target);
            const glm::vec3 at(site->center.x, feet.y, site->center.y);
            return NextDayz::L10n::Format("Contract: recon {} ({:.0f}m {})", target,
                glm::length(glm::vec2(at.x - feet.x, at.z - feet.z)), ContractCompassWord(at - feet));
        }
        return NextDayz::L10n::Format("Contract: {} checked. Extract to report it.", contract.targets.front());
    }
    return {};
}

std::vector<std::pair<glm::vec3, std::string>> NextDayzGameInstance::ContractMarkers() const
{
    std::vector<std::pair<glm::vec3, std::string>> markers;
    if (!CampaignEnabled() || !campaign_.inField) return markers;
    const auto sitesIt = contractSites_.find(ActiveMapId());
    const glm::vec3 feet = player_.Position();
    for (const auto& contract : campaign_.contracts)
    {
        if (contract.mapId != ActiveMapId()) continue;
        if (contract.type == NextDayz::EContractType::Fetch)
        {
            if (inventory_.CountOf(contract.parcel) == 0)
                if (const auto at = loot_.FindItem(contract.parcel))
                    markers.emplace_back(*at, NextDayz::L10n::Tr(ItemName(contract.parcel)));
            continue;
        }
        for (const auto& target : contract.targets)
        {
            const auto& visited = campaign_.sortie.visitedPois;
            if (std::find(visited.begin(), visited.end(), target) != visited.end() || sitesIt == contractSites_.end()) continue;
            if (const auto* site = NextDayz::FindPoi(sitesIt->second, target))
                markers.emplace_back(glm::vec3(site->center.x, feet.y, site->center.y), target);
        }
    }
    if (markers.size() > 3) markers.resize(3);
    return markers;
}

void NextDayzGameInstance::DrawContracts()
{
    using namespace NextDayz::L10n;
    std::vector<std::function<void()>> requests;
    const uint32_t tierBefore = NextDayz::ReputationTier(campaign_.reputation, economy_);
    const auto runReport = [this, tierBefore](const NextDayz::FContractReport& report, NextDayz::EContractResult result)
    {
        lastContractReport_ = report;
        PurgeParcels(report.purgeParcels);
        campaignMessage_ = result == NextDayz::EContractResult::Ok
            ? FText("Contract updated.") + ContractReportText(report) + TierUpText(tierBefore)
            : FText(NextDayz::ContractResultMessage(result));
    };
    const auto drawCard = [&](const NextDayz::FContract& contract)
    {
        const auto* entry = NextDayz::FindContractTemplate(economy_, contract.templateId);
        ImGui::PushID(static_cast<int>(contract.id));
        ImGui::TextDisabled("%s  /  %s", Tr(IssuerName(entry ? entry->issuer : "")), Format("Tier {}", contract.tier).c_str());
        ImGui::TextColored(contractAmber, "%s", ContractTitle(contract).c_str());
        if (contract.type == NextDayz::EContractType::Deliver)
        {
            for (const auto& item : contract.items)
            {
                const int stored = NextDayz::StoredCount(campaign_, item.item);
                ImGui::TextColored(stored >= item.count ? contractGreen : contractRed, "%s",
                    Format("Deliver {} x{}  ({} in storage)", ItemName(item.item), item.count, stored).c_str());
            }
        }
        else
        {
            const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, contract.mapId);
            const std::string region = map ? map->name : contract.mapId;
            if (contract.type == NextDayz::EContractType::Fetch)
                ImGui::TextWrapped("%s", Format("Bring back {} from {} ({}) on the rescue helicopter.",
                    ItemName(contract.parcel), contract.targets.front(), region).c_str());
            else
            {
                std::string targets;
                for (const auto& target : contract.targets) targets += (targets.empty() ? "" : ", ") + target;
                ImGui::TextWrapped("%s", Format("Visit {} ({}), then extract.", targets, region).c_str());
            }
        }
        const uint32_t gain = contract.tier >= 1 && contract.tier <= economy_.reputationGain.size()
            ? economy_.reputationGain[contract.tier - 1] : 0;
        ImGui::TextUnformatted(Format("Reward: {} gold, +{} reputation", contract.reward, gain).c_str());
        if (!contract.itemReward.empty())
        {
            ImGui::SameLine();
            ImGui::TextUnformatted(Format("+ {}", ItemName(contract.itemReward)).c_str());
        }
        const int left = NextDayz::SortiesLeft(campaign_, economy_, contract);
        ImGui::TextDisabled("%s", contract.state == NextDayz::EContractState::Accepted
            ? Format("Expires after {} more expeditions", left).c_str()
            : Format("Leaves the board after {} more expeditions", left).c_str());
        const uint64_t id = contract.id;
        if (contract.state == NextDayz::EContractState::Accepted)
        {
            if (ImGui::SmallButton(Label("Abandon"))) requests.emplace_back([this, id, runReport]()
            {
                NextDayz::FContractReport report;
                runReport(report, NextDayz::AbandonContract(campaign_, economy_, id, report));
            });
            ImGui::SameLine();
            ImGui::TextDisabled("%s", Format("Abandoning costs {} reputation", economy_.reputationPenalty).c_str());
        }
        else if (contract.type == NextDayz::EContractType::Deliver)
        {
            ImGui::BeginDisabled(!NextDayz::CanHandIn(campaign_, contract));
            if (ImGui::SmallButton(Label("Hand in"))) requests.emplace_back([this, id, runReport]()
            {
                NextDayz::FContractReport report;
                runReport(report, NextDayz::HandInContract(campaign_, economy_, id, report));
            });
            ImGui::EndDisabled();
        }
        else
        {
            const bool full = campaign_.contracts.size() >= economy_.board.maxActive;
            ImGui::BeginDisabled(full);
            if (ImGui::SmallButton(Label("Accept"))) requests.emplace_back([this, id]()
            {
                const auto result = NextDayz::AcceptContract(campaign_, economy_, id);
                // Old copies of the parcel from an earlier contract must not count for this one.
                if (const auto* accepted = NextDayz::FindContract(campaign_, id); accepted && !accepted->parcel.empty())
                    PurgeParcels({accepted->parcel});
                campaignMessage_ = result == NextDayz::EContractResult::Ok ? FText("Contract accepted. Deploy to its region.")
                                                                           : FText(NextDayz::ContractResultMessage(result));
            });
            ImGui::EndDisabled();
            if (full)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", Format("At most {} field contracts at once", economy_.board.maxActive).c_str());
            }
        }
        ImGui::Separator();
        ImGui::PopID();
    };

    if (ImGui::BeginTable("contracts_layout", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
    {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Format("CONTRACT BOARD  ({} / {})", campaign_.board.size(),
                                      NextDayz::BoardSlots(campaign_, economy_)).c_str());
        ImGui::TextDisabled("%s", Tr("Deliveries are handed in from storage. Field contracts pay when you extract."));
        ImGui::BeginChild("board", ImVec2(0, -4));
        if (campaign_.board.empty()) ImGui::TextDisabled("%s", Tr("No offers. New contracts arrive after your next expedition."));
        for (const auto& contract : campaign_.board) drawCard(contract);
        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Format("ACTIVE CONTRACTS  ({} / {})", campaign_.contracts.size(), economy_.board.maxActive).c_str());
        ImGui::TextDisabled("%s", Tr("Dying does not fail a contract. It stays open until it expires."));
        ImGui::BeginChild("active", ImVec2(0, -4));
        if (campaign_.contracts.empty()) ImGui::TextDisabled("%s", Tr("Accept a recon or fetch contract from the board."));
        for (const auto& contract : campaign_.contracts) drawCard(contract);
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ApplySafehouseRequests(requests);
}
