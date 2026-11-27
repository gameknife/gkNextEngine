#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/UI/HUDTheme.hpp"
#include "Application/Game/NextDayz/UI/HUDWidgets.hpp"
#include "Application/Game/NextDayz/UI/Localization.hpp"
#include "NextDayzGameInstance.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Options.hpp"

namespace
{
    constexpr ImU32 ocean = NextDayz::UITheme::Colors::AtlasOcean;
    constexpr ImU32 land  = NextDayz::UITheme::Colors::AtlasLand;
    constexpr ImU32 coast = NextDayz::UITheme::Colors::AtlasCoast;
    constexpr ImU32 grid  = NextDayz::UITheme::Colors::AtlasGrid;
    constexpr ImU32 ink   = NextDayz::UITheme::Colors::TextPrimary;
    constexpr ImU32 muted = NextDayz::UITheme::Colors::TextSecondary;
    constexpr ImU32 amber = NextDayz::UITheme::Colors::AccentAmber;

    void Label(ImDrawList* draw, ImVec2 position, const char* text, ImU32 color = ink, float scale = 1.0f)
    {
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, position, color, text);
    }
}

void NextDayzGameInstance::LoadWorldAtlas()
{
    if (atlasLoaded_) return;
    atlasLoaded_ = true;
    try
    {
        std::ifstream input(Utilities::FileHelper::ResolvePlatformFilePath("assets/configs/nextdayz/world_land.json"));
        const auto document = nlohmann::json::parse(input);
        if (document.at("format") != "nextdayz-atlas1") throw std::runtime_error("Invalid vector atlas");
        for (const auto& polygon : document.at("polygons"))
        {
            std::vector<glm::vec2> points;
            for (const auto& point : polygon)
                points.emplace_back(point.at(0).get<float>(), point.at(1).get<float>());
            if (points.size() >= 3) atlasPolygons_.push_back(std::move(points));
        }
    }
    catch (const std::exception& ex)
    {
        SPDLOG_ERROR("[NextDayz] cannot load offline vector atlas: {}", ex.what());
    }
}

void NextDayzGameInstance::DrawWorldOperations()
{
    LoadWorldAtlas();
    std::vector<const NextDayz::FExpeditionMap*> visible;
    for (const auto& map : expeditionMaps_)
        if (map.published || authoringMode_) visible.push_back(&map);
    selectedWorldMap_ = std::clamp(selectedWorldMap_, 0, std::max(0, static_cast<int>(visible.size()) - 1));
    // Open on the chosen destination rather than whichever region was browsed last.
    if (const auto* chosen = ChosenDeployMap(); chosen && !visible.empty() &&
        visible[selectedWorldMap_] != chosen && !visible[selectedWorldMap_]->training)
    {
        const auto found = std::find(visible.begin(), visible.end(), chosen);
        if (found != visible.end()) selectedWorldMap_ = static_cast<int>(found - visible.begin());
    }
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float availableHeight = ImGui::GetContentRegionAvail().y;
    const float sidebarWidth = std::clamp(availableWidth * 0.25f, NextDayz::UITheme::S(280.0f), NextDayz::UITheme::S(380.0f));

    using namespace NextDayz::UITheme;
    using namespace NextDayz::UIWidgets;
    FFieldStyleScope style;

    ImGui::BeginChild("operation_brief", ImVec2(sidebarWidth, availableHeight), false);
    DrawSectionHeader(NextDayz::L10n::Tr("DEPLOYMENT ZONES"), nullptr, Colors::AccentAmber);
    const size_t regions = static_cast<size_t>(std::count_if(visible.begin(), visible.end(),
        [](const auto* map) { return !map->training; }));
    ImGui::TextColored(ColorVec4(Colors::TextSecondary), NextDayz::L10n::Tr("%zu regions / 1 shared safehouse"), regions);
    ImGui::Spacing();
    for (size_t index = 0; index < visible.size(); ++index)
    {
        const auto& map = *visible[index];
        const auto count = campaign_.mapSorties.find(map.id);
        const uint64_t sorties = count == campaign_.mapSorties.end() ? 0 : count->second;
        const auto contracts = std::count_if(campaign_.contracts.begin(), campaign_.contracts.end(),
            [&](const auto& contract) { return contract.mapId == map.id; });
        const bool selected = selectedWorldMap_ == static_cast<int>(index);
        const bool destination = !authoringMode_ && ChosenDeployMap() == &map;

        ImGui::PushID(static_cast<int>(index));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = S(84.0f);
        const bool clicked = ImGui::InvisibleButton("##zone_card", ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        DrawFieldCard(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + h),
                      (selected || destination) ? Colors::AccentAmber : 0, selected || destination, hovered);
        auto* draw = ImGui::GetWindowDrawList();
        ImFont* boldFont = BoldFont();
        draw->AddText(boldFont, S(20.0f), ImVec2(p.x + S(14.0f), p.y + S(10.0f)),
                      (selected || destination) ? Colors::TextHighlight : Colors::TextPrimary,
                      NextDayz::L10n::Tr(map.name));
        if (!map.training && contracts > 0)
        {
            const std::string cBadge = fmt::format("{} {}", contracts, NextDayz::L10n::Tr("CONTRACTS"));
            const ImVec2 cSz = boldFont->CalcTextSizeA(S(13.5f), FLT_MAX, 0.0f, cBadge.c_str());
            const ImVec2 bMin(p.x + w - cSz.x - S(22.0f), p.y + S(10.0f));
            const ImVec2 bMax(p.x + w - S(10.0f), p.y + S(10.0f) + cSz.y + S(8.0f));
            draw->AddRectFilled(bMin, bMax, Colors::AccentAmber, S(5.0f));
            draw->AddText(boldFont, S(13.5f), ImVec2(bMin.x + S(6.0f), bMin.y + S(4.0f)),
                          Colors::TextOnAccent, cBadge.c_str());
        }
        draw->AddText(BodyFont(), S(15.5f), ImVec2(p.x + S(14.0f), p.y + S(36.0f)), Colors::TextSecondary, NextDayz::L10n::Tr(map.region));
        const std::string meta = map.training
            ? std::string(NextDayz::L10n::Tr("Training"))
            : NextDayz::L10n::Format("{} {}  ·  {} {}", NextDayz::L10n::Tr("Sorties"), sorties,
                                     NextDayz::L10n::Tr("CONTRACTS"), contracts);
        draw->AddText(BodyFont(), S(14.5f), ImVec2(p.x + S(14.0f), p.y + S(58.0f)), Colors::TextMuted, meta.c_str());

        if (clicked)
        {
            selectedWorldMap_ = static_cast<int>(index);
            BrowseWorldMap(map);
        }
        ImGui::PopID();
        ImGui::Spacing();
    }
    ImGui::Separator();
    ImGui::Spacing();
    if (!campaign_.lastMapId.empty())
    {
        ImGui::TextDisabled("%s", NextDayz::L10n::Tr("LAST ASSIGNMENT"));
        if (const auto* last = NextDayz::FindExpeditionMap(expeditionMaps_, campaign_.lastMapId))
            ImGui::TextWrapped("%s", NextDayz::L10n::Tr(last->name));
        ImGui::TextColored(ColorVec4(Colors::AccentEmerald), "%s", NextDayz::L10n::Tr(campaign_.lastOutcome));
        ImGui::Spacing();
    }
    const auto* destination = ChosenDeployMap();
    if (!authoringMode_)
    {
        ImGui::TextDisabled("%s", NextDayz::L10n::Tr("DEPLOYMENT DESTINATION"));
        ImGui::PushFont(BoldFont(), S(19.0f));
        if (destination)
            ImGui::TextColored(ColorVec4(Colors::AccentAmber), "%s", NextDayz::L10n::Tr(destination->name));
        else
            ImGui::TextColored(ColorVec4(Colors::AccentAmber), "%s", NextDayz::L10n::Tr("RANDOM ASSIGNMENT"));
        ImGui::PopFont();
    }
    ImGui::Spacing();
    ImGui::TextColored(ColorVec4(Colors::TextSecondary), NextDayz::L10n::Tr("Carried gear: %zu stacks"), inventory_.Instances().size());
    ImGui::TextColored(ColorVec4(Colors::TextSecondary), NextDayz::L10n::Tr("Free space: %d"), inventory_.TotalCapacity() - inventory_.UsedCapacity());
    ImGui::Spacing();
    const char* deployLabel = authoringMode_ ? NextDayz::L10n::Tr("PLAYTEST CURRENT MAP")
        : destination ? NextDayz::L10n::Tr("DEPLOY TO DESTINATION") : NextDayz::L10n::Tr("ACCEPT & DEPLOY");
    if (PrimaryButton(deployLabel, ImVec2(-1, S(50.0f)), !visible.empty() && !pendingCampaign_.has_value())) Deploy();
    if (destination && GhostButton(NextDayz::L10n::Label("RANDOM ASSIGNMENT"), ImVec2(-1, S(36.0f)))) SetDeployChoice({});
    if (GhostButton(NextDayz::L10n::Label("PREPARE LOADOUT"), ImVec2(-1, S(36.0f)))) safehouseTab_ = 1;
    ImGui::Spacing();
    if (authoringMode_ || GOption->AgentValidation)
    {
        ImGui::TextDisabled("%s", authoringMode_ ? NextDayz::L10n::Tr("DRAFT PLAYTEST") : NextDayz::L10n::Tr("TEMPORARY MAP TEST"));
        ImGui::BeginDisabled(visible.empty() || pendingCampaign_.has_value() || campaignSaveBlocked_);
        if (GhostButton(authoringMode_ ? NextDayz::L10n::Tr("PLAYTEST BROWSED MAP") : NextDayz::L10n::Tr("ENTER SELECTED MAP (TEST)"), ImVec2(-1, S(36.0f))))
        {
            const auto& selected = *visible[selectedWorldMap_];
            const auto& scene = authoringMode_ ? selected.draftScene : selected.scene;
            if (scene == campaignMap_ && !cabinScene_) CompleteDeployment();
            else RequestExpeditionMap(selected, true);
        }
        ImGui::EndDisabled();
        if (!visible.empty()) ImGui::TextWrapped(NextDayz::L10n::Tr("Selected: %s"), NextDayz::L10n::Tr(visible[selectedWorldMap_]->name));
    }
    const auto training = std::find_if(visible.begin(), visible.end(), [](const auto* map) { return map->training; });
    if (training != visible.end() && !authoringMode_)
    {
        ImGui::Spacing();
        ImGui::BeginDisabled(pendingCampaign_.has_value() || campaignSaveBlocked_);
        if (GhostButton(NextDayz::L10n::Tr("ENTER TRAINING GROUND"), ImVec2(-1, S(36.0f)))) StartTraining();
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, S(14.0f));

    if (DrawWorldAtlas(visible, availableHeight)) BrowseWorldMap(*visible[selectedWorldMap_]);
}

bool NextDayzGameInstance::DrawWorldAtlas(const std::vector<const NextDayz::FExpeditionMap*>& visible, float availableHeight)
{
    using namespace NextDayz::UITheme;
    bool picked = false;
    LoadWorldAtlas();
    selectedWorldMap_ = std::clamp(selectedWorldMap_, 0, std::max(0, static_cast<int>(visible.size()) - 1));
    ImGui::BeginChild("world_atlas_panel", ImVec2(0, availableHeight), false);
    ImGui::PushFont(BoldFont(), S(16.5f));
    ImGui::TextUnformatted(NextDayz::L10n::Tr("GLOBAL OPERATIONS"));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", NextDayz::L10n::Tr("  /  KNOWN DEPLOYMENT ZONES"));
    // The offline campaign adds the paid landing selector under the description.
    const float footer = netMode_ == ENetMode::Offline && CampaignEnabled() && !authoringMode_ ? S(218.0f) : S(162.0f);
    const ImVec2 mapSize(std::max(180.0f, ImGui::GetContentRegionAvail().x),
                        std::max(160.0f, ImGui::GetContentRegionAvail().y - footer));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + mapSize.x, origin.y + mapSize.y);
    ImGui::InvisibleButton("vector_world_atlas", mapSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    auto& io = ImGui::GetIO();
    if (hovered && io.MouseWheel != 0)
        atlasZoom_ = std::clamp(atlasZoom_ * std::pow(1.18f, io.MouseWheel), 1.0f, 5.0f);
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        atlasPan_.x += io.MouseDelta.x;
        atlasPan_.y += io.MouseDelta.y;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        atlasZoom_ = 1.0f;
        atlasPan_ = {};
    }
    const float scale = std::min(mapSize.x / 360.0f, mapSize.y / 155.0f) * atlasZoom_ * 0.94f;
    const ImVec2 center(origin.x + mapSize.x * 0.5f + atlasPan_.x, origin.y + mapSize.y * 0.5f + atlasPan_.y);
    auto project = [center, scale](double longitude, double latitude)
    {
        return ImVec2(center.x + static_cast<float>(longitude) * scale,
                      center.y - (static_cast<float>(latitude) - 12.5f) * scale);
    };
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, end, ocean, S(8.0f));
    draw->PushClipRect(origin, end, true);
    for (int longitude = -180; longitude <= 180; longitude += 30)
        draw->AddLine(project(longitude, -90), project(longitude, 90), grid);
    for (int latitude = -60; latitude <= 90; latitude += 30)
        draw->AddLine(project(-180, latitude), project(180, latitude), grid);
    std::vector<ImVec2> projected;
    for (const auto& polygon : atlasPolygons_)
    {
        projected.clear();
        for (const auto& point : polygon) projected.push_back(project(point.x, point.y));
        draw->AddConcavePolyFilled(projected.data(), static_cast<int>(projected.size()), land);
        draw->AddPolyline(projected.data(), static_cast<int>(projected.size()), coast, ImDrawFlags_Closed, 1.15f);
    }
    Label(draw, project(-135, 30), NextDayz::L10n::Tr("NORTH AMERICA"), muted, 0.78f);
    Label(draw, project(-68, -15), NextDayz::L10n::Tr("SOUTH AMERICA"), muted, 0.78f);
    Label(draw, project(2, 8), NextDayz::L10n::Tr("AFRICA"), muted, 0.84f);
    Label(draw, project(58, 55), NextDayz::L10n::Tr("EURASIA"), muted, 0.84f);
    Label(draw, project(117, -29), NextDayz::L10n::Tr("AUSTRALIA"), muted, 0.78f);
    Label(draw, project(-168, 2), NextDayz::L10n::Tr("PACIFIC"), grid, 0.86f);
    Label(draw, project(68, -18), NextDayz::L10n::Tr("INDIAN OCEAN"), grid, 0.78f);
    if (atlasPolygons_.empty()) Label(draw, ImVec2(origin.x + 20, origin.y + 65), NextDayz::L10n::Tr("Vector atlas unavailable"), amber);

    ImFont* boldFont = BoldFont();
    for (size_t index = 0; index < visible.size(); ++index)
    {
        const auto& map = *visible[index];
        if (map.training) continue;
        const ImVec2 point = project(map.longitude, map.latitude);
        const bool selected = static_cast<int>(index) == selectedWorldMap_;
        const float distance = std::hypot(io.MousePos.x - point.x, io.MousePos.y - point.y);
        if (hovered && distance < S(22.0f))
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                selectedWorldMap_ = static_cast<int>(index);
                picked = true;
            }
        }
        if (selected)
        {
            // Glowing multi-ring tactical reticle (matches Mockup 04)
            const float pulse = S(22.0f) + S(3.0f) * std::sin(static_cast<float>(ImGui::GetTime()) * 2.2f);
            draw->AddCircleFilled(point, pulse * 1.15f, IM_COL32(224, 162, 57, 36), 32);
            draw->AddCircle(point, pulse, IM_COL32(224, 162, 57, 170), 32, 1.6f);
            draw->AddCircle(point, S(12.5f), IM_COL32(240, 182, 84, 225), 24, 1.5f);
            draw->AddLine(ImVec2(point.x - S(28.0f), point.y), ImVec2(point.x - S(9.0f), point.y), amber, 1.5f);
            draw->AddLine(ImVec2(point.x + S(9.0f), point.y), ImVec2(point.x + S(28.0f), point.y), amber, 1.5f);
            draw->AddLine(ImVec2(point.x, point.y - S(28.0f)), ImVec2(point.x, point.y - S(9.0f)), amber, 1.5f);
            draw->AddLine(ImVec2(point.x, point.y + S(9.0f)), ImVec2(point.x, point.y + S(28.0f)), amber, 1.5f);
        }
        draw->AddCircleFilled(point, S(6.0f), selected ? amber : ink, 18);
        draw->AddCircle(point, S(8.5f), ocean, 18, 2.2f);
        const auto count = campaign_.mapSorties.find(map.id);
        const uint64_t sorties = count == campaign_.mapSorties.end() ? 0 : count->second;
        const std::string titleLine = NextDayz::L10n::Tr(map.name);
        const std::string subLine = NextDayz::L10n::Format("{} DEPLOYMENTS", sorties);
        const ImVec2 tSz = boldFont->CalcTextSizeA(S(17.0f), FLT_MAX, 0.0f, titleLine.c_str());
        const ImVec2 sSz = BodyFont()->CalcTextSizeA(S(14.0f), FLT_MAX, 0.0f, subLine.c_str());
        const float boxW = std::max(tSz.x, sSz.x) + S(22.0f);
        const float boxH = tSz.y + sSz.y + S(16.0f);
        const float labelX = point.x > end.x - boxW - S(36.0f) ? point.x - boxW - S(24.0f) : point.x + S(26.0f);
        const ImVec2 boxMin(labelX, point.y - boxH * 0.5f);
        const ImVec2 boxMax(boxMin.x + boxW, boxMin.y + boxH);
        draw->AddLine(point, ImVec2(labelX < point.x ? boxMax.x : boxMin.x, point.y), selected ? amber : muted, 1.4f);
        draw->AddRectFilled(ImVec2(boxMin.x, boxMin.y + 2.0f), ImVec2(boxMax.x, boxMax.y + 2.0f), IM_COL32(4, 6, 5, 145), S(6.0f));
        draw->AddRectFilled(boxMin, boxMax, IM_COL32(18, 23, 20, 242), S(6.0f));
        draw->AddRect(boxMin, boxMax, selected ? amber : Colors::BorderTactical, S(6.0f), 0, selected ? 1.6f : 1.2f);
        draw->AddText(boldFont, S(17.0f), ImVec2(boxMin.x + S(11.0f), boxMin.y + S(6.0f)),
                      selected ? Colors::TextHighlight : Colors::TextPrimary, titleLine.c_str());
        draw->AddText(BodyFont(), S(14.0f), ImVec2(boxMin.x + S(11.0f), boxMin.y + S(8.0f) + tSz.y),
                      Colors::TextSecondary, subLine.c_str());
    }
    Label(draw, ImVec2(origin.x + S(16.0f), origin.y + S(14.0f)), NextDayz::L10n::Tr("FIELD ATLAS  /  WGS84"), muted, 0.82f);
    Label(draw, ImVec2(origin.x + S(16.0f), end.y - S(28.0f)), NextDayz::L10n::Tr("WHEEL: ZOOM   /   RIGHT DRAG: PAN   /   DOUBLE CLICK: RESET"), muted, 0.74f);
    draw->PopClipRect();
    draw->AddRect(origin, end, coast, S(8.0f), 0, 1.5f);

    ImGui::Spacing();
    if (!visible.empty())
    {
        const auto& map = *visible[selectedWorldMap_];
        ImGui::PushFont(HeadingFont(), S(24.0f));
        ImGui::TextColored(ColorVec4(Colors::AccentAmber), "%s", NextDayz::L10n::Tr(map.name));
        ImGui::PopFont();
        ImGui::SameLine(0.0f, S(14.0f));
        ImGui::TextColored(ColorVec4(Colors::TextSecondary), "|  %s  |  %.0f x %.0f m", NextDayz::L10n::Tr(map.region), map.sizeM, map.sizeM);
        if (map.training) ImGui::TextDisabled("%s   /   %s", NextDayz::L10n::Tr("NO FIXED LOCATION"), NextDayz::L10n::Tr(map.terrain));
        else ImGui::TextDisabled("%.4f N, %.4f E   /   %s", map.latitude, map.longitude, NextDayz::L10n::Tr(map.terrain));
        ImGui::TextWrapped("%s", NextDayz::L10n::Tr(map.description));
        if (netMode_ == ENetMode::Offline && CampaignEnabled() && !authoringMode_ && NextDayz::IsDispatchable(map, false))
            DrawPaidLanding(map);
    }
    ImGui::TextDisabled("%s", NextDayz::L10n::Tr("Made with Natural Earth."));
    ImGui::EndChild();
    return picked;
}
