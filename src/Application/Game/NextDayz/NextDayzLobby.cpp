#include "Engine/Common/CoreMinimal.hpp"
#include "NextDayzGameInstance.hpp"

#include <sstream>

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

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
    if (!exists && !fresh) SaveCampaign();
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
    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->WorkPos;
    const ImVec2 size = viewport->WorkSize;
    const float margin = std::clamp(size.x * 0.035f, 18.0f, 64.0f);
    const float width = std::clamp(size.x * 0.23f, 200.0f, 310.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
    if (!ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) safehouseTab_ = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_M, false)) safehouseTab_ = safehouseTab_ == 0 ? -1 : 0;
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) safehouseTab_ = safehouseTab_ == 1 ? -1 : 1;
    }
    auto* background = ImGui::GetBackgroundDrawList();
    const ImVec2 bottom(origin.x + size.x, origin.y + size.y);
    background->AddRectFilledMultiColor(origin, ImVec2(origin.x + size.x, origin.y + 150),
        IM_COL32(6,12,16,200), IM_COL32(6,12,16,170), IM_COL32(6,12,16,0), IM_COL32(6,12,16,0));
    background->AddRectFilledMultiColor(ImVec2(origin.x, bottom.y - 150), bottom,
        IM_COL32(6,12,16,0), IM_COL32(6,12,16,0), IM_COL32(6,12,16,225), IM_COL32(6,12,16,225));
    // The menu owns the left third; the survivor keeps the right of the frame.
    background->AddRectFilledMultiColor(origin, ImVec2(origin.x + size.x * 0.42f, bottom.y),
        IM_COL32(6,12,16,150), IM_COL32(6,12,16,0), IM_COL32(6,12,16,0), IM_COL32(6,12,16,150));
    ImGui::SetNextWindowPos(ImVec2(origin.x + margin, origin.y + 25));
    ImGui::SetNextWindowSize(ImVec2(size.x - margin * 2, 105));
    ImGui::Begin("##cabin_title", nullptr, flags);
    ImGui::SetWindowFontScale(1.7f);
    ImGui::TextUnformatted("NEXTDAYZ");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextColored(ImVec4(0.80f,0.68f,0.43f,1), "%s", Tr("AIRBORNE / FORWARD OPERATIONS"));
    ImGui::End();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18,18));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14,12));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8,12));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.07f,0.11f,0.13f,0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f,0.30f,0.25f,0.98f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.42f,0.34f,0.19f,1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f,0.94f,0.88f,1));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.65f,0.70f,0.67f,1));
    const float panelHeight[2] = {196.0f, 214.0f};
    float panelTop = origin.y + std::max(150.0f, size.y * 0.22f);
    for (int side = 0; side < 2; ++side)
    {
        ImGui::SetNextWindowPos(ImVec2(origin.x + margin, panelTop));
        ImGui::SetNextWindowSize(ImVec2(width, panelHeight[side]));
        panelTop += panelHeight[side] + 14.0f;
        ImGui::Begin(side == 0 ? "##cabin_operations" : "##cabin_equipment", nullptr, flags);
        ImGui::SetWindowFontScale(1.15f);
        const ImVec2 at = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + width, at.y + panelHeight[side] - 5.0f),
            IM_COL32(9,17,20,205), 5.0f);
        ImGui::TextDisabled("%s", Tr(side == 0 ? "OPERATIONS" : "PREPARATION"));
        ImGui::Separator();
        if (side == 0)
        {
            if (ImGui::Button(Label("WORLD MAP  [M]"), ImVec2(-1,56))) safehouseTab_ = safehouseTab_ == 0 ? -1 : 0;
            ImGui::BeginDisabled(!canDeploy);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.44f,0.32f,0.16f,0.96f));
            if (ImGui::Button(Label(resume ? "CONTINUE EXPEDITION" : "DEPLOY"), ImVec2(-1,56))) deploy = true;
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
        }
        else
        {
            if (ImGui::Button(Label("STORAGE / LOADOUT  [TAB]"), ImVec2(-1,56))) safehouseTab_ = safehouseTab_ == 1 ? -1 : 1;
            ImGui::TextWrapped("%s", Tr("Equipment changes appear on your survivor."));
            DrawLanguageSelector();
        }
        ImGui::End();
    }
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);
    ImGui::SetNextWindowPos(ImVec2(origin.x + margin, bottom.y - 100));
    ImGui::SetNextWindowSize(ImVec2(size.x - margin * 2, 100));
    ImGui::Begin("##cabin_status", nullptr, flags);
    ImGui::SetWindowFontScale(1.1f);
    ImGui::TextWrapped("%s", status.c_str());
    ImGui::TextColored(ImVec4(0.68f,0.74f,0.70f,1), "%s", Tr("M  World map     TAB  Storage     ESC  Back to cabin"));
    ImGui::End();
}

void NextDayzGameInstance::BeginCabinPanel(const char* title)
{
    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 size = viewport->WorkSize;
    const float margin = std::clamp(size.x * 0.07f, 16.0f, 100.0f);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + margin, viewport->WorkPos.y + 100));
    ImGui::SetNextWindowSize(ImVec2(size.x - 2 * margin, std::max(200.0f, size.y - 200)));
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGui::Begin("##cabin_panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(NextDayz::L10n::Tr(title));
    ImGui::SameLine(std::max(200.0f, ImGui::GetWindowWidth() - 160));
    if (ImGui::SmallButton(NextDayz::L10n::Label("BACK TO CABIN"))) safehouseTab_ = -1;
    ImGui::Separator();
}
