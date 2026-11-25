#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/UI/Localization.hpp"
#include "Application/Game/NextDayz/World/Economy.hpp"
#include "NextDayzGameInstance.hpp"

#include <imgui.h>

// The safehouse trader page and the paid landing selector
// (docs/projects/nextdayz/nextdayz-economy-contracts-design.md §4.2, §4.3, §9).
namespace
{
    const ImVec4 traderAmber(0.89f, 0.71f, 0.40f, 1.0f);
    const ImVec4 traderSage(0.70f, 0.78f, 0.64f, 1.0f);
    const ImVec4 traderWarning(0.92f, 0.52f, 0.40f, 1.0f);

    std::string DisplayName(std::string_view item)
    {
        const auto* def = NextDayz::FindItemDef(item);
        return def ? std::string(NextDayz::L10n::Tr(def->displayName)) : std::string(item);
    }

    const char* OutcomeLabel(std::string_view outcome)
    {
        if (outcome == "extracted") return "EXTRACTED";
        if (outcome == "died") return "DIED";
        return "DEBUG RETURN";
    }
}

void NextDayzGameInstance::DrawTrader()
{
    using namespace NextDayz::L10n;
    std::vector<std::function<void()>> requests;
    const uint32_t tier = NextDayz::ReputationTier(campaign_.reputation, economy_);
    ImGui::TextColored(traderAmber, "%s", Format("Gold: {}", campaign_.gold).c_str());
    ImGui::SameLine(0, 30);
    ImGui::TextUnformatted(Format("Reputation: {}  (tier {})", campaign_.reputation, tier).c_str());
    if (tier + 1 < economy_.reputationTiers.size())
    {
        ImGui::SameLine(0, 30);
        ImGui::TextDisabled("%s", Format("Next tier at {}", economy_.reputationTiers[tier + 1]).c_str());
    }
    ImGui::TextDisabled("%s", Tr("Shelves and the contract board renew after every expedition, whether you extract or not."));
    ImGui::Separator();
    if (!ImGui::BeginTabBar("trader_pages"))
    {
        ApplySafehouseRequests(requests);
        return;
    }
    const auto pageFlags = [this](int page)
    { return traderPageForced_ && traderPage_ == page ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
    if (ImGui::BeginTabItem(Label("CONTRACTS"), nullptr, pageFlags(0)))
    {
        if (!traderPageForced_) traderPage_ = 0;
        DrawContracts();
        ImGui::EndTabItem();
    }
    const bool shelves = ImGui::BeginTabItem(Label("TRADER & SERVICES"), nullptr, pageFlags(1));
    if (shelves && !traderPageForced_) traderPage_ = 1;
    traderPageForced_ = false;
    if (shelves && ImGui::BeginTable("trader_layout", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("shelves", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableSetupColumn("services", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Tr("TRADER"));
        if (economy_.trader.empty()) ImGui::TextColored(traderWarning, "%s", Tr("The trader is closed: the price table failed to load."));
        if (ImGui::BeginTable("shelves", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH,
                              ImVec2(0, std::max(120.0f, ImGui::GetContentRegionAvail().y - 12))))
        {
            ImGui::TableSetupColumn(Tr("Item"));
            ImGui::TableSetupColumn(Tr("Pack"), ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn(Tr("Price"), ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn(Tr("Left"), ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("##buy", ImGuiTableColumnFlags_WidthFixed, 130);
            ImGui::TableHeadersRow();
            for (const auto& entry : economy_.trader)
            {
                const auto result = NextDayz::CanBuy(campaign_, economy_, entry);
                const bool locked = result == NextDayz::ETradeResult::Locked;
                ImGui::PushID(entry.item.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const std::string name = DisplayName(entry.item);
                if (locked) ImGui::TextDisabled("%s", name.c_str());
                else ImGui::TextUnformatted(name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("x%d", entry.pack);
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(entry.buy * static_cast<uint64_t>(entry.pack)));
                ImGui::TableNextColumn();
                ImGui::Text("%d", NextDayz::TraderRemaining(campaign_, entry));
                ImGui::TableNextColumn();
                if (locked) ImGui::TextDisabled("%s", Format("Tier {}", entry.tier).c_str());
                else
                {
                    ImGui::BeginDisabled(result != NextDayz::ETradeResult::Ok);
                    const std::string item = entry.item;
                    if (ImGui::SmallButton(Label("Buy"))) requests.emplace_back([this, item]()
                    { campaignMessage_ = NextDayz::TradeResultMessage(NextDayz::Buy(campaign_, economy_, item)); });
                    ImGui::EndDisabled();
                    if (result != NextDayz::ETradeResult::Ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("%s", Tr(NextDayz::TradeResultMessage(result)));
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Tr("SERVICES"));
        ImGui::Spacing();
        ImGui::TextDisabled("%s", Tr("PAID LANDING"));
        const auto& sortie = campaign_.sortie;
        if (sortie.insertionMap.empty())
            ImGui::TextWrapped("%s", Format("Random landing. Pick a landing point on the world map for {} gold.",
                                            economy_.insertionFee).c_str());
        else
        {
            const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, sortie.insertionMap);
            const auto& points = map ? LandingPoints(*map) : landingPoints_[sortie.insertionMap];
            const std::string where = sortie.insertionPoint < static_cast<int>(points.size())
                ? points[sortie.insertionPoint].nearSite : std::string("?");
            ImGui::TextColored(traderAmber, "%s", Format("{}: near {}", map ? map->name : sortie.insertionMap, where).c_str());
            if (ImGui::SmallButton(Label("Cancel landing (refund)")))
                requests.emplace_back([this]() { NextDayz::CancelInsertion(campaign_); });
        }
        ImGui::Spacing();
        ImGui::TextDisabled("%s", Tr("WAREHOUSE"));
        const bool over = NextDayz::WarehouseOver(campaign_, economy_);
        const auto used = Format("WAREHOUSE  {} / {} stacks", campaign_.storage.size(), NextDayz::WarehouseCapacity(campaign_, economy_));
        if (over) ImGui::TextColored(traderWarning, "%s", used.c_str());
        else ImGui::TextUnformatted(used.c_str());
        if (const auto* step = NextDayz::NextWarehouseStep(campaign_, economy_))
        {
            ImGui::BeginDisabled(campaign_.gold < step->price);
            if (ImGui::SmallButton(Format("Expand to {} stacks ({} gold)", step->capacity, step->price).c_str()))
                requests.emplace_back([this]()
                { campaignMessage_ = NextDayz::TradeResultMessage(NextDayz::UpgradeWarehouse(campaign_, economy_)); });
            ImGui::EndDisabled();
        }
        else ImGui::TextDisabled("%s", Tr("The warehouse is fully expanded."));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("%s", Tr("FIELD REPORTS"));
        if (campaign_.history.empty()) ImGui::TextDisabled("%s", Tr("No expeditions recorded yet."));
        const size_t shown = std::min<size_t>(campaign_.history.size(), 10);
        for (size_t offset = 0; offset < shown; ++offset)
        {
            const auto& record = campaign_.history[campaign_.history.size() - 1 - offset];
            const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, record.mapId);
            const int64_t net = NextDayz::SortieNet(record);
            ImGui::TextUnformatted(Format("#{}  {}", record.sortieId, map ? map->name : record.mapId).c_str());
            ImGui::SameLine();
            ImGui::TextColored(record.outcome == "died" ? traderWarning : traderSage, "%s", Tr(OutcomeLabel(record.outcome)));
            ImGui::TextDisabled("%s", Format("Brought home {}  /  contracts {}  /  spent {}  /  net {:+}", record.extractedValue,
                                             record.contractGold, record.spentBefore, net).c_str());
        }
        ImGui::EndTable();
    }
    if (shelves) ImGui::EndTabItem();
    ImGui::EndTabBar();
    ApplySafehouseRequests(requests);
}

void NextDayzGameInstance::DrawPaidLanding(const NextDayz::FExpeditionMap& map)
{
    using namespace NextDayz::L10n;
    const auto& points = LandingPoints(map);
    if (points.empty()) return;
    const auto& sortie = campaign_.sortie;
    const bool paidHere = sortie.insertionMap == map.id && sortie.insertionPoint >= 0 &&
        sortie.insertionPoint < static_cast<int>(points.size());
    const auto pointLabel = [&](size_t index)
    { return Format("Near {} ({:.0f} m)", points[index].nearSite, points[index].siteDistance); };
    const std::string preview = paidHere ? pointLabel(static_cast<size_t>(sortie.insertionPoint))
        : Format("Random landing (free)");
    ImGui::Spacing();
    ImGui::TextDisabled("%s", Format("LANDING  /  choosing a point costs {} gold, refunded if you cancel", economy_.insertionFee).c_str());
    std::vector<std::function<void()>> requests;
    ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("##paid_landing", preview.c_str()))
    {
        if (ImGui::Selectable(Format("Random landing (free)").c_str(), !paidHere) && paidHere)
            requests.emplace_back([this]() { NextDayz::CancelInsertion(campaign_); });
        for (size_t index = 0; index < points.size(); ++index)
        {
            const bool selected = paidHere && sortie.insertionPoint == static_cast<int>(index);
            if (ImGui::Selectable(pointLabel(index).c_str(), selected) && !selected)
            {
                const std::string id = map.id;
                const int point = static_cast<int>(index);
                requests.emplace_back([this, id, point]()
                {
                    deployMapChoice_ = id;
                    const auto result = NextDayz::PayInsertion(campaign_, economy_, id, point);
                    campaignMessage_ = result == NextDayz::ETradeResult::Ok
                        ? NextDayz::L10n::Message("Landing point booked. Fly there with DEPLOY.")
                        : NextDayz::L10n::FText(NextDayz::TradeResultMessage(result));
                });
            }
        }
        ImGui::EndCombo();
    }
    ApplySafehouseRequests(requests);
}
