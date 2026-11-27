#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/UI/HUDTheme.hpp"
#include "Application/Game/NextDayz/UI/HUDWidgets.hpp"
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
    using namespace NextDayz::UITheme;
    using namespace NextDayz::UIWidgets;
    std::vector<std::function<void()>> requests;
    const uint32_t tier = NextDayz::ReputationTier(campaign_.reputation, economy_);

    // Top sub-tabs + reputation strip
    if (traderPageForced_) traderPageForced_ = false;
    if (SegmentedButton(Label("CONTRACTS"), traderPage_ == 0, ImVec2(150.0f, 32.0f))) traderPage_ = 0;
    ImGui::SameLine(0.0f, 6.0f);
    if (SegmentedButton(Label("TRADER & SERVICES"), traderPage_ == 1, ImVec2(170.0f, 32.0f))) traderPage_ = 1;
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::TextColored(ColorVec4(Colors::AccentAmber), "%s", Format("Gold: {}", campaign_.gold).c_str());
    ImGui::SameLine(0.0f, 20.0f);
    ImGui::TextUnformatted(Format("Reputation: {}  (tier {})", campaign_.reputation, tier).c_str());
    if (tier + 1 < economy_.reputationTiers.size())
    {
        ImGui::SameLine(0.0f, 14.0f);
        const uint32_t prev = tier < economy_.reputationTiers.size() ? economy_.reputationTiers[tier] : 0;
        const uint32_t next = economy_.reputationTiers[tier + 1];
        const float frac = next > prev ? std::clamp(static_cast<float>(campaign_.reputation - prev) / static_cast<float>(next - prev), 0.0f, 1.0f) : 1.0f;
        const ImVec2 bp = ImGui::GetCursorScreenPos();
        DrawTacticalBar(ImGui::GetWindowDrawList(), ImVec2(bp.x, bp.y + 6.0f), ImVec2(bp.x + 110.0f, bp.y + 13.0f), frac, Colors::AccentAmber);
        ImGui::Dummy(ImVec2(116.0f, 18.0f));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", Format("Next tier at {}", next).c_str());
    }
    ImGui::TextDisabled("%s", Tr("Shelves and the contract board renew after every expedition, whether you extract or not."));
    ImGui::Separator();

    if (traderPage_ == 0)
    {
        DrawContracts();
        ApplySafehouseRequests(requests);
        return;
    }

    if (ImGui::BeginTable("trader_layout", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("shelves", ImGuiTableColumnFlags_WidthStretch, 0.64f);
        ImGui::TableSetupColumn("services", ImGuiTableColumnFlags_WidthStretch, 0.36f);
        ImGui::TableNextColumn();
        DrawSectionHeader(Tr("Field supplies"), fmt::format("{}", economy_.trader.size()).c_str(), Colors::AccentAmber);
        if (economy_.trader.empty()) ImGui::TextColored(ColorVec4(Colors::AccentRose), "%s", Tr("The trader is closed: the price table failed to load."));

        ImGui::BeginChild("shelves_grid", ImVec2(0.0f, -8.0f));
        constexpr int kCols = 3;
        if (ImGui::BeginTable("shelves_cards", kCols, ImGuiTableFlags_SizingStretchSame))
        {
            for (const auto& entry : economy_.trader)
            {
                const auto result = NextDayz::CanBuy(campaign_, economy_, entry);
                const bool locked = result == NextDayz::ETradeResult::Locked;
                const auto* def = NextDayz::FindItemDef(entry.item);
                const uint64_t totalCost = entry.buy * static_cast<uint64_t>(entry.pack);
                const int left = NextDayz::TraderRemaining(campaign_, entry);

                ImGui::TableNextColumn();
                ImGui::PushID(entry.item.c_str());
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float w = ImGui::GetContentRegionAvail().x;
                const float h = 84.0f;
                DrawFieldCard(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + h),
                              locked ? Colors::BorderMuted : (def ? ItemKindColor(def->kind) : Colors::AccentAmber));
                ImGui::SetCursorScreenPos(ImVec2(p.x + 12.0f, p.y + 8.0f));
                const std::string name = fmt::format("{}  x{}", DisplayName(entry.item), entry.pack);
                ImGui::TextColored(ColorVec4(locked ? Colors::TextMuted : Colors::TextPrimary), "%s", name.c_str());
                ImGui::SetCursorScreenPos(ImVec2(p.x + 12.0f, p.y + 28.0f));
                if (locked)
                    ImGui::TextDisabled("%s", Format("Tier {}", entry.tier).c_str());
                else
                    ImGui::TextColored(ColorVec4(Colors::TextSecondary), "%s: %d", Tr("Left"), left);

                ImGui::SetCursorScreenPos(ImVec2(p.x + 12.0f, p.y + 52.0f));
                ImGui::TextColored(ColorVec4(Colors::AccentAmber), "%llu", static_cast<unsigned long long>(totalCost));
                ImGui::SetCursorScreenPos(ImVec2(p.x + w - 76.0f, p.y + 48.0f));
                if (!locked)
                {
                    ImGui::BeginDisabled(result != NextDayz::ETradeResult::Ok);
                    const std::string item = entry.item;
                    if (GhostButton(Label("Buy"), ImVec2(66.0f, 26.0f))) requests.emplace_back([this, item]()
                    { campaignMessage_ = NextDayz::TradeResultMessage(NextDayz::Buy(campaign_, economy_, item)); });
                    ImGui::EndDisabled();
                    if (result != NextDayz::ETradeResult::Ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("%s", Tr(NextDayz::TradeResultMessage(result)));
                }
                ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 8.0f));
                ImGui::Dummy(ImVec2(w, 0.0f));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::TableNextColumn();
        DrawSectionHeader(Tr("SERVICES"), nullptr, Colors::AccentEmerald);
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
            ImGui::TextColored(ColorVec4(Colors::AccentAmber), "%s", Format("{}: near {}", map ? map->name : sortie.insertionMap, where).c_str());
            if (GhostButton(Label("Cancel landing (refund)"), ImVec2(0.0f, 28.0f)))
                requests.emplace_back([this]() { NextDayz::CancelInsertion(campaign_); });
        }
        ImGui::Spacing();
        ImGui::TextDisabled("%s", Tr("WAREHOUSE"));
        const bool over = NextDayz::WarehouseOver(campaign_, economy_);
        const auto used = Format("WAREHOUSE  {} / {} stacks", campaign_.storage.size(), NextDayz::WarehouseCapacity(campaign_, economy_));
        ImGui::TextColored(ColorVec4(over ? Colors::AccentRose : Colors::TextPrimary), "%s", used.c_str());
        if (const auto* step = NextDayz::NextWarehouseStep(campaign_, economy_))
        {
            ImGui::BeginDisabled(campaign_.gold < step->price);
            if (GhostButton(Format("Expand to {} stacks ({} gold)", step->capacity, step->price).c_str(), ImVec2(0.0f, 28.0f)))
                requests.emplace_back([this]()
                { campaignMessage_ = NextDayz::TradeResultMessage(NextDayz::UpgradeWarehouse(campaign_, economy_)); });
            ImGui::EndDisabled();
        }
        else ImGui::TextDisabled("%s", Tr("The warehouse is fully expanded."));
        ImGui::Spacing();
        DrawSectionHeader(Tr("FIELD REPORTS"), nullptr, Colors::TextSecondary);
        if (campaign_.history.empty()) ImGui::TextDisabled("%s", Tr("No expeditions recorded yet."));
        const size_t shown = std::min<size_t>(campaign_.history.size(), 8);
        for (size_t offset = 0; offset < shown; ++offset)
        {
            const auto& record = campaign_.history[campaign_.history.size() - 1 - offset];
            const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, record.mapId);
            const int64_t net = NextDayz::SortieNet(record);
            const bool died = record.outcome == "died";
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            DrawFieldCard(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + 44.0f),
                          died ? Colors::AccentRose : Colors::AccentEmerald);
            ImGui::SetCursorScreenPos(ImVec2(p.x + 10.0f, p.y + 5.0f));
            ImGui::TextUnformatted(Format("#{}  {}", record.sortieId, map ? map->name : record.mapId).c_str());
            ImGui::SameLine();
            ImGui::TextColored(ColorVec4(died ? Colors::AccentRose : Colors::AccentEmerald), "%s", Tr(OutcomeLabel(record.outcome)));
            ImGui::SetCursorScreenPos(ImVec2(p.x + 10.0f, p.y + 23.0f));
            ImGui::TextDisabled("%s", Format("Brought home {}  /  contracts {}  /  spent {}  /  net {:+}", record.extractedValue,
                                             record.contractGold, record.spentBefore, net).c_str());
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 50.0f));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }
        ImGui::EndTable();
    }
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
