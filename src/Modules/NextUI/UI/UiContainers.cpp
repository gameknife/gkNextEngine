#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/UI/UiContainers.hpp"

namespace NextUI::Foundation
{
    bool BeginOverlayPanel(const FOverlayPanelOptions& options)
    {
        ImGui::SetNextWindowPos(options.position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(options.size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(options.backgroundAlpha);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, options.padding);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, options.itemSpacing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, options.rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, options.borderSize);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(options.backgroundColor, options.backgroundAlpha));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::BorderStrong, options.borderAlpha));

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | options.extraFlags;
        return ImGui::Begin(options.windowId, nullptr, flags);
    }

    void EndOverlayPanel()
    {
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
    }

    bool BeginInsetPanel(const char* id,
                         const ImVec2 size,
                         const bool border,
                         const ImGuiWindowFlags flags,
                         const ImVec2 padding,
                         const float backgroundAlpha)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Color(EColor::Background, backgroundAlpha));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.82f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
        // ChildBorderSize is 0 in this theme, so ImGuiChildFlags_Borders alone would draw nothing;
        // restore ChildBorderSize when a border is requested and always preserve window padding.
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, border ? 1.0f : 0.0f);
        const ImGuiChildFlags childFlags = ImGuiChildFlags_AlwaysUseWindowPadding |
            (border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None);
        const bool visible = ImGui::BeginChild(id, size, childFlags, flags);
        ImGui::PopStyleVar();
        return visible;
    }

    void EndInsetPanel()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    void BeginCard(const char* id, const float height, const ImGuiWindowFlags flags, const float horizontalInset)
    {
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        const float width = std::max(0.0f, ImGui::GetContentRegionAvail().x - horizontalInset * 2.0f);
        if (horizontalInset > 1.0f)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizontalInset);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Color(EColor::SurfaceElevated, 0.40f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.70f));
        const ImGuiChildFlags childFlags = height > 0.0f
            ? ImGuiChildFlags_Borders
            : ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY;
        ImGui::BeginChild(id, ImVec2(width, std::max(0.0f, height)), childFlags, flags);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    void EndCard()
    {
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
    }

    bool BeginSection(const char* icon, const char* label, const bool defaultOpen)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Header, Color(EColor::Background, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(EColor::Accent, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.84f));

        const std::string header = fmt::format("{} {}", icon ? icon : "", label ? label : "");
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
        if (defaultOpen)
        {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        const bool open = ImGui::CollapsingHeader(header.c_str(), flags);

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);

        if (open)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
            ImGui::Indent(5.0f);
        }
        return open;
    }

    void EndSection()
    {
        ImGui::Unindent(5.0f);
        ImGui::PopStyleVar();
        ImGui::Spacing();
    }

    FScopedOverlayPanel::FScopedOverlayPanel(const FOverlayPanelOptions& options)
    {
        ImGui::SetNextWindowPos(options.position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(options.size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(options.backgroundAlpha);
        style_.Add(ImGuiStyleVar_WindowPadding, options.padding)
            .Add(ImGuiStyleVar_ItemSpacing, options.itemSpacing)
            .Add(ImGuiStyleVar_WindowRounding, options.rounding)
            .Add(ImGuiStyleVar_WindowBorderSize, options.borderSize)
            .Add(ImGuiCol_WindowBg, Color(options.backgroundColor, options.backgroundAlpha))
            .Add(ImGuiCol_Border, Color(EColor::BorderStrong, options.borderAlpha));

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | options.extraFlags;
        window_ = std::make_unique<FScopedWindow>(options.windowId, nullptr, flags);
    }

    FScopedInsetPanel::FScopedInsetPanel(const char* id,
                                         const ImVec2 size,
                                         const bool border,
                                         const ImGuiWindowFlags flags,
                                         const ImVec2 padding,
                                         const float backgroundAlpha)
    {
        style_.Add(ImGuiCol_ChildBg, Color(EColor::Background, backgroundAlpha))
            .Add(ImGuiCol_Border, Color(EColor::Border, 0.82f))
            .Add(ImGuiStyleVar_WindowPadding, padding)
            .Add(ImGuiStyleVar_ChildBorderSize, border ? 1.0f : 0.0f);
        const ImGuiChildFlags childFlags = ImGuiChildFlags_AlwaysUseWindowPadding |
            (border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None);
        child_ = std::make_unique<FScopedChild>(id, size, childFlags, flags);
    }

    FScopedCard::FScopedCard(const char* id, const float height, const ImGuiWindowFlags flags)
    {
        BeginCard(id, height, flags);
    }

    FScopedCard::~FScopedCard()
    {
        EndCard();
    }

    FScopedSection::FScopedSection(const char* icon, const char* label, const bool defaultOpen)
        : open_(BeginSection(icon, label, defaultOpen))
    {
    }

    FScopedSection::~FScopedSection()
    {
        if (open_)
        {
            EndSection();
        }
    }

    FScopedToolbar::FScopedToolbar(const char* id,
                                   const ImVec2 size,
                                   const ImVec2 padding,
                                   const ImVec2 spacing)
    {
        style_.Add(ImGuiStyleVar_WindowPadding, padding)
            .Add(ImGuiStyleVar_ItemSpacing, spacing)
            .Add(ImGuiStyleVar_ChildRounding, 6.0f)
            .Add(ImGuiStyleVar_ChildBorderSize, 0.0f)
            .Add(ImGuiCol_ChildBg, Color(EColor::Surface, 0.92f));
        child_ = std::make_unique<FScopedChild>(id, size, ImGuiChildFlags_None,
                                                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    }

    void LabeledRow(const char* label,
                    const float ratio,
                    const float minLabelWidth,
                    const float maxLabelWidth)
    {
        ImGui::AlignTextToFramePadding();
        {
            FScopedStyle textStyle;
            textStyle.Add(ImGuiCol_Text, Color(EColor::TextMuted));
            ImGui::TextUnformatted(label ? label : "");
        }
        const float availableWidth = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
        const float labelWidth = std::clamp(availableWidth * ratio, minLabelWidth, maxLabelWidth);
        ImGui::SameLine(labelWidth);
    }
}
