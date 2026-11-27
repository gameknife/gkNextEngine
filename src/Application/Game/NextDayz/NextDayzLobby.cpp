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
    const float margin = std::clamp(size.x * 0.028f, S(22.0f), S(46.0f));
    const float navH = S(66.0f);
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
    // Top soft gradient bar + bottom legibility vignette; keep center/right 3D survivor unobstructed.
    bg->AddRectFilledMultiColor(origin, ImVec2(bottom.x, origin.y + S(128.0f)),
        IM_COL32(10, 12, 11, 232), IM_COL32(10, 12, 11, 232), IM_COL32(10, 12, 11, 0), IM_COL32(10, 12, 11, 0));
    bg->AddRectFilledMultiColor(ImVec2(origin.x, bottom.y - S(220.0f)), bottom,
        IM_COL32(10, 12, 11, 0), IM_COL32(10, 12, 11, 0), IM_COL32(10, 12, 11, 236), IM_COL32(10, 12, 11, 236));
    bg->AddLine(ImVec2(origin.x, origin.y + navH), ImVec2(bottom.x, origin.y + navH), Colors::BorderMuted, 1.2f);

    FFieldStyleScope style;

    // ---- 1. UNIFIED TOP NAVIGATION BAR (Mockup 01 & 04) ----
    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(ImVec2(size.x, navH + 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##cabin_topbar", nullptr, bareFlags);
    {
        auto* draw = ImGui::GetWindowDrawList();
        // Brand wordmark on far left: thick rounded amber bar + bold condensed NEXTDAYZ
        const float logoX = origin.x + margin;
        const float barW = S(5.5f);
        const float barH = S(34.0f);
        draw->AddRectFilled(ImVec2(logoX, origin.y + (navH - barH) * 0.5f),
                            ImVec2(logoX + barW, origin.y + (navH + barH) * 0.5f),
                            Colors::AccentAmber, S(2.0f));
        ImFont* brandFont = NumeralFont();
        const float brandSize = S(33.0f);
        const ImVec2 nextSz = brandFont->CalcTextSizeA(brandSize, FLT_MAX, 0.0f, "NEXT");
        const ImVec2 dayzSz = brandFont->CalcTextSizeA(brandSize, FLT_MAX, 0.0f, "DAYZ");
        const float brandY = origin.y + (navH - nextSz.y) * 0.5f;
        draw->AddText(brandFont, brandSize, ImVec2(logoX + barW + S(12.0f), brandY), Colors::TextPrimary, "NEXT");
        draw->AddText(brandFont, brandSize, ImVec2(logoX + barW + S(12.0f) + nextSz.x, brandY), Colors::AccentAmber, "DAYZ");

        // 4 Primary Game Channel Tabs
        const float tabsX = logoX + barW + S(12.0f) + nextSz.x + dayzSz.x + S(38.0f);
        ImGui::SetCursorScreenPos(ImVec2(tabsX, origin.y));
        if (NavTabButton(Tr("Cabin"), nullptr, safehouseTab_ == -1, navH)) safehouseTab_ = -1;
        ImGui::SameLine(0.0f, S(4.0f));
        if (NavTabButton(Tr("Operations"), "M", safehouseTab_ == 0, navH)) safehouseTab_ = safehouseTab_ == 0 ? -1 : 0;
        ImGui::SameLine(0.0f, S(4.0f));
        if (NavTabButton(Tr("Loadout"), "TAB", safehouseTab_ == 1, navH)) safehouseTab_ = safehouseTab_ == 1 ? -1 : 1;
        ImGui::SameLine(0.0f, S(4.0f));
        if (NavTabButton(Tr("Quartermaster"), "T", safehouseTab_ == 2, navH)) safehouseTab_ = safehouseTab_ == 2 ? -1 : 2;

        // Right cluster: Gold coin + bold value + Tier chip + Mode Pill + Crisp Gear Settings button
        const float pillH = S(38.0f);
        const float pillY = origin.y + (navH - pillH) * 0.5f;
        const float settingsW = pillH;
        const float modeW = S(168.0f);
        const float rightEnd = bottom.x - margin;
        const float settingsX = rightEnd - settingsW;
        const float modeX = settingsX - S(10.0f) - modeW;

        ImGui::SetCursorScreenPos(ImVec2(modeX, pillY));
        const std::string modeBtnLabel = std::string("   ") + Tr(SessionModeLabel()) + "##mode_pill";
        if (GhostButton(modeBtnLabel.c_str(), ImVec2(modeW, pillH), sessionMenuOpen_)) OpenSessionMenu();
        // Green status dot inside mode pill
        draw->AddCircleFilled(ImVec2(modeX + S(14.0f), pillY + pillH * 0.5f), S(4.0f),
                              (netMode_ == ENetMode::Offline || NetLoggedIn()) ? Colors::AccentEmerald : Colors::AccentAmber, 14);

        // Flat-stylized gear button for Settings (matches Mockup 01 & 04)
        ImGui::SetCursorScreenPos(ImVec2(settingsX, pillY));
        if (GhostButton("##settings_gear", ImVec2(settingsW, pillH), settingsOpen_)) OpenSettings();
        DrawVitalIcon(draw, ImVec2(settingsX + settingsW * 0.5f, pillY + pillH * 0.5f), S(20.0f),
                      EVitalIcon::Gear, settingsOpen_ ? Colors::AccentAmber : Colors::TextSecondary);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr("SETTINGS"));

        // Compact resource summary to the left of the mode pill
        ImFont* boldFont = BoldFont();
        const float statSz = S(18.0f);
        if (CampaignEnabled())
        {
            const uint32_t tier = NextDayz::ReputationTier(campaign_.reputation, economy_);
            const std::string tierStr = fmt::format("T{}", tier);
            const std::string goldStr = fmt::format("{}", campaign_.gold);
            const ImVec2 tSz = boldFont->CalcTextSizeA(S(16.0f), FLT_MAX, 0.0f, tierStr.c_str());
            const ImVec2 gSz = boldFont->CalcTextSizeA(statSz, FLT_MAX, 0.0f, goldStr.c_str());
            const float tierW = tSz.x + S(16.0f);
            const float tierH = S(28.0f);
            const float badgeX = modeX - S(16.0f) - tierW;
            const float badgeY = origin.y + (navH - tierH) * 0.5f;

            draw->AddRectFilled(ImVec2(badgeX, badgeY), ImVec2(badgeX + tierW, badgeY + tierH),
                                IM_COL32(116, 179, 110, 48), S(5.0f));
            draw->AddRect(ImVec2(badgeX, badgeY), ImVec2(badgeX + tierW, badgeY + tierH),
                          IM_COL32(116, 179, 110, 140), S(5.0f), 0, 1.3f);
            draw->AddText(boldFont, S(16.0f), ImVec2(badgeX + S(8.0f), badgeY + (tierH - tSz.y) * 0.5f),
                          Colors::AccentEmerald, tierStr.c_str());

            const float goldX = badgeX - S(16.0f) - gSz.x;
            const float goldY = origin.y + (navH - gSz.y) * 0.5f;
            draw->AddText(boldFont, statSz, ImVec2(goldX, goldY), Colors::AccentAmber, goldStr.c_str());
            DrawVitalIcon(draw, ImVec2(goldX - S(14.0f), origin.y + navH * 0.5f), S(15.0f),
                          EVitalIcon::Coin, Colors::AccentAmber);
        }
        else if (NetLoggedIn())
        {
            const std::string acct = NetAccountName();
            const ImVec2 aSz = boldFont->CalcTextSizeA(statSz, FLT_MAX, 0.0f, acct.c_str());
            draw->AddText(boldFont, statSz, ImVec2(modeX - S(18.0f) - aSz.x, origin.y + (navH - aSz.y) * 0.5f),
                          Colors::TextPrimary, acct.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // When a sub-tab is open, the top nav stays live and BeginCabinPanel owns the body below.
    if (safehouseTab_ >= 0) return;

    // ---- 2. CABIN HOME: BOTTOM-LEFT ASSIGNMENT BRIEFING CARD (Mockup 01) ----
    const float cardW = std::clamp(size.x * 0.28f, S(350.0f), S(460.0f));
    const float mainCardH = S(170.0f);
    const float stripH = S(48.0f);
    const float stripGap = S(10.0f);
    const float totalCardH = mainCardH + stripGap + stripH;
    const ImVec2 cardPos(origin.x + margin, bottom.y - margin - totalCardH);
    ImGui::SetNextWindowPos(cardPos);
    ImGui::SetNextWindowSize(ImVec2(cardW, totalCardH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20.0f), S(16.0f)));
    ImGui::Begin("##cabin_briefing", nullptr, bareFlags);
    {
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 q(p.x + cardW, p.y + mainCardH);
        DrawFieldCard(draw, p, q, Colors::AccentAmber);

        ImGui::PushFont(BoldFont(), S(14.5f));
        ImGui::TextColored(ColorVec4(Colors::AccentAmber), "%s", Tr("ACTIVE ASSIGNMENT"));
        ImGui::PopFont();

        const auto* dest = CampaignEnabled() ? ChosenDeployMap() : nullptr;
        const std::string destName = dest ? std::string(Tr(dest->name))
            : (CampaignEnabled() ? std::string(Tr("RANDOM ASSIGNMENT")) : std::string(Tr(SessionModeLabel())));
        ImGui::PushFont(HeadingFont(), S(31.0f));
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

        const float sepY = q.y - S(42.0f);
        draw->AddLine(ImVec2(p.x + S(18.0f), sepY), ImVec2(q.x - S(18.0f), sepY), Colors::BorderMuted, 1.0f);
        ImGui::SetCursorScreenPos(ImVec2(p.x + S(20.0f), sepY + S(10.0f)));
        ImGui::PushFont(BoldFont(), S(16.5f));
        ImGui::TextColored(ColorVec4(Colors::TextPrimary), "%s",
                           Format("Carried: {} items", inventory_.Instances().size()).c_str());
        ImGui::PopFont();

        // Secondary rounded contracts / message pill below the main card
        const ImVec2 p2(p.x, q.y + stripGap);
        const ImVec2 q2(p.x + cardW, p.y + totalCardH);
        DrawFieldCard(draw, p2, q2, 0);
        const float dotY = (p2.y + q2.y) * 0.5f;
        const bool hasContracts = CampaignEnabled() && !campaign_.contracts.empty();
        draw->AddCircleFilled(ImVec2(p2.x + S(18.0f), dotY), S(4.5f),
                              hasContracts ? Colors::AccentAmber : Colors::AccentEmerald, 12);
        const std::string noteText = hasContracts
            ? Format("Active contracts: {}", campaign_.contracts.size())
            : [&]()
            {
                std::string note = status;
                if (const size_t nl = note.find('\n'); nl != std::string::npos) note = note.substr(nl + 1);
                return note.empty() ? std::string(Tr("Equipment changes appear on your survivor.")) : note;
            }();
        ImFont* boldFont = BoldFont();
        const float noteSz = S(16.0f);
        const ImVec2 nSz = boldFont->CalcTextSizeA(noteSz, FLT_MAX, 0.0f, noteText.c_str());
        draw->AddText(boldFont, noteSz, ImVec2(p2.x + S(32.0f), dotY - nSz.y * 0.5f),
                      hasContracts ? Colors::TextHighlight : Colors::TextSecondary, noteText.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // ---- 3. CABIN HOME: BOTTOM-RIGHT HERO DEPLOY ACTION ZONE (Mockup 01) ----
    const float ctaW = std::clamp(size.x * 0.24f, S(310.0f), S(400.0f));
    const float ctaBtnH = S(62.0f);
    const float subBtnH = S(42.0f);
    const float ctaH = S(28.0f) + ctaBtnH + S(10.0f) + subBtnH + S(12.0f);
    ImGui::SetNextWindowPos(ImVec2(bottom.x - margin - ctaW, bottom.y - margin - ctaH));
    ImGui::SetNextWindowSize(ImVec2(ctaW, ctaH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10.0f), S(10.0f)));
    ImGui::Begin("##cabin_deploy_cta", nullptr, bareFlags);
    {
        const std::string subHint = CampaignEnabled()
            ? (campaign_.sortie.insertionMap.empty() ? std::string(Tr("Random landing (free)"))
                                                     : Format("Landing: {}", campaign_.sortie.insertionMap))
            : Localize(NetLobbyStatus(), Language());
        {
            ImFont* boldFont = BoldFont();
            const float hintSz = S(16.5f);
            const ImVec2 hSz = boldFont->CalcTextSizeA(hintSz, FLT_MAX, 0.0f, subHint.c_str());
            const ImVec2 tp = ImGui::GetCursorScreenPos();
            DrawTextShadow(ImGui::GetWindowDrawList(), boldFont, hintSz,
                           ImVec2(tp.x + std::max(0.0f, ctaW - hSz.x), tp.y), Colors::TextPrimary, subHint.c_str(), 1.6f);
            ImGui::Dummy(ImVec2(ctaW, hSz.y + S(4.0f)));
        }

        if (PrimaryButton(Label(resume ? "CONTINUE EXPEDITION" : "DEPLOY"), ImVec2(-1.0f, ctaBtnH), canDeploy, true))
            deploy = true;

        const float halfW = (ctaW - S(10.0f)) * 0.5f;
        if (GhostButton(Label("Loadout"), ImVec2(halfW, subBtnH))) safehouseTab_ = 1;
        ImGui::SameLine();
        if (GhostButton(Label("Training"), ImVec2(halfW, subBtnH))) StartTraining();
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
    const float margin = std::clamp(size.x * 0.028f, S(20.0f), S(46.0f));
    const float navH = S(66.0f);
    const float topOffset = navH + S(12.0f);

    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(origin.x, origin.y + navH), ImVec2(origin.x + size.x, origin.y + size.y), Colors::BgScrim);

    ImGui::SetNextWindowPos(ImVec2(origin.x + margin, origin.y + topOffset));
    ImGui::SetNextWindowSize(ImVec2(size.x - 2.0f * margin, std::max(S(300.0f), size.y - topOffset - S(20.0f))));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ColorVec4(Colors::SurfacePanel));
    ImGui::PushStyleColor(ImGuiCol_Border, ColorVec4(Colors::BorderTactical));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22.0f), S(18.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(8.0f));
    ImGui::Begin("##cabin_panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings);

    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    draw->AddRectFilled(p, ImVec2(p.x + S(5.0f), p.y + S(32.0f)), Colors::AccentAmber, S(2.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + S(14.0f));
    ImGui::PushFont(HeadingFont(), S(30.0f));
    ImGui::TextUnformatted(NextDayz::L10n::Tr(title));
    ImGui::PopFont();
    const float backW = S(148.0f);
    ImGui::SameLine(std::max(S(240.0f), w - backW));
    if (NextDayz::UIWidgets::GhostButton(NextDayz::L10n::Label("Back to cabin"), ImVec2(backW, S(36.0f))))
        safehouseTab_ = -1;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
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
    ImGui::SetNextWindowSize(ImVec2(std::min(S(620.0f), size.x - S(40.0f)), 0.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(30.0f), S(26.0f)));
    ImGui::Begin("##nd_debrief", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);

    const ImVec2 pMin = ImGui::GetWindowPos();
    const ImVec2 pMax(pMin.x + ImGui::GetWindowSize().x, pMin.y + ImGui::GetWindowSize().y);
    const ImU32 accent = debrief_.extracted ? Colors::AccentEmerald : Colors::AccentRose;
    ImGui::GetWindowDrawList()->AddRectFilled(pMin, ImVec2(pMax.x, pMin.y + S(4.0f)), accent, S(8.0f), ImDrawFlags_RoundCornersTop);

    ImGui::PushFont(BoldFont(), S(15.0f));
    ImGui::TextColored(ColorVec4(accent), "%s", Tr(debrief_.extracted ? "EXTRACTION COMPLETE" : "KILLED IN ACTION"));
    ImGui::PopFont();
    ImGui::PushFont(HeadingFont(), S(34.0f));
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
            const float h = S(68.0f);
            DrawFieldCard(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + h), col);
            ImGui::GetWindowDrawList()->AddText(BoldFont(), S(14.0f), ImVec2(p.x + S(14.0f), p.y + S(10.0f)), Colors::TextSecondary, label);
            ImGui::GetWindowDrawList()->AddText(HeadingFont(), S(24.0f), ImVec2(p.x + S(14.0f), p.y + S(32.0f)), Colors::TextPrimary, value.c_str());
            ImGui::Dummy(ImVec2(w, h));
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
        const float h = S(50.0f);
        DrawFieldCard(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + h), Colors::AccentEmerald);
        const std::string line = Format("Last report: contracts completed: {} (+{} gold, reputation {:+}).",
            debrief_.contractsCompleted, debrief_.contractGold, debrief_.contractReputation);
        ImGui::GetWindowDrawList()->AddText(BoldFont(), S(16.0f), ImVec2(p.x + S(16.0f), p.y + S(15.0f)), Colors::TextHighlight, line.c_str());
        ImGui::Dummy(ImVec2(w, h));
    }

    ImGui::Spacing();
    if (PrimaryButton(Label("Return to safehouse"), ImVec2(-1.0f, S(48.0f)), true, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        debrief_.open = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
