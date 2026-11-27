#include "Engine/Common/CoreMinimal.hpp"
#include "NextDayzGameInstance.hpp"

#include <sstream>

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include "Application/Game/NextDayz/UI/HUDTheme.hpp"
#include "Application/Game/NextDayz/UI/HUDWidgets.hpp"
#include "Application/Game/NextDayz/World/Economy.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Components/LightComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Platform/UserPaths.hpp"
#include "Engine/Utilities/FileHelper.hpp"

namespace
{
    std::vector<float> ParseNumbers(const std::string& text)
    {
        std::vector<float> values;
        std::istringstream stream(text);
        for (float value = 0.0f; stream >> value;) values.push_back(value);
        return values;
    }

    // SCAD authoring space (Z-up, front -Y) to engine space (Y-up).
    glm::vec3 ScadToEngine(float x, float y, float z) { return {x, z, -y}; }
}

void NextDayzGameInstance::ConfigureCabinCVars(NextCVar::FCVarSystem& cvars)
{
    cvars.RegisterString("nextdayz.cabin.lights", cabinLights_, &cabinLights_, NextCVar::ECVarFlags::None,
        "Cabin lamps in SCAD space, ';' separated: x y z  nx ny nz  size  r g b  intensity (applied on scene load)");
    cvars.RegisterString("nextdayz.cabin.env", cabinEnv_, &cabinEnv_, NextCVar::ECVarFlags::None,
        "Cabin sky and sun: skyIntensity skyRGB  sunIntensity sunRGB  sunRotation sunElevation",
        [this]() { if (cabinScene_ && sceneReady_) ApplyCabinEnvironment(); });
}

void NextDayzGameInstance::InjectCabinLights(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                             std::vector<Assets::Model>& models,
                                             std::vector<Assets::FMaterial>& materials)
{
    std::istringstream lamps(cabinLights_);
    int index = 0;
    for (std::string spec; std::getline(lamps, spec, ';');)
    {
        const auto v = ParseNumbers(spec);
        if (v.size() < 11 || v[6] <= 0.0f || v[10] <= 0.0f) continue;
        const glm::vec3 normal = ScadToEngine(v[3], v[4], v[5]);
        if (glm::length(normal) < 1e-4f) continue;
        const glm::vec3 n = glm::normalize(normal);
        const glm::vec3 helper = std::abs(n.y) > 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const glm::vec3 right = glm::normalize(glm::cross(helper, n)) * v[6];
        const glm::vec3 up = glm::normalize(glm::cross(n, right)) * v[6];
        const uint32_t material = Assets::SceneBuilder::AddDiffuseLightMaterial(materials, {v[7], v[8], v[9]}, v[10]);
        const std::string name = fmt::format("nd_cabin_lamp_{}", index++);
        std::vector<Assets::LightObject> created;
        models.push_back(Assets::FProcModel::CreateAreaLight(name, -0.5f * (right + up), right, up, material, created));
        if (created.empty()) continue;
        auto node = Assets::SceneBuilder::CreateRenderNode(name, ScadToEngine(v[0], v[1], v[2]), glm::vec3(1.0f),
            static_cast<uint32_t>(nodes.size()), static_cast<uint32_t>(models.size() - 1), material, true);
        if (auto render = node->GetComponent<Runtime::RenderComponent>()) render->SetCastShadows(false);
        node->AddComponent(std::make_shared<Runtime::LightComponent>(created.front()));
        nodes.push_back(node);
    }
}

void NextDayzGameInstance::ApplyCabinEnvironment()
{
    auto& env = GetEngine().GetScene().GetEnvSettings();
    env.AtmosphereEnabled = false;
    env.AerialPerspectiveEnabled = false;
    env.HeightFogEnabled = false;
    const auto v = ParseNumbers(cabinEnv_);
    if (v.size() < 10) return;
    env.HasSky = v[0] > 0.0f;
    env.SkyIntensity = v[0];
    env.SkyColor = glm::vec3(v[1], v[2], v[3]);
    env.HasSun = v[4] > 0.0f;
    env.SunIntensity = v[4];
    env.SunColor = glm::vec3(v[5], v[6], v[7]);
    env.SunRotation = v[8];
    env.SunElevation = v[9];
}

void NextDayzGameInstance::BindCabinCamera()
{
    // Fallback matches the bench if the set ships without a marker.
    cabinView_ = glm::lookAt(glm::vec3(0.16f, 1.28f, 2.70f), glm::vec3(0.0f, 0.94f, -0.20f), glm::vec3(0, 1, 0));
    cabinFov_ = 48.0f;
    cabinAperture_ = cabinFocal_ = 0.0f;
    for (const Assets::Camera& camera : GetEngine().GetScene().GetEnvSettings().cameras)
    {
        if (camera.name != "Cabin") continue;
        cabinView_ = camera.ModelView;
        cabinFov_ = camera.FieldOfView;
        cabinAperture_ = camera.Aperture;
        cabinFocal_ = camera.FocalDistance;
        return;
    }
    SPDLOG_WARN("[NextDayz] cabin scene has no 'Cabin' camera; using the default shot");
}

void NextDayzGameInstance::RequestCabin()
{
    if (loadingCabin_) return;
    if (CampaignEnabled())
    {
        if (!SaveCampaign()) return;
        pendingCampaign_ = CaptureCampaign();
    }
    loadingCabin_ = true;
    safehouseTab_ = -1;
    SetMouseCaptured(false);
    GetEngine().RequestLoadScene({.filename = cabinScenePath});
}

void NextDayzGameInstance::InitializeCabinCampaign()
{
    LoadEconomy();
    std::string error;
    if (!NextDayz::LoadExtractionConfig(Utilities::FileHelper::ResolvePlatformFilePath(
            "assets/configs/nextdayz_extraction.json"), extractionConfig_, error))
        SPDLOG_ERROR("[NextDayz] extraction config: {}; using defaults", error);
    campaignPath_ = GOption->AgentValidation ? std::filesystem::path(validationCampaignPath_)
        : NextPlatform::UserPaths::EnsureUserFile("NextDayz", "campaign/save.json");
    campaignSaveBlocked_ = false;
    saveError_.clear();
    const bool fresh = GOption->AgentValidation && std::exchange(validationCampaignFresh_, false);
    std::error_code ec;
    const bool exists = !fresh && (std::filesystem::exists(campaignPath_, ec) ||
        std::filesystem::exists(campaignPath_.string() + ".bak", ec));
    if (pendingCampaign_)
        cabinCampaign_ = std::exchange(pendingCampaign_, std::nullopt);
    else if (exists)
    {
        NextDayz::FCampaignSave saved;
        if (!NextDayz::ReadCampaignSave(campaignPath_, saved, error))
        {
            campaignSaveBlocked_ = true;
            saveError_ = error;
            campaignMessage_ = NextDayz::L10n::Message("Cannot load campaign. Original save preserved. {}", error);
            return;
        }
        cabinCampaign_ = std::move(saved);
    }
    else
    {
        cabinCampaign_.emplace();
        inventory_.Clear();
        survival_.Reset();
        cabinCampaign_->inventory = inventory_.Capture();
        cabinCampaign_->survival = survival_.Snapshot();
        cabinCampaign_->sessionRng = sessionRng_.State();
        cabinCampaign_->survivalRng = survivalRng_.State();
        cabinCampaign_->map = campaignMap_;
        if (const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_, campaignMap_))
            cabinCampaign_->mapVersion = map->version;
    }
    const bool releasedTraining = NextDayz::ReleaseTrainingDeployment(*cabinCampaign_, expeditionMaps_);
    if (releasedTraining)
    {
        campaignMessage_ = "Training is practice only. Your survivor is back in the safehouse with the gear they carried.";
        SPDLOG_INFO("[NextDayz] campaign was deployed into the training ground; returned it to the safehouse");
    }
    const auto& saved = *cabinCampaign_;
    campaignMap_ = saved.map;
    campaign_ = saved.campaign;
    mapStates_ = saved.maps;
    inventory_.Clear();
    if (!inventory_.Restore(saved.inventory))
    {
        campaignSaveBlocked_ = true;
        saveError_ = "Cannot restore carried gear";
        campaignMessage_ = NextDayz::L10n::Message("Cannot load campaign. Original save preserved. {}", saveError_);
        return;
    }
    survival_.Restore(saved.survival);
    weapons_.ResetRuntime();
    for (int slot = 0; slot < NextDayz::kWeaponSlotCount; ++slot)
        if (saved.weaponSlots[slot]) weapons_.EquipInstance(slot, inventory_, saved.weaponSlots[slot]);
    weapons_.RestoreSelection(saved.activeSlot, saved.previousSlot);
    if (saved.sessionRng) sessionRng_.RestoreState(saved.sessionRng);
    if (saved.survivalRng) survivalRng_.RestoreState(saved.survivalRng);
    showInventory_ = showMap_ = paused_ = false;
    if (campaignMessage_.empty()) campaignMessage_ = campaign_.inField
        ? "Your expedition is saved. Continue when ready."
        : "Prepare your equipment. The helicopter is ready when you are.";
    // World, clock and survivor coordinates stay in cabinCampaign_. Merely
    // opening the lobby must never replace them with the cabin's empty world.
    const bool boardChanged = !campaignSaveBlocked_ && RefreshContractBoard();
    if ((!exists && !fresh) || releasedTraining || boardChanged) SaveCampaign();
}

void NextDayzGameInstance::TickCabin(float dt)
{
    cabinSeconds_ += std::min(dt, 0.1f);
    rig_.SetVisible(true);
    rig_.UpdateSeated(glm::vec3(0.0f, 0.0f, -0.25f), LocalPresentation(), dt);
}

void NextDayzGameInstance::DrawCabinMenu(const std::string& status, bool canDeploy, bool resume, bool& deploy)
{
    using namespace NextDayz::L10n;
    using namespace NextDayz::UITheme;
    using namespace NextDayz::UIWidgets;

    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->WorkPos;
    const ImVec2 size = viewport->WorkSize;
    const ImVec2 bottom(origin.x + size.x, origin.y + size.y);
    const float margin = std::clamp(size.x * 0.028f, 20.0f, 44.0f);
    constexpr float navH = 56.0f;
    const ImGuiWindowFlags bareFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar;

    if (!settingsOpen_ && !sessionMenuOpen_ && !debrief_.open && !ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            if (safehouseTab_ >= 0) safehouseTab_ = -1;
            else OpenSettings();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_M, false)) safehouseTab_ = safehouseTab_ == 0 ? -1 : 0;
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) safehouseTab_ = safehouseTab_ == 1 ? -1 : 1;
        if (ImGui::IsKeyPressed(ImGuiKey_T, false)) safehouseTab_ = safehouseTab_ == 2 ? -1 : 2;
    }

    auto* bg = ImGui::GetBackgroundDrawList();
    // Top subtle gradient bar + bottom legibility vignette; keep center/right 3D survivor open.
    bg->AddRectFilledMultiColor(origin, ImVec2(bottom.x, origin.y + 110.0f),
        IM_COL32(10, 12, 11, 228), IM_COL32(10, 12, 11, 228), IM_COL32(10, 12, 11, 0), IM_COL32(10, 12, 11, 0));
    bg->AddRectFilledMultiColor(ImVec2(origin.x, bottom.y - 190.0f), bottom,
        IM_COL32(10, 12, 11, 0), IM_COL32(10, 12, 11, 0), IM_COL32(10, 12, 11, 232), IM_COL32(10, 12, 11, 232));
    bg->AddLine(ImVec2(origin.x, origin.y + navH), ImVec2(bottom.x, origin.y + navH), Colors::BorderMuted, 1.0f);

    FFieldStyleScope style;

    // ---- 1. UNIFIED TOP NAVIGATION BAR (56px) ----
    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(ImVec2(size.x, navH + 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##cabin_topbar", nullptr, bareFlags);
    {
        auto* draw = ImGui::GetWindowDrawList();
        // Brand wordmark on far left: amber vertical bar + NEXTDAYZ
        const float logoX = origin.x + margin;
        draw->AddRectFilled(ImVec2(logoX, origin.y + 14.0f), ImVec2(logoX + 4.0f, origin.y + 42.0f), Colors::AccentAmber);
        ImFont* brandFont = inventoryTitleFont_ ? inventoryTitleFont_ : ImGui::GetFont();
        const float brandSize = inventoryTitleFont_ ? 26.0f : 22.0f;
        const ImVec2 nextSz = brandFont->CalcTextSizeA(brandSize, FLT_MAX, 0.0f, "NEXT");
        const float brandY = origin.y + (navH - nextSz.y) * 0.5f;
        draw->AddText(brandFont, brandSize, ImVec2(logoX + 12.0f, brandY), Colors::TextPrimary, "NEXT");
        draw->AddText(brandFont, brandSize, ImVec2(logoX + 12.0f + nextSz.x, brandY), Colors::AccentAmber, "DAYZ");

        // 4 Primary Game Channel Tabs
        ImGui::SetCursorScreenPos(ImVec2(logoX + 168.0f, origin.y));
        if (NavTabButton(Tr("Cabin"), nullptr, safehouseTab_ == -1, navH)) safehouseTab_ = -1;
        ImGui::SameLine(0.0f, 4.0f);
        if (NavTabButton(Tr("Operations"), "M", safehouseTab_ == 0, navH)) safehouseTab_ = safehouseTab_ == 0 ? -1 : 0;
        ImGui::SameLine(0.0f, 4.0f);
        if (NavTabButton(Tr("Loadout"), "TAB", safehouseTab_ == 1, navH)) safehouseTab_ = safehouseTab_ == 1 ? -1 : 1;
        ImGui::SameLine(0.0f, 4.0f);
        if (NavTabButton(Tr("Quartermaster"), "T", safehouseTab_ == 2, navH)) safehouseTab_ = safehouseTab_ == 2 ? -1 : 2;

        // Right cluster: Callsign + Gold + Tier + Mode Pill + Settings
        const float settingsW = 92.0f;
        const float modeW = 178.0f;
        const float rightEnd = bottom.x - margin;
        const float settingsX = rightEnd - settingsW;
        const float modeX = settingsX - 10.0f - modeW;

        ImGui::SetCursorScreenPos(ImVec2(modeX, origin.y + 11.0f));
        const std::string modeBtnLabel = std::string(Tr(SessionModeLabel())) + "##mode_pill";
        if (GhostButton(modeBtnLabel.c_str(), ImVec2(modeW, 34.0f), sessionMenuOpen_)) OpenSessionMenu();
        // Green status dot inside mode pill
        draw->AddCircleFilled(ImVec2(modeX + 10.0f, origin.y + 28.0f), 3.2f,
                              (netMode_ == ENetMode::Offline || NetLoggedIn()) ? Colors::AccentEmerald : Colors::AccentAmber, 12);

        ImGui::SetCursorScreenPos(ImVec2(settingsX, origin.y + 11.0f));
        if (GhostButton(Label("SETTINGS"), ImVec2(settingsW, 34.0f), settingsOpen_)) OpenSettings();

        // Compact resource summary to the left of the mode pill
        if (CampaignEnabled())
        {
            const uint32_t tier = NextDayz::ReputationTier(campaign_.reputation, economy_);
            const std::string tierStr = fmt::format("T{}", tier);
            const std::string goldStr = fmt::format("{} {}", Tr("Gold"), campaign_.gold);
            const ImVec2 tSz = ImGui::CalcTextSize(tierStr.c_str());
            const ImVec2 gSz = ImGui::CalcTextSize(goldStr.c_str());
            const float badgeX = modeX - 18.0f - (tSz.x + 14.0f);
            const float textY = origin.y + (navH - gSz.y) * 0.5f;

            draw->AddRectFilled(ImVec2(badgeX, origin.y + 16.0f), ImVec2(badgeX + tSz.x + 14.0f, origin.y + 40.0f),
                                IM_COL32(111, 166, 107, 42), 2.0f);
            draw->AddRect(ImVec2(badgeX, origin.y + 16.0f), ImVec2(badgeX + tSz.x + 14.0f, origin.y + 40.0f),
                          IM_COL32(111, 166, 107, 120), 2.0f);
            draw->AddText(ImVec2(badgeX + 7.0f, textY), Colors::AccentEmerald, tierStr.c_str());
            draw->AddText(ImVec2(badgeX - 14.0f - gSz.x, textY), Colors::AccentAmber, goldStr.c_str());
        }
        else if (NetLoggedIn())
        {
            const std::string acct = NetAccountName();
            const ImVec2 aSz = ImGui::CalcTextSize(acct.c_str());
            draw->AddText(ImVec2(modeX - 18.0f - aSz.x, origin.y + (navH - aSz.y) * 0.5f),
                          Colors::TextPrimary, acct.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // When a sub-tab is open, the top nav stays live and BeginCabinPanel owns the body below.
    if (safehouseTab_ >= 0) return;

    // ---- 2. CABIN HOME: BOTTOM-LEFT ASSIGNMENT BRIEFING CARD ----
    const float cardW = std::clamp(size.x * 0.25f, 300.0f, 380.0f);
    const float cardH = 196.0f;
    const ImVec2 cardPos(origin.x + margin, bottom.y - margin - cardH);
    ImGui::SetNextWindowPos(cardPos);
    ImGui::SetNextWindowSize(ImVec2(cardW, cardH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    ImGui::Begin("##cabin_briefing", nullptr, bareFlags);
    {
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 q(p.x + cardW, p.y + 138.0f);
        DrawFieldCard(draw, p, q, Colors::AccentAmber);

        ImGui::TextColored(ColorVec4(Colors::AccentAmber), "%s", Tr("ACTIVE ASSIGNMENT"));
        const auto* dest = CampaignEnabled() ? ChosenDeployMap() : nullptr;
        const std::string destName = dest ? std::string(Tr(dest->name))
            : (CampaignEnabled() ? std::string(Tr("RANDOM ASSIGNMENT")) : std::string(Tr(SessionModeLabel())));
        ImGui::PushFont(inventoryTitleFont_ ? inventoryTitleFont_ : ImGui::GetFont());
        ImGui::TextUnformatted(destName.c_str());
        ImGui::PopFont();
        if (dest)
        {
            ImGui::TextColored(ColorVec4(Colors::TextSecondary), "%s  ·  %.0fm", Tr(dest->region), dest->sizeM);
        }
        else if (!status.empty())
        {
            const auto firstLine = status.substr(0, status.find('\n'));
            ImGui::TextColored(ColorVec4(Colors::TextSecondary), "%s", firstLine.c_str());
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ColorVec4(Colors::TextSecondary), "%s",
                           Format("Carried: {} items", inventory_.Instances().size()).c_str());

        // Secondary compact contracts / message strip below the main card
        const ImVec2 p2(p.x, q.y + 8.0f);
        const ImVec2 q2(p.x + cardW, p.y + cardH);
        DrawFieldCard(draw, p2, q2, Colors::AccentEmerald);
        ImGui::SetCursorScreenPos(ImVec2(p2.x + 14.0f, p2.y + 12.0f));
        if (CampaignEnabled() && !campaign_.contracts.empty())
        {
            ImGui::TextColored(ColorVec4(Colors::TextHighlight), "%s",
                               Format("Active contracts: {}", campaign_.contracts.size()).c_str());
        }
        else
        {
            std::string note = status;
            if (const size_t nl = note.find('\n'); nl != std::string::npos) note = note.substr(nl + 1);
            if (note.empty()) note = Tr("Equipment changes appear on your survivor.");
            ImGui::TextColored(ColorVec4(Colors::TextSecondary), "%s", note.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // ---- 3. CABIN HOME: BOTTOM-RIGHT HERO DEPLOY ACTION ZONE ----
    const float ctaW = std::clamp(size.x * 0.22f, 270.0f, 340.0f);
    const float ctaH = 136.0f;
    ImGui::SetNextWindowPos(ImVec2(bottom.x - margin - ctaW, bottom.y - margin - ctaH));
    ImGui::SetNextWindowSize(ImVec2(ctaW, ctaH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    ImGui::Begin("##cabin_deploy_cta", nullptr, bareFlags);
    {
        const std::string subHint = CampaignEnabled()
            ? (campaign_.sortie.insertionMap.empty() ? std::string(Tr("Random landing (free)"))
                                                     : Format("Landing: {}", campaign_.sortie.insertionMap))
            : Localize(NetLobbyStatus(), Language());
        {
            const ImVec2 tp = ImGui::GetCursorScreenPos();
            DrawTextShadow(ImGui::GetWindowDrawList(), tp, Colors::TextPrimary, subHint.c_str());
            ImGui::Dummy(ImVec2(ctaW, ImGui::GetTextLineHeight() + 2.0f));
        }

        if (PrimaryButton(Label(resume ? "CONTINUE EXPEDITION" : "DEPLOY"), ImVec2(-1.0f, 52.0f), canDeploy))
            deploy = true;

        const float halfW = (ctaW - 8.0f) * 0.5f;
        if (GhostButton(Label("Loadout"), ImVec2(halfW, 36.0f))) safehouseTab_ = 1;
        ImGui::SameLine();
        if (GhostButton(Label("Training"), ImVec2(halfW, 36.0f))) StartTraining();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void NextDayzGameInstance::BeginCabinPanel(const char* title)
{
    using namespace NextDayz::UITheme;
    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->WorkPos;
    const ImVec2 size = viewport->WorkSize;
    const float margin = std::clamp(size.x * 0.028f, 18.0f, 44.0f);
    constexpr float topOffset = 66.0f;

    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(origin.x, origin.y + 56.0f), ImVec2(origin.x + size.x, origin.y + size.y), Colors::BgScrim);

    ImGui::SetNextWindowPos(ImVec2(origin.x + margin, origin.y + topOffset));
    ImGui::SetNextWindowSize(ImVec2(size.x - 2.0f * margin, std::max(260.0f, size.y - topOffset - 18.0f)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ColorVec4(Colors::SurfacePanel));
    ImGui::PushStyleColor(ImGuiCol_Border, ColorVec4(Colors::BorderMuted));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f);
    ImGui::Begin("##cabin_panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings);

    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    draw->AddRectFilled(p, ImVec2(p.x + 3.0f, p.y + 26.0f), Colors::AccentAmber);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
    ImGui::PushFont(inventoryTitleFont_ ? inventoryTitleFont_ : ImGui::GetFont());
    ImGui::TextUnformatted(NextDayz::L10n::Tr(title));
    ImGui::PopFont();
    ImGui::SameLine(std::max(220.0f, w - 140.0f));
    if (NextDayz::UIWidgets::GhostButton(NextDayz::L10n::Label("Back to cabin"), ImVec2(138.0f, 30.0f)))
        safehouseTab_ = -1;
    ImGui::Separator();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void NextDayzGameInstance::DrawDebriefOverlay()
{
    using namespace NextDayz::L10n;
    using namespace NextDayz::UITheme;
    using namespace NextDayz::UIWidgets;
    if (!debrief_.open) return;

    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->WorkPos;
    const ImVec2 size = viewport->WorkSize;
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        origin, ImVec2(origin.x + size.x, origin.y + size.y),
        debrief_.extracted ? IM_COL32(10, 14, 11, 215) : IM_COL32(24, 9, 9, 218));

    FFieldStyleScope style;
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(560.0f, size.x - 40.0f), 0.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
    ImGui::Begin("##nd_debrief", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);

    const ImVec2 pMin = ImGui::GetWindowPos();
    const ImVec2 pMax(pMin.x + ImGui::GetWindowSize().x, pMin.y + ImGui::GetWindowSize().y);
    const ImU32 accent = debrief_.extracted ? Colors::AccentEmerald : Colors::AccentRose;
    ImGui::GetWindowDrawList()->AddRectFilled(pMin, ImVec2(pMax.x, pMin.y + 3.0f), accent);

    ImGui::TextColored(ColorVec4(accent), "%s", Tr(debrief_.extracted ? "EXTRACTION COMPLETE" : "KILLED IN ACTION"));
    ImGui::PushFont(inventoryTitleFont_ ? inventoryTitleFont_ : ImGui::GetFont());
    ImGui::TextUnformatted(debrief_.mapName.empty() ? Tr("SAFEHOUSE") : debrief_.mapName.c_str());
    ImGui::PopFont();
    ImGui::TextColored(ColorVec4(Colors::TextSecondary), "%s",
        Tr(debrief_.extracted ? "Everything you carried has been stored in your safehouse."
                              : "Your carried gear remains where you fell. Storage and gold are safe."));
    ImGui::Spacing();

    if (ImGui::BeginTable("##debrief_stats", 3, ImGuiTableFlags_SizingStretchSame))
    {
        const auto tile = [](const char* label, const std::string& value, ImU32 col)
        {
            ImGui::TableNextColumn();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            DrawFieldCard(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + 56.0f), col);
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 12.0f, p.y + 8.0f), Colors::TextMuted, label);
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 12.0f, p.y + 28.0f), Colors::TextPrimary, value.c_str());
            ImGui::Dummy(ImVec2(w, 56.0f));
        };
        char timeBuf[32];
        std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d",
                      static_cast<int>(debrief_.survivalSeconds) / 60,
                      static_cast<int>(debrief_.survivalSeconds) % 60);
        tile(Tr("Survival time"), timeBuf, Colors::AccentAmber);
        if (debrief_.extracted)
            tile(Tr("Extraction value"), fmt::format("+{}", debrief_.extractedValue), Colors::AccentEmerald);
        else
            tile(Tr("Cause of death"), debrief_.deathCause.empty() ? std::string(Tr("Unknown")) : std::string(Tr(debrief_.deathCause)), Colors::AccentRose);
        tile(Tr("Infected eliminated"), std::to_string(debrief_.kills), Colors::TextSecondary);
        ImGui::EndTable();
    }

    if (debrief_.contractsCompleted > 0)
    {
        ImGui::Spacing();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        DrawFieldCard(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + 44.0f), Colors::AccentEmerald);
        const std::string line = Format("Last report: contracts completed: {} (+{} gold, reputation {:+}).",
            debrief_.contractsCompleted, debrief_.contractGold, debrief_.contractReputation);
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 14.0f, p.y + 13.0f), Colors::TextHighlight, line.c_str());
        ImGui::Dummy(ImVec2(w, 44.0f));
    }

    ImGui::Spacing();
    if (PrimaryButton(Label("Return to safehouse"), ImVec2(-1.0f, 44.0f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        debrief_.open = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
