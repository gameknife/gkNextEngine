#include "KongLie3DStyle.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"

namespace KongLie3D::KongLieFonts
{
    ImFont* Body = nullptr;
    ImFont* Title = nullptr;
    ImFont* Display = nullptr;
}

namespace KongLie3D
{
    void ApplyKongLieImGuiStyle()
    {
        NextUI::Theme::ApplyProfessionalTheme();
    }

    void PushPanelStyle()
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Style::Surface);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(Style::SurfaceAlt.x, Style::SurfaceAlt.y, Style::SurfaceAlt.z, 0.40f));
        ImGui::PushStyleColor(ImGuiCol_Border, Style::Border);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    }

    void PopPanelStyle()
    {
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
    }
}
