#include "BrickPlayerUserInterface.hpp"
#include "BrickPlayerGameInstance.hpp"
#include "Engine/Runtime/Interface/ScreenShotService.hpp"
#include "Engine/Utilities/ImGui.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include <imgui_internal.h>

namespace
{
    constexpr float titleBarHeight = 40.0f;
    constexpr float titleBarControlWidth = titleBarHeight * 3;
    constexpr float titleBarActionWidth = titleBarHeight * 12;
}

BrickPlayerUserInterface::BrickPlayerUserInterface(BrickPlayerGameInstance* gameInstance)
    : gameInstance_(gameInstance)
{
}

void BrickPlayerUserInterface::ApplyStyle()
{
    NextUI::Theme::ApplyProfessionalTheme();
}

void BrickPlayerUserInterface::Render()
{
    gameInstance_->SetMouseCapturedByUI(ImGui::GetIO().WantCaptureMouse);

    RenderTitleBar();

    if (!gameInstance_->IsSceneLoaded())
    {
        RenderWelcomeScreen();
        return;
    }

    if (gameInstance_->IsFreeBuildMode())
        RenderFreeBuildToolbar();
    else
        RenderTimeline();
    gameInstance_->DrawSnapConfirmation();
    gameInstance_->DrawSnapDebug();
}

void BrickPlayerUserInterface::RenderTitleBar()
{
    auto& engine = gameInstance_->GetEngine();
    NextUI::Theme::FAppTitleBarConfig config{};
    config.BrandWindowId = "##BrickPlayerTitleBarBrand";
    config.MenuWindowId = "##BrickPlayerTitleBarLeft";
    config.RightWindowId = "##BrickPlayerTitleBarRight";
    config.AppName = "BrickPlayer";
    config.Height = titleBarHeight;
    config.BrandIconSize = 26.0f;
    config.IsMaximized = engine.IsMaximized();
    config.OnMinimize = [&engine]() { engine.RequestMinimize(); };
    config.OnToggleMaximize = [&engine]() { engine.ToggleMaximize(); };
    config.OnClose = [&engine]() { engine.RequestClose(); };
    config.DrawMenuBar = [this]() -> float
    {
        const ImVec2 btnSize(titleBarHeight - 10.0f, titleBarHeight - 12.0f);
        if (ImGui::Button(ICON_FA_FOLDER_OPEN, btnSize))
        {
            gameInstance_->OpenFileDialog();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Open File");

        if (gameInstance_->IsSceneLoaded())
        {
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT, btnSize))
            {
                gameInstance_->ResetAll();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reset");

            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_CAMERA, btnSize))
            {
                gameInstance_->GetEngine().GetScreenShotService().Request();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Take a Screenshot into the screenshots folder");

            if (gameInstance_->HasBuildSteps())
            {
                NextUI::Theme::DrawVerticalSeparator(14.0f, 8.0f);

                bool perPart = gameInstance_->IsPerPartMode();
                if (ImGui::Button(perPart ? ICON_FA_CUBE : ICON_FA_LAYER_GROUP, btnSize))
                {
                    gameInstance_->SetPerPartMode(!perPart);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(perPart ? "Part Mode (click for Step Mode)" : "Step Mode (click for Part Mode)");
            }

            NextUI::Theme::DrawVerticalSeparator(14.0f, 8.0f);

            bool debugOn = gameInstance_->IsShowPhysicsDebug();
            if (debugOn)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.0f, 0.6f));
            if (ImGui::Button(ICON_FA_VECTOR_SQUARE, btnSize))
            {
                gameInstance_->TogglePhysicsDebug();
            }
            if (debugOn)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Physics Debug (F1)");

            ImGui::SameLine();
            bool globalPhysicsOn = gameInstance_->IsShowGlobalPhysicsBodies();
            if (globalPhysicsOn)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.1f, 0.1f, 0.75f));
            if (ImGui::Button(ICON_FA_CUBES_STACKED, btnSize))
            {
                gameInstance_->ToggleGlobalPhysicsBodies();
            }
            if (globalPhysicsOn)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Engine Physics Bodies Debug (F8)");

            ImGui::SameLine();
            bool snapDebugOn = gameInstance_->IsShowSnapDebug();
            if (snapDebugOn)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.45f, 0.0f, 0.75f));
            if (ImGui::Button("Sn", btnSize))
            {
                gameInstance_->ToggleSnapDebug();
            }
            if (snapDebugOn)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Snap Debug (F7)");

            NextUI::Theme::DrawVerticalSeparator(14.0f, 8.0f);

            bool horizontalDrag = gameInstance_->IsHorizontalDragPlane();
            if (horizontalDrag)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.7f, 0.75f));
            if (ImGui::Button(horizontalDrag ? ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT : ICON_FA_ARROW_UP, btnSize))
            {
                gameInstance_->ToggleDragPlaneMode();
                gameInstance_->SwitchDragPlaneWhileDragging();
            }
            if (horizontalDrag)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(horizontalDrag ? "Drag: XZ Plane (F9)" : "Drag: Screen Plane (F9)");

            ImGui::SameLine();
            bool bgmPaused = gameInstance_->IsBGMPaused();
            if (ImGui::Button(bgmPaused ? ICON_FA_VOLUME_LOW : ICON_FA_VOLUME_XMARK, btnSize))
            {
                gameInstance_->PauseBGM(!bgmPaused);
            }
            if (ImGui::IsItemHovered())
            {
                const std::string currentBgmName = gameInstance_->GetCurrentBGMName();
                const std::string tooltip = currentBgmName.empty()
                    ? std::string(bgmPaused ? "Play BGM" : "Pause BGM")
                    : std::string(bgmPaused ? "Play BGM\n" : "Pause BGM\n") + currentBgmName;
                ImGui::SetTooltip("%s", tooltip.c_str());
            }

            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_SHUFFLE, btnSize))
            {
                gameInstance_->PlayNextBGM();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Shuffle BGM");
        }
        return ImGui::GetItemRectMax().x;
    };
    NextUI::Theme::DrawAppTitleBar(engine, config);
}

void BrickPlayerUserInterface::RenderTimeline()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float panelHeight = 50.0f;
    float panelY = viewport->Pos.y + viewport->Size.y - panelHeight;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, panelY));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, panelHeight));
    ImGui::SetNextWindowBgAlpha(0.8f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("##Timeline", nullptr, flags))
    {
        int32_t totalSteps = gameInstance_->GetTotalSteps();
        int32_t currentStep = gameInstance_->GetCurrentStep();
        bool isPlaying = gameInstance_->IsAutoPlaying();

        // Step backward button
        if (ImGui::Button(ICON_FA_BACKWARD_STEP))
        {
            gameInstance_->StepBackward();
        }
        ImGui::SameLine();

        // Play / Pause button
        if (ImGui::Button(isPlaying ? ICON_FA_PAUSE : ICON_FA_PLAY))
        {
            gameInstance_->ToggleAutoPlay();
        }
        ImGui::SameLine();

        // Speed button
        char speedLabel[16];
        snprintf(speedLabel, sizeof(speedLabel), "%s", gameInstance_->GetPlaySpeedLabel());
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(speedLabel))
        {
            gameInstance_->CyclePlaySpeed();
        }
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Playback Speed");
        ImGui::SameLine();

        // Step forward button
        if (ImGui::Button(ICON_FA_FORWARD_STEP))
        {
            gameInstance_->StepForward();
        }
        ImGui::SameLine();

        // Timeline slider
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 260.0f);
        int step = currentStep;
        if (totalSteps > 0 && ImGui::SliderInt("##Step", &step, 0, std::max(0, totalSteps - 1)))
        {
            gameInstance_->SetCurrentStep(step);
        }
        ImGui::SameLine();

        // Step counter
        if (gameInstance_->IsPerPartMode())
            ImGui::Text("Part %d / %d", currentStep + 1, totalSteps);
        else
            ImGui::Text("Step %d / %d", currentStep + 1, totalSteps);
        ImGui::SameLine();

        // Disassemble button
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        bool hasSelection = gameInstance_->HasSelection();
        if (!hasSelection)
            ImGui::BeginDisabled();

        if (ImGui::Button(ICON_FA_HAMMER " Disassemble"))
        {
            gameInstance_->DisassembleSelected();
        }

        if (!hasSelection)
            ImGui::EndDisabled();
    }
    ImGui::End();
}

void BrickPlayerUserInterface::RenderFreeBuildToolbar()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float panelHeight = 50.0f;
    float panelY = viewport->Pos.y + viewport->Size.y - panelHeight;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, panelY));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, panelHeight));
    ImGui::SetNextWindowBgAlpha(0.8f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("##FreeBuild", nullptr, flags))
    {
        ImGui::Text(ICON_FA_CUBES " FreeBuild");
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_PLUS " Spawn Bricks (N)"))
        {
            gameInstance_->SpawnRandomBricks(6);
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        ImGui::Text("Available: %d", gameInstance_->CountAvailableBricks());
    }
    ImGui::End();
}

void BrickPlayerUserInterface::RenderWelcomeScreen()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                           viewport->Pos.y + viewport->Size.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 120));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("##Welcome", nullptr, flags))
    {
        ImGui::Text("BrickPlayer");
        ImGui::Separator();
        ImGui::TextWrapped("Open an LDraw file (.ldr / .mpd) to begin, or start FreeBuild mode.");
        ImGui::Spacing();

        float buttonWidth = 120.0f;
        float totalWidth = buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX((400.0f - totalWidth) * 0.5f);
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " Open File", ImVec2(buttonWidth, 0)))
        {
            gameInstance_->OpenFileDialog();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CUBES " FreeBuild", ImVec2(buttonWidth, 0)))
        {
            gameInstance_->StartFreeBuild();
        }
    }
    ImGui::End();
}
