#include "Modules/NextDotNet/ManagedProjectBuilder.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextDotNet/ManagedGameSession.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace Modules::NextDotNet
{
    using NextUI::Foundation::Color;
    using NextUI::Foundation::ColorU32;
    using NextUI::Foundation::EColor;

    namespace
    {
        constexpr float kModalWidth = 500.0f;
        constexpr float kModalHeight = 270.0f;

        void DrawSmoothSpinner(ImDrawList* drawList, ImVec2 center, float radius, float thickness, ImU32 color)
        {
            constexpr int numSegments = 36;
            const float startAngle = static_cast<float>(ImGui::GetTime() * 6.0);
            constexpr float sweepAngle = 1.6f * 3.14159265f;

            // Background subtle track
            drawList->AddCircle(center, radius, ColorU32(EColor::Border, 0.35f), numSegments, thickness);

            // Rotating active arc
            const int arcSegments = static_cast<int>(numSegments * (sweepAngle / (2.0f * 3.14159265f)));
            const float angleStep = sweepAngle / static_cast<float>(arcSegments);

            for (int i = 0; i < arcSegments; ++i)
            {
                const float a0 = startAngle + static_cast<float>(i) * angleStep;
                const float a1 = startAngle + static_cast<float>(i + 1) * angleStep;
                const float alpha = static_cast<float>(i + 1) / static_cast<float>(arcSegments);

                // Fade in along the arc tail
                const ImVec4 baseCol = ImGui::ColorConvertU32ToFloat4(color);
                const ImU32 segCol = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(baseCol.x, baseCol.y, baseCol.z, baseCol.w * (0.2f + 0.8f * alpha)));

                drawList->AddLine(
                    ImVec2(center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius),
                    ImVec2(center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius),
                    segCol, thickness);
            }
        }

        void DrawAnimatedProgressBar(ImDrawList* drawList, ImVec2 barMin, ImVec2 barMax, ImU32 fillCol)
        {
            const float width = barMax.x - barMin.x;
            const float height = barMax.y - barMin.y;
            const float rounding = height * 0.5f;

            // Background trough
            drawList->AddRectFilled(barMin, barMax, ColorU32(EColor::Background, 0.75f), rounding);
            drawList->AddRect(barMin, barMax, ColorU32(EColor::Border, 0.40f), rounding, 0, 1.0f);

            // Pulsing / moving highlight segment
            constexpr float pulseWidthRatio = 0.38f;
            const float pulseWidth = width * pulseWidthRatio;
            const float travelDist = width - pulseWidth;
            const float t = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime() * 3.2));
            const float pulseLeft = barMin.x + travelDist * t;
            const float pulseRight = pulseLeft + pulseWidth;

            drawList->PushClipRect(barMin, barMax, true);
            drawList->AddRectFilled(ImVec2(pulseLeft, barMin.y), ImVec2(pulseRight, barMax.y), fillCol, rounding);
            drawList->PopClipRect();
        }
    }

    FManagedProjectBuilder::FManagedProjectBuilder() = default;

    FManagedProjectBuilder::~FManagedProjectBuilder()
    {
        if (workerThread_.joinable())
        {
            workerThread_.join();
        }
    }

    bool FManagedProjectBuilder::StartBuild(ManagedGameSession* session, const FManagedGameManifest& manifest,
                                           std::string_view targetDisplayName)
    {
        if (IsBusy())
        {
            return false;
        }

        if (workerThread_.joinable())
        {
            workerThread_.join();
        }

        manifest_ = manifest;
        targetName_ = !targetDisplayName.empty()
                          ? std::string(targetDisplayName)
                          : (!manifest.displayName.empty() ? manifest.displayName : manifest.id);
        errorMessage_.clear();
        finalElapsedSeconds_ = 0.0;
        startTime_ = std::chrono::steady_clock::now();
        state_.store(EManagedBuildState::Building, std::memory_order_release);
        modalOpenRequested_ = true;
        successDismissTimer_ = -1.0f;

        workerThread_ = std::thread([this, session, manifest = manifest_]() {
            std::string err;
            bool ok = false;
            if (session != nullptr)
            {
                ok = session->RebuildGame(manifest, err);
            }
            else
            {
                err = "No active managed game session available for rebuild";
            }

            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - startTime_).count();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                finalElapsedSeconds_ = elapsed;
                errorMessage_ = std::move(err);
                state_.store(ok ? EManagedBuildState::Succeeded : EManagedBuildState::Failed,
                             std::memory_order_release);
            }
        });

        return true;
    }

    void FManagedProjectBuilder::Reset()
    {
        if (workerThread_.joinable())
        {
            workerThread_.join();
        }
        state_.store(EManagedBuildState::Idle, std::memory_order_release);
        errorMessage_.clear();
        finalElapsedSeconds_ = 0.0;
        modalOpenRequested_ = false;
        successDismissTimer_ = -1.0f;
    }

    bool FManagedProjectBuilder::IsBusy() const
    {
        return state_.load(std::memory_order_acquire) == EManagedBuildState::Building;
    }

    bool FManagedProjectBuilder::HasFinished() const
    {
        const EManagedBuildState s = state_.load(std::memory_order_acquire);
        return s == EManagedBuildState::Succeeded || s == EManagedBuildState::Failed;
    }

    bool FManagedProjectBuilder::IsSuccess() const
    {
        return state_.load(std::memory_order_acquire) == EManagedBuildState::Succeeded;
    }

    EManagedBuildState FManagedProjectBuilder::GetState() const
    {
        return state_.load(std::memory_order_acquire);
    }

    const std::string& FManagedProjectBuilder::GetTargetName() const
    {
        return targetName_;
    }

    const std::string& FManagedProjectBuilder::GetErrorMessage() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return errorMessage_;
    }

    double FManagedProjectBuilder::GetElapsedSeconds() const
    {
        if (IsBusy())
        {
            const auto now = std::chrono::steady_clock::now();
            return std::chrono::duration<double>(now - startTime_).count();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return finalElapsedSeconds_;
    }

    void FManagedProjectBuilder::DrawProgressCore(float cardWidth, bool inlineMode)
    {
        const EManagedBuildState currentState = state_.load(std::memory_order_acquire);
        const double elapsed = GetElapsedSeconds();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImFont* titleFont = nullptr;
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            titleFont = NextUI::Theme::GetTitleFont(*engine);
        }
        if (titleFont == nullptr)
        {
            titleFont = ImGui::GetFont();
        }

        const float startY = ImGui::GetCursorPosY();
        constexpr float spinnerRadius = 24.0f;
        const float centerX = ImGui::GetCursorPosX() + cardWidth * 0.5f;
        const float spinnerCenterY = ImGui::GetCursorScreenPos().y + spinnerRadius + 6.0f;
        const ImVec2 spinnerCenter(centerX, spinnerCenterY);

        if (currentState == EManagedBuildState::Building)
        {
            DrawSmoothSpinner(drawList, spinnerCenter, spinnerRadius, 3.5f, ColorU32(EColor::AccentHover));

            // Centered subtle icon inside spinner
            const ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_HAMMER);
            drawList->AddText(ImVec2(spinnerCenter.x - iconSize.x * 0.5f, spinnerCenter.y - iconSize.y * 0.5f),
                              ColorU32(EColor::TextMuted), ICON_FA_HAMMER);

            ImGui::SetCursorPosY(startY + spinnerRadius * 2.0f + 16.0f);

            // Title
            ImGui::PushFont(titleFont);
            const char* titleText = inlineMode ? "Publishing C# Project..." : "Rebuilding C# Project...";
            const ImVec2 titleSize = ImGui::CalcTextSize(titleText);
            ImGui::SetCursorPosX(centerX - titleSize.x * 0.5f);
            ImGui::TextColored(Color(EColor::Text), "%s", titleText);
            ImGui::PopFont();

            // Subtitle with Target Name badge
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            const std::string subText = fmt::format(ICON_FA_GAMEPAD "  {}  •  .NET 9 SDK", targetName_);
            const ImVec2 subSize = ImGui::CalcTextSize(subText.c_str());
            ImGui::SetCursorPosX(centerX - subSize.x * 0.5f);
            ImGui::TextColored(Color(EColor::AccentHover), "%s", subText.c_str());

            // Animated progress bar
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            const float barWidth = std::min(360.0f, cardWidth - 48.0f);
            const float barLeft = centerX - barWidth * 0.5f;
            const ImVec2 barMin(barLeft, ImGui::GetCursorScreenPos().y);
            const ImVec2 barMax(barLeft + barWidth, barMin.y + 7.0f);
            DrawAnimatedProgressBar(drawList, barMin, barMax, ColorU32(EColor::AccentHover));
            ImGui::Dummy(ImVec2(0.0f, 12.0f));

            // Real-time elapsed timer & detail status
            const std::string statusText = fmt::format("Running dotnet publish... ({:.1f}s)", elapsed);
            const ImVec2 statusSize = ImGui::CalcTextSize(statusText.c_str());
            ImGui::SetCursorPosX(centerX - statusSize.x * 0.5f);
            ImGui::TextColored(Color(EColor::TextDim), "%s", statusText.c_str());

            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            const char* hintText = "Building C# assemblies and syncing project content to runtime.";
            const ImVec2 hintSize = ImGui::CalcTextSize(hintText);
            ImGui::SetCursorPosX(centerX - hintSize.x * 0.5f);
            ImGui::TextColored(Color(EColor::TextMuted), "%s", hintText);
        }
        else if (currentState == EManagedBuildState::Succeeded)
        {
            // Success checkmark badge
            drawList->AddCircleFilled(spinnerCenter, spinnerRadius, ColorU32(EColor::Success, 0.20f), 32);
            drawList->AddCircle(spinnerCenter, spinnerRadius, ColorU32(EColor::Success, 0.85f), 32, 2.0f);

            const ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_CHECK);
            drawList->AddText(ImVec2(spinnerCenter.x - iconSize.x * 0.5f, spinnerCenter.y - iconSize.y * 0.5f),
                              ColorU32(EColor::Success), ICON_FA_CHECK);

            ImGui::SetCursorPosY(startY + spinnerRadius * 2.0f + 16.0f);

            ImGui::PushFont(titleFont);
            const std::string successTitle = fmt::format("Build Completed in {:.1f}s!", elapsed);
            const ImVec2 titleSize = ImGui::CalcTextSize(successTitle.c_str());
            ImGui::SetCursorPosX(centerX - titleSize.x * 0.5f);
            ImGui::TextColored(Color(EColor::Success), "%s", successTitle.c_str());
            ImGui::PopFont();

            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            const std::string subText = fmt::format("'{}' assemblies published and ready.", targetName_);
            const ImVec2 subSize = ImGui::CalcTextSize(subText.c_str());
            ImGui::SetCursorPosX(centerX - subSize.x * 0.5f);
            ImGui::TextColored(Color(EColor::TextMuted), "%s", subText.c_str());
        }
        else if (currentState == EManagedBuildState::Failed)
        {
            // Failed exclamation badge
            drawList->AddCircleFilled(spinnerCenter, spinnerRadius, ColorU32(EColor::Danger, 0.20f), 32);
            drawList->AddCircle(spinnerCenter, spinnerRadius, ColorU32(EColor::Danger, 0.85f), 32, 2.0f);

            const ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_EXCLAMATION);
            drawList->AddText(ImVec2(spinnerCenter.x - iconSize.x * 0.5f, spinnerCenter.y - iconSize.y * 0.5f),
                              ColorU32(EColor::Danger), ICON_FA_EXCLAMATION);

            ImGui::SetCursorPosY(startY + spinnerRadius * 2.0f + 16.0f);

            ImGui::PushFont(titleFont);
            const char* failTitle = "Build Failed";
            const ImVec2 titleSize = ImGui::CalcTextSize(failTitle);
            ImGui::SetCursorPosX(centerX - titleSize.x * 0.5f);
            ImGui::TextColored(Color(EColor::Danger), "%s", failTitle);
            ImGui::PopFont();

            std::string err;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                err = errorMessage_;
            }

            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            const float errPanelWidth = std::min(440.0f, cardWidth - 32.0f);
            ImGui::SetCursorPosX(centerX - errPanelWidth * 0.5f);

            if (NextUI::Theme::BeginInsetPanel("##BuildErrDetails", ImVec2(errPanelWidth, 76.0f), true, 0,
                                               ImVec2(10.0f, 8.0f), 0.50f))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::Danger));
                ImGui::TextWrapped("%s", err.c_str());
                ImGui::PopStyleColor();
            }
            NextUI::Theme::EndInsetPanel();
        }
    }

    bool FManagedProjectBuilder::DrawInlineProgress(ImVec2 size)
    {
        if (state_.load(std::memory_order_acquire) == EManagedBuildState::Idle)
        {
            return false;
        }

        const float availWidth = size.x > 0.0f ? size.x : ImGui::GetContentRegionAvail().x;
        const float availHeight = size.y > 0.0f ? size.y : ImGui::GetContentRegionAvail().y;

        ImGui::BeginChild("##InlineBuildProgress", ImVec2(availWidth, availHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // Center vertically
        constexpr float contentEstimatedHeight = 220.0f;
        const float topPadding = std::max(12.0f, (availHeight - contentEstimatedHeight) * 0.42f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPadding);

        DrawProgressCore(availWidth, true);

        ImGui::EndChild();

        return HasFinished();
    }

    bool FManagedProjectBuilder::DrawModalProgress(const char* popupId)
    {
        const EManagedBuildState s = state_.load(std::memory_order_acquire);
        if (s == EManagedBuildState::Idle)
        {
            return false;
        }

        if (modalOpenRequested_)
        {
            modalOpenRequested_ = false;
            ImGui::OpenPopup(popupId);
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x, viewport->GetCenter().y), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(kModalWidth, kModalHeight), ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(EColor::Surface, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Color(EColor::Surface, 0.98f));

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

        bool stayOpen = true;
        bool dismissed = false;

        if (ImGui::BeginPopupModal(popupId, &stayOpen, flags))
        {
            const float availWidth = ImGui::GetContentRegionAvail().x;
            DrawProgressCore(availWidth, false);

            ImGui::SetCursorPosY(kModalHeight - 48.0f);

            if (s == EManagedBuildState::Succeeded)
            {
                // Auto-dismiss after 1.2 seconds, or let user click Done immediately
                if (successDismissTimer_ < 0.0f)
                {
                    successDismissTimer_ = 1.2f;
                }
                else
                {
                    successDismissTimer_ -= ImGui::GetIO().DeltaTime;
                    if (successDismissTimer_ <= 0.0f)
                    {
                        ImGui::CloseCurrentPopup();
                        Reset();
                        dismissed = true;
                    }
                }

                ImGui::SetCursorPosX((kModalWidth - 110.0f) * 0.5f);
                if (ImGui::Button(ICON_FA_CHECK "  Done", ImVec2(110.0f, 30.0f)))
                {
                    ImGui::CloseCurrentPopup();
                    Reset();
                    dismissed = true;
                }
            }
            else if (s == EManagedBuildState::Failed)
            {
                ImGui::SetCursorPosX((kModalWidth - 110.0f) * 0.5f);
                if (ImGui::Button("Close", ImVec2(110.0f, 30.0f)))
                {
                    ImGui::CloseCurrentPopup();
                    Reset();
                    dismissed = true;
                }
            }
            else
            {
                // Building: indicate running in background
                ImGui::SetCursorPosX((kModalWidth - 220.0f) * 0.5f);
                ImGui::TextColored(Color(EColor::TextDim), ICON_FA_ROTATE "  Compiling in background...");
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);

        return dismissed;
    }
}
