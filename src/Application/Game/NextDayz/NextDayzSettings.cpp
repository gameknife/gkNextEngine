#include "Engine/Common/CoreMinimal.hpp"
#include "NextDayzGameInstance.hpp"

#include <imgui.h>

#include "Engine/Options.hpp"
#include "Engine/Rendering/RendererChoices.hpp"
#include "Engine/Rendering/Upscaler/UpscalerTypes.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"

void NextDayzGameInstance::ConfigureSettingsCVars(NextCVar::FCVarSystem& cvars)
{
    using NextCVar::ECVarFlags;
    cvars.RegisterFloat("nextdayz.settings.soundVolume", 1.0f, &soundVolume_, ECVarFlags::Archive,
        "Sound effects and ambience volume", [this]() { audio_.SetVolume(soundVolume_); }, 0.0, 1.0);
    cvars.RegisterFloat("nextdayz.settings.mouseSensitivity", 1.0f, &mouseSensitivity_, ECVarFlags::Archive,
        "Mouse look sensitivity multiplier", nullptr, 0.1, 3.0);
    cvars.RegisterBool("nextdayz.settings.invertMouseY", false, &invertMouseY_, ECVarFlags::Archive,
        "Invert vertical mouse look");
}

void NextDayzGameInstance::OpenSettings()
{
    settingsOpen_ = true;
    settingsSaveFailed_ = false;
    safehouseTab_ = -1;
    weapons_.SetTriggerDown(false);
    for (int direction = 0; direction < 4; ++direction) player_.SetMoveKey(direction, false);
    player_.SetSprintModifier(false);
    player_.SetWalkModifier(false);
    player_.SetJumpHeld(false);
    SetMouseCaptured(false);
}

void NextDayzGameInstance::DrawSettings()
{
    using namespace NextDayz::L10n;
    using namespace Rendering::Upscaler;
    if (!settingsOpen_) return;

    constexpr const char* popupId = "##nextdayz_settings";
    const bool openedThisFrame = !ImGui::IsPopupOpen(popupId);
    if (openedThisFrame) ImGui::OpenPopup(popupId);
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(720.0f, viewport->WorkSize.x - 24.0f),
                                  std::min(550.0f, viewport->WorkSize.y - 24.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 12));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.035f, 0.065f, 0.075f, 0.98f));
    if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        const bool childPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextUnformatted(Tr("SETTINGS"));
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
        const char* tabs[] = {"Graphics", "Audio & Controls", "Language"};
        const float tabWidth = (ImGui::GetContentRegionAvail().x - 24.0f) / 3.0f;
        for (int tab = 0; tab < 3; ++tab)
        {
            if (tab) ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, settingsTab_ == tab
                ? ImVec4(0.40f, 0.31f, 0.17f, 1) : ImVec4(0.10f, 0.16f, 0.18f, 1));
            if (ImGui::Button(Label(tabs[tab]), ImVec2(tabWidth, 38))) settingsTab_ = tab;
            ImGui::PopStyleColor();
        }

        auto& engine = GetEngine();
        auto& settings = engine.GetUserSettings();
        auto& renderer = engine.GetRenderer();
        const auto set = [&](const char* name, const auto& value)
        {
            const auto result = engine.GetCVarSystem().ExecuteCommand(fmt::format("{} {}", name, value));
            if (result.success)
            {
                settingsDirty_ = true;
                settingsSaveFailed_ = false;
            }
            else SPDLOG_WARN("[NextDayz] setting {} failed: {}", name, result.message);
        };
        const auto endCombo = []()
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
            ImGui::EndCombo();
        };
        ImGui::BeginChild("settings_content", ImVec2(0, -94));
        if (settingsTab_ == 0)
        {
            const auto row = [](const char* label)
            {
                ImGui::TextUnformatted(Tr(label));
                ImGui::SetNextItemWidth(-1);
            };
            row("Renderer");
            const auto* current = Rendering::FindRendererChoice(static_cast<Vulkan::ERendererType>(settings.RendererType));
            if (ImGui::BeginCombo("##renderer", current ? Tr(current->displayName) : Tr("Compatibility")))
            {
                for (const auto& choice : Rendering::RendererChoiceCatalog())
                {
                    const bool available = Rendering::IsRendererChoiceAvailable(choice.type, renderer.RendererChoiceCapabilities());
                    ImGui::BeginDisabled(!available);
                    const bool selected = settings.RendererType == static_cast<int32_t>(choice.type);
                    if (ImGui::Selectable(Label(choice.displayName), selected))
                    {
                        set("r.rendererType", static_cast<int32_t>(choice.type));
                        // RR consumes the full path tracer's material and hit-distance outputs.
                        if (GetUpscalerTypeInfo(settings.UpscalerType).requiresRayReconstruction &&
                            choice.type != Vulkan::ERT_PathTracing)
                            set("r.upscaler.type", static_cast<uint32_t>(renderer.SupportsUpscaler(EUpscalerType::NativeTAAU)
                                ? EUpscalerType::NativeTAAU : EUpscalerType::None));
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                    ImGui::EndDisabled();
                }
                endCombo();
            }
            row("Upscaler");
            if (ImGui::BeginCombo("##upscaler", Tr(GetUpscalerTypeInfo(settings.UpscalerType).name)))
            {
                for (uint32_t index = 0; index < static_cast<uint32_t>(EUpscalerType::Count); ++index)
                {
                    const auto& info = GetUpscalerTypeInfo(index);
                    const bool available = info.type == EUpscalerType::None ||
                        (renderer.SupportsUpscaler(info.type) && (!info.requiresRayReconstruction ||
                            settings.RendererType == static_cast<int32_t>(Vulkan::ERT_PathTracing)));
                    ImGui::BeginDisabled(!available);
                    const bool selected = settings.UpscalerType == static_cast<int32_t>(index);
                    if (ImGui::Selectable(Label(info.name), selected)) set("r.upscaler.type", index);
                    if (selected) ImGui::SetItemDefaultFocus();
                    ImGui::EndDisabled();
                }
                endCombo();
            }
            ImGui::BeginDisabled(settings.UpscalerType == static_cast<int32_t>(EUpscalerType::None));
            row("Upscaling quality");
            if (ImGui::BeginCombo("##upscale_quality", Tr(GetUpscaleModeInfo(settings.SuperResolution).name)))
            {
                for (uint32_t index = 0; index <= static_cast<uint32_t>(EUpscaleMode::Auto); ++index)
                {
                    const bool selected = settings.SuperResolution == index;
                    if (ImGui::Selectable(Label(GetUpscaleModeInfo(index).name), selected)) set("r.upscaler.qualityMode", index);
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                endCombo();
            }
            ImGui::EndDisabled();
            bool fullscreen = settings.BorderlessFullscreen;
            if (ImGui::Checkbox(Label("Borderless fullscreen"), &fullscreen)) set("sys.borderlessFullscreen", fullscreen);
            bool autoExposure = settings.AutoExposure;
            if (ImGui::Checkbox(Label("Automatic exposure"), &autoExposure)) set("r.exposure.auto", autoExposure);
            ImGui::TextDisabled("%s", Tr("Unavailable options depend on your GPU and renderer."));
        }
        else if (settingsTab_ == 1)
        {
            ImGui::TextUnformatted(Tr("Sound effects volume"));
            ImGui::SetNextItemWidth(-1);
            float volume = soundVolume_ * 100.0f;
            if (ImGui::SliderFloat("##sound_volume", &volume, 0, 100, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
                set("nextdayz.settings.soundVolume", volume / 100.0f);
            ImGui::TextUnformatted(Tr("Mouse sensitivity"));
            ImGui::SetNextItemWidth(-1);
            float sensitivity = mouseSensitivity_;
            if (ImGui::SliderFloat("##mouse_sensitivity", &sensitivity, 0.1f, 3.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
                set("nextdayz.settings.mouseSensitivity", sensitivity);
            bool invertY = invertMouseY_;
            if (ImGui::Checkbox(Label("Invert mouse Y"), &invertY)) set("nextdayz.settings.invertMouseY", invertY);
        }
        else
        {
            ImGui::TextUnformatted("语言 / Language (F12)");
            ImGui::SetNextItemWidth(-1);
            const char* languages[] = {"简体中文", "English"};
            const int language = Language() == ELanguage::Chinese ? 0 : 1;
            if (ImGui::BeginCombo("##settings_language", languages[language]))
            {
                for (int index = 0; index < 2; ++index)
                {
                    if (ImGui::Selectable(languages[index], language == index))
                        SetLanguage(index == 0 ? ELanguage::Chinese : ELanguage::English);
                    if (language == index) ImGui::SetItemDefaultFocus();
                }
                endCombo();
            }
            ImGui::Spacing();
            ImGui::TextWrapped("%s", Tr("Language changes apply immediately to menus and messages."));
        }
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::TextWrapped("%s", Tr(settingsSaveFailed_
            ? "Could not save settings. Try again."
            : "Changes apply immediately and are saved when you close this panel."));
        if (ImGui::Button(Label("BACK  [ESC]"), ImVec2(-1, 38)) ||
            (!openedThisFrame && !childPopupOpen && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
             !ImGui::IsAnyItemActive()))
        {
            settingsSaveFailed_ = settingsDirty_ && !GOption->AgentValidation && !engine.GetCVarSystem().SaveUserFiles();
            if (!settingsSaveFailed_)
            {
                settingsDirty_ = false;
                settingsOpen_ = false;
                ImGui::CloseCurrentPopup();
                if (!paused_ && !cabinScene_ && !NetInLobby() && !showInventory_ && !showMap_ && survival_.IsAlive() &&
                    (!CampaignEnabled() || (campaign_.inField && !campaign_.flight.boarded && !campaignSaveBlocked_)))
                    SetMouseCaptured(true);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}
