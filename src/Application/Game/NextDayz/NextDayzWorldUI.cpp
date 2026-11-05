#include "Engine/Common/CoreMinimal.hpp"
#include "NextDayzGameInstance.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include "Engine/Utilities/FileHelper.hpp"

namespace
{
    constexpr ImU32 ocean = IM_COL32(13, 23, 26, 255);
    constexpr ImU32 land = IM_COL32(42, 62, 60, 255);
    constexpr ImU32 coast = IM_COL32(79, 108, 97, 255);
    constexpr ImU32 grid = IM_COL32(49, 72, 70, 110);
    constexpr ImU32 ink = IM_COL32(219, 229, 211, 255);
    constexpr ImU32 muted = IM_COL32(128, 155, 146, 255);
    constexpr ImU32 amber = IM_COL32(227, 181, 101, 255);

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
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float availableHeight = ImGui::GetContentRegionAvail().y;
    const float sidebarWidth = std::clamp(availableWidth * 0.25f, 245.0f, 340.0f);

    ImGui::BeginChild("operation_brief", ImVec2(sidebarWidth, availableHeight), false);
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.89f,0.71f,0.40f,1), "MERCENARY FIELD OFFICE");
    ImGui::Spacing();
    ImGui::TextWrapped("Search. Survive. Bring it home.");
    ImGui::TextDisabled("%zu regions / 1 shared safehouse", visible.size());
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("FIELD RECORDS");
    for (size_t index = 0; index < visible.size(); ++index)
    {
        const auto& map = *visible[index];
        const auto count = campaign_.mapSorties.find(map.id);
        const uint64_t sorties = count == campaign_.mapSorties.end() ? 0 : count->second;
        ImGui::PushID(static_cast<int>(index));
        ImGui::Spacing();
        const auto title = fmt::format("{}\n{}  |  {} deployments", map.name, map.region, sorties);
        if (ImGui::Selectable(title.c_str(), selectedWorldMap_ == static_cast<int>(index), 0, ImVec2(0, 62)))
            selectedWorldMap_ = static_cast<int>(index);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (!campaign_.lastMapId.empty())
    {
        ImGui::TextDisabled("LAST ASSIGNMENT");
        if (const auto* last = NextDayz::FindExpeditionMap(expeditionMaps_, campaign_.lastMapId))
            ImGui::TextWrapped("%s", last->name.c_str());
        ImGui::TextColored(ImVec4(0.7f,0.78f,0.64f,1), "%s", campaign_.lastOutcome.c_str());
        ImGui::Spacing();
    }
    ImGui::TextDisabled("CLASSIFIED RECOVERY CONTRACT");
    ImGui::TextWrapped("Destination assigned on departure. The map is your field record; browsing a region does not select your next assignment.");
    ImGui::Spacing();
    ImGui::Text("Carried gear: %zu stacks", inventory_.Instances().size());
    ImGui::Text("Free space: %d", inventory_.TotalCapacity() - inventory_.UsedCapacity());
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.46f,0.33f,0.16f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f,0.44f,0.23f,1));
    ImGui::BeginDisabled(visible.empty() || pendingCampaign_.has_value());
    if (ImGui::Button(authoringMode_ ? "PLAYTEST CURRENT MAP" : "ACCEPT & DEPLOY", ImVec2(-1, 48))) Deploy();
    ImGui::EndDisabled();
    ImGui::PopStyleColor(2);
    if (ImGui::Button("PREPARE LOADOUT", ImVec2(-1, 30))) safehouseTab_ = 1;
    ImGui::Spacing();
    ImGui::TextDisabled(authoringMode_ ? "DRAFT PLAYTEST" : "TEMPORARY MAP TEST");
    ImGui::BeginDisabled(visible.empty() || pendingCampaign_.has_value() || campaignSaveBlocked_);
    if (ImGui::Button(authoringMode_ ? "PLAYTEST BROWSED MAP" : "ENTER SELECTED MAP (TEST)", ImVec2(-1, 36)))
    {
        const auto& selected = *visible[selectedWorldMap_];
        const auto& scene = authoringMode_ ? selected.draftScene : selected.scene;
        if (scene == campaignMap_) CompleteDeployment();
        else RequestExpeditionMap(selected, true);
    }
    ImGui::EndDisabled();
    if (!visible.empty()) ImGui::TextWrapped("Selected: %s", visible[selectedWorldMap_]->name.c_str());
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("world_atlas_panel", ImVec2(0, availableHeight), false);
    ImGui::TextUnformatted("GLOBAL OPERATIONS");
    ImGui::SameLine();
    ImGui::TextDisabled("  /  KNOWN DEPLOYMENT ZONES");
    const ImVec2 mapSize(std::max(180.0f, ImGui::GetContentRegionAvail().x),
                        std::max(150.0f, ImGui::GetContentRegionAvail().y - 150.0f));
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
    draw->AddRectFilled(origin, end, ocean, 5.0f);
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
        draw->AddPolyline(projected.data(), static_cast<int>(projected.size()), coast, ImDrawFlags_Closed, 0.9f);
    }
    Label(draw, project(-135, 30), "NORTH AMERICA", muted, 0.75f);
    Label(draw, project(-68, -15), "SOUTH AMERICA", muted, 0.75f);
    Label(draw, project(2, 8), "AFRICA", muted, 0.8f);
    Label(draw, project(58, 55), "EURASIA", muted, 0.8f);
    Label(draw, project(117, -29), "AUSTRALIA", muted, 0.75f);
    Label(draw, project(-168, 2), "PACIFIC", grid, 0.9f);
    Label(draw, project(68, -18), "INDIAN OCEAN", grid, 0.75f);
    if (atlasPolygons_.empty()) Label(draw, ImVec2(origin.x + 20, origin.y + 65), "Vector atlas unavailable", amber);

    for (size_t index = 0; index < visible.size(); ++index)
    {
        const auto& map = *visible[index];
        const ImVec2 point = project(map.longitude, map.latitude);
        const bool selected = static_cast<int>(index) == selectedWorldMap_;
        const float distance = std::hypot(io.MousePos.x - point.x, io.MousePos.y - point.y);
        if (hovered && distance < 18.0f)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) selectedWorldMap_ = static_cast<int>(index);
        }
        if (selected)
        {
            const float pulse = 13.0f + 2.0f * std::sin(static_cast<float>(ImGui::GetTime()) * 2.0f);
            draw->AddCircle(point, pulse, IM_COL32(227,181,101,120), 32, 1.2f);
            draw->AddLine(ImVec2(point.x - 22,point.y), ImVec2(point.x - 8,point.y), amber);
            draw->AddLine(ImVec2(point.x + 8,point.y), ImVec2(point.x + 22,point.y), amber);
        }
        draw->AddCircleFilled(point, 5.0f, selected ? amber : ink, 16);
        draw->AddCircle(point, 7.0f, ocean, 16, 2.0f);
        const auto count = campaign_.mapSorties.find(map.id);
        const uint64_t sorties = count == campaign_.mapSorties.end() ? 0 : count->second;
        const auto text = fmt::format("{}\n{} DEPLOYMENTS", map.name, sorties);
        const auto textSize = ImGui::CalcTextSize(text.c_str());
        const float labelX = point.x > end.x - textSize.x - 35 ? point.x - textSize.x - 22 : point.x + 22;
        const ImVec2 labelPosition(labelX, point.y + 18);
        draw->AddLine(point, ImVec2(labelX + (labelX < point.x ? textSize.x : 0), labelPosition.y + 7), muted);
        draw->AddRectFilled(ImVec2(labelPosition.x - 7,labelPosition.y - 4),
            ImVec2(labelPosition.x + textSize.x + 7,labelPosition.y + textSize.y + 4), IM_COL32(13,23,26,235), 3);
        Label(draw, labelPosition, text.c_str(), selected ? amber : ink, 0.9f);
    }
    Label(draw, ImVec2(origin.x + 16,origin.y + 14), "FIELD ATLAS  /  WGS84", muted, 0.8f);
    Label(draw, ImVec2(origin.x + 16,end.y - 26), "WHEEL: ZOOM   /   RIGHT DRAG: PAN   /   DOUBLE CLICK: RESET", muted, 0.7f);
    draw->PopClipRect();
    draw->AddRect(origin, end, coast, 5.0f);

    ImGui::Spacing();
    if (!visible.empty())
    {
        const auto& map = *visible[selectedWorldMap_];
        ImGui::TextColored(ImVec4(0.89f,0.71f,0.40f,1), "%s", map.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(" | %s | %.0f x %.0f m", map.region.c_str(), map.sizeM, map.sizeM);
        ImGui::TextDisabled("%.4f N, %.4f E   /   %s", map.latitude, map.longitude, map.terrain.c_str());
        ImGui::TextWrapped("%s", map.description.c_str());
    }
    ImGui::TextDisabled("Made with Natural Earth.");
    ImGui::EndChild();
}
