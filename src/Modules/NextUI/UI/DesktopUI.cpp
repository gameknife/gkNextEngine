#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"

#include <algorithm>
#include <cmath>
#include <imgui_internal.h>
#include <fmt/format.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

namespace NextUI::Theme
{
    namespace
    {
        constexpr const char* kBrandIconAssetPath = "assets/brand/gknext_logo_icon.png";
        constexpr const char* kBrandIconAssetPath125 = "assets/brand/gknext_logo_icon_125.png";
        constexpr const char* kBrandIconAssetPath150 = "assets/brand/gknext_logo_icon_150.png";
        constexpr const char* kBrandIconAssetPath175 = "assets/brand/gknext_logo_icon_175.png";
        constexpr const char* kBrandIconAssetPath200 = "assets/brand/gknext_logo_icon_200.png";
        constexpr float kDetailPanelBackgroundAlpha = 0.82f;
        constexpr float kDetailPanelHeaderAlpha = 0.56f;
        constexpr float kDetailPanelBorderAlpha = 0.52f;

        const char* GetBrandIconAssetPath(float uiScale)
        {
            if (uiScale >= 1.875f)
            {
                return kBrandIconAssetPath200;
            }
            if (uiScale >= 1.625f)
            {
                return kBrandIconAssetPath175;
            }
            if (uiScale >= 1.375f)
            {
                return kBrandIconAssetPath150;
            }
            if (uiScale >= 1.125f)
            {
                return kBrandIconAssetPath125;
            }
            return kBrandIconAssetPath;
        }

        void DrawBrandIcon(NextEngine& engine, ImDrawList* drawList, ImVec2 min, float size)
        {
            if (drawList == nullptr)
            {
                return;
            }

            IUserInterface* userInterface = engine.GetUserInterface();
            if (userInterface == nullptr)
            {
                DrawBrandMark(drawList, min, size);
                return;
            }

            const IUserInterface::FUiTextureHandle texture =
                userInterface->RequestUiTexture(GetBrandIconAssetPath(userInterface->UiScale()), false,
                                                 EUiTextureLifetime::Persistent);
            if (!texture.valid || texture.pixelSize.x <= 0.0f || texture.pixelSize.y <= 0.0f)
            {
                DrawBrandMark(drawList, min, size);
                return;
            }

            const float aspect = texture.pixelSize.x / texture.pixelSize.y;
            float drawWidth = size;
            float drawHeight = drawWidth / aspect;
            if (drawHeight > size)
            {
                drawHeight = size;
                drawWidth = drawHeight * aspect;
            }

            const ImVec2 drawMin(
                min.x + std::floor((size - drawWidth) * 0.5f),
                min.y + std::floor((size - drawHeight) * 0.5f));
            const ImVec2 drawMax(drawMin.x + drawWidth, drawMin.y + drawHeight);
            drawList->AddImage(texture.textureId, drawMin, drawMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                               ImGui::GetColorU32(ImGuiCol_Text));
        }
    } // namespace

    ImFont* GetDefaultFont(NextEngine& engine)
    {
        if (IUserInterface* userInterface = engine.GetUserInterface())
        {
            return userInterface->GetDefaultFont();
        }
        return nullptr;
    }

    ImFont* GetTitleFont(NextEngine& engine)
    {
        if (IUserInterface* userInterface = engine.GetUserInterface())
        {
            return userInterface->GetTitleBarFont();
        }
        return nullptr;
    }

    void ApplyProfessionalTheme()
    {
        Foundation::ApplyTheme();
    }

    FMainMenuGhostScope::FMainMenuGhostScope() : drawList_(ImGui::GetWindowDrawList())
    {
        if (drawList_ != nullptr)
        {
            // ImGui renders menu Selectables as square frames. Keep their text
            // on a foreground channel so the rounded ghost surface can be
            // emitted afterwards while still appearing behind it.
            drawList_->ChannelsSplit(2);
            drawList_->ChannelsSetCurrent(1);
            channelsSplit_ = true;
        }
    }

    FMainMenuGhostScope::~FMainMenuGhostScope()
    {
        if (channelsSplit_)
        {
            drawList_->ChannelsMerge();
        }
    }

    bool FMainMenuGhostScope::BeginMenu(const char* label, const bool enabled)
    {
        if (!channelsSplit_)
        {
            return ImGui::BeginMenu(label, enabled);
        }

        drawList_->ChannelsSetCurrent(1);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        const bool menuOpen = ImGui::BeginMenu(label, enabled);
        ImGui::PopStyleColor(3);

        const bool menuHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        if (menuHovered || menuOpen)
        {
            const ImVec2 inset(1.0f, 1.0f);
            const ImVec2 itemMin = ImGui::GetItemRectMin() + inset;
            const ImVec2 itemMax = ImGui::GetItemRectMax() - inset;
            drawList_->ChannelsSetCurrent(0);
            drawList_->AddRectFilled(itemMin, itemMax, ColorU32(EColor::SurfaceHover), 5.0f);
        }
        drawList_->ChannelsSetCurrent(1);
        return menuOpen;
    }

    void DrawAppTitleBar(NextEngine& engine, const FAppTitleBarConfig& config)
    {
        Foundation::FUiContext context;
        context.imguiContext = ImGui::GetCurrentContext();
        if (NextUI::IUserInterface* ui = engine.GetUserInterface())
        {
            context.defaultFont = ui->GetDefaultFont();
            context.titleFont = config.TitleFont != nullptr ? config.TitleFont : ui->GetTitleBarFont();
            context.metrics = Foundation::FUiMetrics::FromScale(ui->UiScale());
        }
        context.allowWindowCommands = static_cast<bool>(config.OnMinimize) ||
            static_cast<bool>(config.OnToggleMaximize) || static_cast<bool>(config.OnClose);

        Foundation::FAppTitleBarOptions options;
        options.windowId = config.BrandWindowId;
        options.menuWindowId = config.MenuWindowId;
        options.rightWindowId = config.RightWindowId;
        options.appName = config.AppName;
        options.height = config.Height;
        options.rightContentWidth = config.RightContentWidth;
        options.brandHorizontalPadding = config.BrandHorizontalPadding;
        options.brandIconSize = config.BrandIconSize;
        options.brandTextSpacing = config.BrandTextSpacing;
        options.menuTrailingPadding = config.MenuTrailingPadding;
        options.menuHitPadding = config.MenuHitPadding;
        options.isMaximized = config.IsMaximized;
        options.drawBrandIcon = [&engine](ImDrawList* drawList, ImVec2 min, float size)
        {
            DrawBrandIcon(engine, drawList, min, size);
        };
        options.drawMenuBar = config.DrawMenuBar;
        options.drawRightContent = config.DrawRightContent;
        const Foundation::FAppChromeResult result = Foundation::DrawAppTitleBar(context, options);

        if (context.allowWindowCommands)
        {
            engine.ConfigureCustomTitleBarDrag(true, result.dragHeight,
                                               result.dragLeftReservedWidth, result.dragRightReservedWidth);
        }
        switch (result.action)
        {
        case Foundation::EAppChromeAction::Minimize:
            if (config.OnMinimize) config.OnMinimize();
            break;
        case Foundation::EAppChromeAction::ToggleMaximize:
            if (config.OnToggleMaximize) config.OnToggleMaximize();
            break;
        case Foundation::EAppChromeAction::Close:
            if (config.OnClose) config.OnClose();
            break;
        case Foundation::EAppChromeAction::None:
            break;
        }
    }

    void DrawBottomBar(const FBottomBarConfig& config)
    {
        Foundation::FBottomBarOptions options;
        options.windowId = config.WindowId;
        options.height = config.Height;
        options.horizontalPadding = config.HorizontalPadding;
        options.verticalPadding = config.VerticalPadding;
        options.centerWidth = config.CenterWidth;
        options.rightWidth = config.RightWidth;
        options.drawLeftContent = config.DrawLeftContent;
        options.drawCenterContent = config.DrawCenterContent;
        options.drawRightContent = config.DrawRightContent;
        Foundation::DrawBottomBar(options);
    }

    void DrawTooltip(const char* text)
    {
        Foundation::Tooltip(text);
    }

    bool IconButton(const char* label, const char* tooltip, bool active, ImVec2 size)
    {
        return Foundation::IconButton(label, tooltip, active, size);
    }

    bool GhostButton(const char* label, const char* tooltip, ImVec2 size)
    {
        Foundation::FButtonOptions options;
        options.variant = Foundation::EButtonVariant::Ghost;
        options.size = size;
        options.tooltip = tooltip;
        return Foundation::Button(label, options);
    }

    bool ToolbarButton(const char* label, const char* tooltip, bool active, ImVec2 size)
    {
        Foundation::FButtonOptions options;
        options.variant = Foundation::EButtonVariant::Toolbar;
        options.size = size;
        options.tooltip = tooltip;
        options.active = active;
        return Foundation::Button(label, options);
    }

    void PushViewportToolbarStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 3.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Color(EColor::Surface, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Color(EColor::SurfaceHover, 0.72f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Color(EColor::Accent, 0.24f));
        ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::Surface, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.72f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::Accent, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.0f));
    }

    void PopViewportToolbarStyle()
    {
        ImGui::PopStyleColor(7);
        ImGui::PopStyleVar(3);
    }

    bool DrawFlatViewportButton(const char* label, const char* tooltip, bool active, ImVec2 size)
    {
        Foundation::FButtonOptions options;
        options.variant = Foundation::EButtonVariant::Toolbar;
        options.size = size;
        options.tooltip = tooltip;
        options.active = active;
        return Foundation::Button(label, options);
    }

    void PushViewportPopupStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Color(EColor::Background, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_Header, Color(EColor::SurfaceHover, 0.46f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.78f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(EColor::Accent, 0.26f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.0f));
    }

    void PopViewportPopupStyle()
    {
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(5);
    }

    bool DrawViewportComboOption(const char* label, bool selected)
    {
        return Foundation::ComboOption(label, selected);
    }

    void PushToolWindowStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Color(EColor::SurfaceElevated));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Color(EColor::SurfaceHover));
        ImGui::PushStyleColor(ImGuiCol_Header, Color(EColor::SurfaceElevated, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(EColor::Accent, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, Color(EColor::Surface, 0.56f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, Color(EColor::Background, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, Color(EColor::SurfaceElevated, 0.32f));
    }

    void PopToolWindowStyle()
    {
        ImGui::PopStyleColor(8);
        ImGui::PopStyleVar(7);
    }

    void PushToolWindowContentStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    }

    void PopToolWindowContentStyle()
    {
        ImGui::PopStyleVar();
    }

    void BeginFormRow(const char* label, float ratio, float minLabelWidth, float maxLabelWidth)
    {
        Foundation::LabeledRow(label, ratio, minLabelWidth, maxLabelWidth);
    }

    bool BeginOverlayPanel(const FOverlayPanelConfig& config)
    {
        Foundation::FOverlayPanelOptions options;
        options.windowId = config.WindowId;
        options.position = config.Position;
        options.size = config.Size;
        options.padding = config.Padding;
        options.itemSpacing = config.ItemSpacing;
        options.rounding = config.Rounding;
        options.borderSize = config.BorderSize;
        options.borderAlpha = config.BorderAlpha;
        options.backgroundAlpha = config.BackgroundAlpha;
        options.backgroundColor = config.BackgroundColor;
        options.extraFlags = config.ExtraFlags;
        return Foundation::BeginOverlayPanel(options);
    }

    void EndOverlayPanel()
    {
        Foundation::EndOverlayPanel();
    }

    void DrawSelectionAccentStrip(ImVec2 itemMin, ImVec2 itemMax, float verticalPadding,
                                  float horizontalOffset)
    {
        constexpr float stripWidth = 3.0f;
        const float x = itemMin.x + horizontalOffset;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(x, itemMin.y + verticalPadding), ImVec2(x + stripWidth, itemMax.y - verticalPadding),
            ColorU32(EColor::AccentHover), stripWidth * 0.5f);
    }

    void TagChip(const char* label, const ImVec4& color, const char* tooltip)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, 0.18f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x, color.y, color.z, 0.35f));
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::SmallButton(label);
        if (tooltip != nullptr && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", tooltip);
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
    }

    bool SegmentedPillsIndexed(const char* id, std::span<const char* const> labels, int& selectedIndex,
                               float totalWidth, float itemHeight)
    {
        if (labels.empty())
        {
            return false;
        }

        ImGui::PushID(id);
        const float available = totalWidth > 0.0f ? totalWidth : ImGui::GetContentRegionAvail().x;
        constexpr float spacing = 3.0f;
        const float count = static_cast<float>(labels.size());
        const float itemWidth = std::max(20.0f, (available - spacing * (count - 1.0f)) / count);

        bool changed = false;
        for (size_t i = 0; i < labels.size(); ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine(0.0f, spacing);
            }
            const bool selected = selectedIndex == static_cast<int>(i);

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::Accent, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::AccentHover));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::Accent));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::SurfaceElevated, 0.45f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.75f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::SurfaceHover));
                ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
            }

            const char* label = labels[i] != nullptr ? labels[i] : "";
            if (ImGui::Button(label, ImVec2(itemWidth, itemHeight)) && !selected)
            {
                selectedIndex = static_cast<int>(i);
                changed = true;
            }
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();
        }
        ImGui::PopID();
        return changed;
    }

    bool SegmentedPills(const char* id, std::span<const FPillOption> options, int& value, float totalWidth,
                        float itemHeight)
    {
        if (options.empty())
        {
            return false;
        }
        std::vector<const char*> labels;
        labels.reserve(options.size());
        int selectedIndex = -1;
        for (size_t i = 0; i < options.size(); ++i)
        {
            labels.push_back(options[i].Label);
            if (options[i].Value == value)
            {
                selectedIndex = static_cast<int>(i);
            }
        }
        if (SegmentedPillsIndexed(id, labels, selectedIndex, totalWidth, itemHeight) &&
            selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < options.size())
        {
            value = options[static_cast<size_t>(selectedIndex)].Value;
            return true;
        }
        return false;
    }

    int PresetPills(const char* id, std::span<const FPresetPill> pills, float totalWidth, float itemHeight)
    {
        if (pills.empty())
        {
            return -1;
        }

        ImGui::PushID(id);
        const float available = totalWidth > 0.0f ? totalWidth : ImGui::GetContentRegionAvail().x;
        constexpr float spacing = 4.0f;
        const float count = static_cast<float>(pills.size());
        const float itemWidth = std::max(20.0f, (available - spacing * (count - 1.0f)) / count);

        int clickedIndex = -1;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        for (size_t i = 0; i < pills.size(); ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine(0.0f, spacing);
            }
            const bool selected = pills[i].Selected;
            if (selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::Accent, 0.78f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::AccentHover));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::Accent));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            if (ImGui::Button(pills[i].Label ? pills[i].Label : "", ImVec2(itemWidth, itemHeight)))
            {
                clickedIndex = static_cast<int>(i);
            }
            if (selected)
            {
                ImGui::PopStyleColor(4);
            }
        }
        ImGui::PopStyleVar();
        ImGui::PopID();
        return clickedIndex;
    }

    bool Draw3AxisFloatDrag(const char* label, float value[3], float speed,
                            float min, float max, float resetValue, float labelWidth, const char* format)
    {
        if (value == nullptr)
        {
            return false;
        }
        const char* resolvedFormat = (format != nullptr && format[0] != '\0') ? format : "%.3f";

        ImGui::PushID(label);
        const float availWidth = ImGui::GetContentRegionAvail().x;
        constexpr float resetBtnWidth = 22.0f;
        const float frameHeight = ImGui::GetFrameHeight();
        ImFont* font = ImGui::GetFont();
        const float textSize = ImGui::GetFontSize();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        if (label != nullptr && label[0] != '\0' && labelWidth > 0.0f)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(Color(EColor::TextMuted), "%s", label);
            ImGui::SameLine(labelWidth);
        }
        else
        {
            labelWidth = 0.0f;
        }

        constexpr float axisGap = 6.0f;
        const float inputAreaWidth = std::max(120.0f, availWidth - labelWidth - resetBtnWidth - 6.0f);
        const float axisWidth = std::floor((inputAreaWidth - axisGap * 2.0f) / 3.0f);
        constexpr float tagWidth = 19.0f;
        const float dragWidth = std::max(26.0f, axisWidth - tagWidth);

        bool changed = false;
        struct FAxisDef
        {
            const char* name;
            const char* tagId;
            const char* inputId;
            ImU32 tagCol;
            ImU32 tagHoverCol;
            ImU32 tagActiveCol;
        };
        static constexpr FAxisDef axes[3] = {
            {"X", "##tag_X", "##axis_X",
             IM_COL32(208, 52, 58, 235), IM_COL32(232, 68, 74, 255), IM_COL32(180, 38, 44, 255)},
            {"Y", "##tag_Y", "##axis_Y",
             IM_COL32(46, 160, 67, 235), IM_COL32(56, 185, 78, 255), IM_COL32(35, 135, 52, 255)},
            {"Z", "##tag_Z", "##axis_Z",
             IM_COL32(38, 115, 222, 235), IM_COL32(52, 138, 245, 255), IM_COL32(28, 92, 185, 255)},
        };

        for (int i = 0; i < 3; ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine(0.0f, axisGap);
            }

            const ImVec2 tagMin = ImGui::GetCursorScreenPos();
            const ImVec2 tagMax(tagMin.x + tagWidth, tagMin.y + frameHeight);
            const ImVec2 dragMin(tagMax.x, tagMin.y);
            const ImVec2 dragMax(dragMin.x + dragWidth, tagMin.y + frameHeight);
            const ImVec2 axisTotalMin = tagMin;
            const ImVec2 axisTotalMax = dragMax;

            ImGui::InvisibleButton(axes[i].tagId, ImVec2(tagWidth, frameHeight));
            const bool tagHovered = ImGui::IsItemHovered();
            const bool tagActive = ImGui::IsItemActive();

            if (tagHovered)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                ImGui::SetTooltip("%s Axis (Drag to scrub, double-click to reset)", axes[i].name);
            }

            if (tagActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                value[i] += ImGui::GetIO().MouseDelta.x * speed;
                if (min < max)
                {
                    value[i] = std::clamp(value[i], min, max);
                }
                changed = true;
            }
            if (tagHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                value[i] = resetValue;
                changed = true;
            }

            const ImU32 tagColor = tagActive ? axes[i].tagActiveCol : (tagHovered ? axes[i].tagHoverCol : axes[i].tagCol);
            drawList->AddRectFilled(tagMin, tagMax, tagColor, 4.0f, ImDrawFlags_RoundCornersLeft);

            const ImVec2 charSize = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, axes[i].name);
            const ImVec2 charPos(tagMin.x + (tagWidth - charSize.x) * 0.5f,
                                 tagMin.y + (frameHeight - charSize.y) * 0.5f);
            drawList->AddText(font, textSize, charPos, IM_COL32(255, 255, 255, 245), axes[i].name);

            ImGui::SameLine(0.0f, 0.0f);

            const bool isDragAreaHovered = ImGui::IsMouseHoveringRect(dragMin, dragMax);
            const ImU32 dragBgCol = ColorU32(
                isDragAreaHovered ? EColor::SurfaceHover : EColor::SurfaceElevated,
                isDragAreaHovered ? 0.90f : 0.65f);
            drawList->AddRectFilled(dragMin, dragMax, dragBgCol, 4.0f, ImDrawFlags_RoundCornersRight);

            ImGui::SetNextItemWidth(dragWidth);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));

            if (ImGui::DragFloat(axes[i].inputId, &value[i], speed, min, max, resolvedFormat))
            {
                changed = true;
            }
            const bool dragActive = ImGui::IsItemActive();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();

            if (dragActive)
            {
                drawList->AddRect(axisTotalMin, axisTotalMax, ColorU32(EColor::Accent, 0.70f), 4.0f, 0, 1.0f);
            }
        }

        ImGui::SameLine(0.0f, 6.0f);
        const bool isModified = (std::abs(value[0] - resetValue) > 0.0001f ||
                                 std::abs(value[1] - resetValue) > 0.0001f ||
                                 std::abs(value[2] - resetValue) > 0.0001f);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::SurfaceHover, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Text, Color(
            isModified ? EColor::Accent : EColor::TextDim,
            isModified ? 0.95f : 0.40f));

        if (ImGui::Button(ICON_FA_ROTATE_LEFT, ImVec2(resetBtnWidth, frameHeight)))
        {
            value[0] = resetValue;
            value[1] = resetValue;
            value[2] = resetValue;
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            const std::string tooltip = isModified
                ? fmt::format("Reset {} to default ({:.1f})", label ? label : "value", resetValue)
                : fmt::format("{} is at default value ({:.1f})", label ? label : "Value", resetValue);
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();

        ImGui::PopID();
        return changed;
    }

    bool DrawSettingSliderRow(const char* label, ImGuiDataType dataType, void* value,
                              const void* minValue, const void* maxValue, const char* format,
                              float dragSpeed, float valueWidth)
    {
        ImGui::PushID(label);
        BeginFormRow(label);

        const float sliderWidth = std::max(40.0f, ImGui::GetContentRegionAvail().x - valueWidth - 6.0f);
        bool changed = false;

        ImGui::SetNextItemWidth(sliderWidth);
        changed |= ImGui::SliderScalar("##Slider", dataType, value, minValue, maxValue, format);

        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetNextItemWidth(valueWidth);
        changed |= ImGui::DragScalar("##Value", dataType, value, dragSpeed, minValue, maxValue, format);

        ImGui::PopID();
        return changed;
    }

    bool DrawFloatSettingRow(const char* label, float* value, float minValue, float maxValue,
                             const char* format, float dragSpeed, float valueWidth)
    {
        return DrawSettingSliderRow(label, ImGuiDataType_Float, value, &minValue, &maxValue,
                                    format, dragSpeed, valueWidth);
    }

    bool DrawIntSettingRow(const char* label, int* value, int minValue, int maxValue,
                           const char* format, float valueWidth)
    {
        return DrawSettingSliderRow(label, ImGuiDataType_S32, value, &minValue, &maxValue,
                                    format, 1.0f, valueWidth);
    }

    bool DrawCheckboxSettingRow(const char* label, bool* value)
    {
        return DrawSettingRow(label, [value]() { return ImGui::Checkbox("##Value", value); });
    }

    void SameLineRightAligned(float groupWidth, float minLeadingGap)
    {
        const float available = ImGui::GetContentRegionAvail().x;
        if (available > groupWidth + minLeadingGap)
        {
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - groupWidth - ImGui::GetStyle().WindowPadding.x);
        }
        else
        {
            ImGui::SameLine();
        }
    }

    std::string TruncateWithEllipsis(const std::string& text, float maxWidth)
    {
        if (text.empty() || ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
        {
            return text;
        }

        static constexpr const char* kEllipsis = "...";
        const float ellipsisWidth = ImGui::CalcTextSize(kEllipsis).x;
        if (ellipsisWidth >= maxWidth)
        {
            return kEllipsis;
        }

        const char* begin = text.c_str();
        const char* end = begin + text.size();
        const char* remaining = begin;
        ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), maxWidth - ellipsisWidth, 0.0f, begin, end,
                                        &remaining);
        return std::string(begin, remaining) + kEllipsis;
    }

    void DrawPanelHeader(const char* icon, const char* title, const char* subtitle,
                         const char* badgeText, ImVec4 badgeColor)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::Text));
        if (icon != nullptr && icon[0] != '\0')
        {
            ImGui::TextColored(Color(EColor::AccentHover), "%s", icon);
            ImGui::SameLine(0.0f, 6.0f);
        }
        ImGui::TextUnformatted(title ? title : "");
        ImGui::PopStyleColor();

        if (badgeText != nullptr && badgeText[0] != '\0')
        {
            const float badgeWidth = ImGui::CalcTextSize(badgeText).x + 14.0f;
            SameLineRightAligned(badgeWidth, 8.0f);
            const ImVec4 resolvedBadgeColor = badgeColor.w > 0.0f ? badgeColor : Color(EColor::AccentHover);
            TagChip(badgeText, resolvedBadgeColor);
        }

        if (subtitle != nullptr && subtitle[0] != '\0')
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
            ImGui::TextUnformatted(subtitle);
            ImGui::PopStyleColor();
        }

        DrawThinSeparator(0.9f);
    }

    void DrawThinSeparator(float alpha)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddLine(pos, ImVec2(pos.x + ImGui::GetContentRegionAvail().x, pos.y), ColorU32(EColor::Border, alpha), 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    void DrawVerticalSeparator(float height, float spacing, float alpha)
    {
        ImGui::SameLine(0.0f, spacing);
        const ImVec2 separatorMin = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(separatorMin.x, separatorMin.y + 1.0f),
            ImVec2(separatorMin.x, separatorMin.y + height - 1.0f),
            ColorU32(EColor::Border, alpha));
        ImGui::Dummy(ImVec2(1.0f, height));
        ImGui::SameLine(0.0f, spacing);
    }

    void DrawProgressBar(float fraction, ImVec4 color, ImVec2 size)
    {
        fraction = std::clamp(fraction, 0.0f, 1.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, pos + size, ColorU32(EColor::Background, 0.86f), 4.0f);
        drawList->AddRect(pos, pos + size, ColorU32(EColor::BorderStrong, 0.88f), 4.0f);
        if (fraction > 0.0f)
        {
            const float fillWidth = std::max(2.0f, size.x * fraction);
            drawList->AddRectFilled(pos + ImVec2(1.0f, 1.0f), pos + ImVec2(fillWidth - 1.0f, size.y - 1.0f),
                                    ImGui::GetColorU32(color), 3.0f);
        }
        ImGui::Dummy(size);
    }

    bool ModeRailButton(const char* icon, const char* tooltip, bool active, float buttonSize)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::Accent, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::Accent, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::Accent, 0.45f));
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::AccentHover));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.65f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::SurfaceHover, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
        }
        const bool pressed = ImGui::Button(icon ? icon : "?", ImVec2(buttonSize, buttonSize));
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);

        if (active)
        {
            DrawSelectionAccentStrip(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), 6.0f, -4.0f);
        }

        DrawTooltip(tooltip);
        return pressed;
    }

    bool BeginFloatingPanel(const char* id, const char* icon, const char* title, bool* pOpen,
                             ImVec2 position, ImVec2 size, ImVec2 pivot, const bool detachedViewport)
    {
        if (detachedViewport && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            ImGuiWindowClass windowClass;
            windowClass.ParentViewportId = 0;
            windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
            ImGui::SetNextWindowClass(&windowClass);
            ImGui::SetNextWindowPos(position, ImGuiCond_Appearing, pivot);
            ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
        }
        else
        {
            ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
            ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        }
        ImGui::SetNextWindowBgAlpha(kDetailPanelBackgroundAlpha);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, kDetailPanelBorderAlpha));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(EColor::Background, kDetailPanelBackgroundAlpha));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs;
        if (!detachedViewport)
        {
            flags |= ImGuiWindowFlags_NoMove;
        }

        const bool visible = ImGui::Begin(id, nullptr, flags);
        if (!visible)
        {
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            return false;
        }

        constexpr float headerHeight = 38.0f;
        const ImVec2 winPos = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            winPos, ImVec2(winPos.x + winSize.x, winPos.y + headerHeight),
            ColorU32(EColor::Surface, kDetailPanelHeaderAlpha), 8.0f, ImDrawFlags_RoundCornersTop);
        drawList->AddLine(
            ImVec2(winPos.x, winPos.y + headerHeight),
            ImVec2(winPos.x + winSize.x, winPos.y + headerHeight),
            ColorU32(EColor::Border, kDetailPanelBorderAlpha));

        const float textY = winPos.y + (headerHeight - ImGui::GetTextLineHeight()) * 0.5f;
        const float textX = winPos.x + 14.0f;
        const ImU32 iconCol = ColorU32(EColor::AccentHover);
        const ImU32 titleCol = ColorU32(EColor::Text);
        if (icon != nullptr && icon[0] != '\0')
        {
            drawList->AddText(ImVec2(textX, textY), iconCol, icon);
            const float iconWidth = ImGui::CalcTextSize(icon).x;
            drawList->AddText(ImVec2(textX + iconWidth + 8.0f, textY), titleCol, title ? title : "");
        }
        else
        {
            drawList->AddText(ImVec2(textX, textY), titleCol, title ? title : "");
        }

        if (pOpen != nullptr)
        {
            const float closeSize = 20.0f;
            ImGui::SetCursorScreenPos(ImVec2(winPos.x + winSize.x - closeSize - 10.0f,
                                             winPos.y + (headerHeight - closeSize) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::SurfaceHover));
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(closeSize, closeSize)))
            {
                *pOpen = false;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
        }

        ImGui::SetCursorScreenPos(ImVec2(winPos.x, winPos.y + headerHeight));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##FloatingPanelBody", ImVec2(0, 0), false, ImGuiWindowFlags_NoBackground);
        return true;
    }

    void EndFloatingPanel()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    bool BeginDetailPanel(const FDetailPanelConfig& config)
    {
        if (!BeginFloatingPanel(config.WindowId, config.Icon, config.Title, config.Open,
                                config.Position, config.Size, config.Pivot, config.DetachedViewport))
        {
            return false;
        }

        BeginInsetPanel(config.ContentWindowId, ImVec2(0.0f, 0.0f), config.ContentBorder,
                        config.ContentFlags, config.ContentPadding, config.ContentBackgroundAlpha);
        return true;
    }

    void EndDetailPanel()
    {
        EndInsetPanel();
        EndFloatingPanel();
    }

    bool BeginCollapsibleSidePanel(const FCollapsibleSidePanelConfig& config)
    {
        ImGui::SetNextWindowPos(config.Position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(config.Size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(config.BackgroundAlpha);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
            config.ExtraFlags;
        if (config.Collapsed)
        {
            flags |= ImGuiWindowFlags_NoScrollbar;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            config.Collapsed ? config.CollapsedPadding : config.ExpandedPadding);
        const bool visible = ImGui::Begin(config.WindowId, nullptr, flags);
        ImGui::PopStyleVar(3);
        if (!visible)
        {
            ImGui::End();
            return false;
        }
        return true;
    }

    void EndCollapsibleSidePanel()
    {
        ImGui::End();
    }

    bool BeginModernModal(const FModernModalConfig& config)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport != nullptr)
        {
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            const float width = std::min(config.TargetSize.x, std::max(240.0f, viewport->WorkSize.x - config.ViewportMargin));
            const float height = std::min(config.TargetSize.y, std::max(180.0f, viewport->WorkSize.y - config.ViewportMargin));
            ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Appearing);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, config.Rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, config.Padding);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(EColor::Surface, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Color(EColor::Surface, 0.98f));

        const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
            config.ExtraFlags;
        if (!ImGui::BeginPopupModal(config.PopupId, config.Open, windowFlags))
        {
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(3);
            return false;
        }
        return true;
    }

    void EndModernModal()
    {
        ImGui::EndPopup();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
    }

    bool DrawModalHeader(const char* icon, const char* title, const char* subtitle,
                         ImFont* titleFont, const bool allowClose)
    {
        constexpr float kHeaderHeight = 56.0f;
        const ImVec2 dialogPos = ImGui::GetWindowPos();
        const ImVec2 dialogSize = ImGui::GetWindowSize();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const ImVec2 headerMin = dialogPos;
        const ImVec2 headerMax(dialogPos.x + dialogSize.x, dialogPos.y + kHeaderHeight);

        drawList->AddRectFilled(headerMin, headerMax, ColorU32(EColor::Background, 0.65f), 8.0f,
                                ImDrawFlags_RoundCornersTop);
        drawList->AddLine(ImVec2(headerMin.x, headerMax.y), headerMax, ColorU32(EColor::Border, 0.50f), 1.0f);

        constexpr float headerPadX = 18.0f;
        float titleX = dialogPos.x + headerPadX;
        if (icon != nullptr && icon[0] != '\0')
        {
            constexpr float iconBoxSize = 34.0f;
            const float iconBoxPosY = dialogPos.y + (kHeaderHeight - iconBoxSize) * 0.5f;
            const ImVec2 iconBoxMin(dialogPos.x + headerPadX, iconBoxPosY);
            const ImVec2 iconBoxMax(iconBoxMin.x + iconBoxSize, iconBoxMin.y + iconBoxSize);

            drawList->AddRectFilled(iconBoxMin, iconBoxMax, ColorU32(EColor::Accent, 0.16f), 6.0f);
            drawList->AddRect(iconBoxMin, iconBoxMax, ColorU32(EColor::AccentHover, 0.45f), 6.0f);

            const ImVec2 headerIconSize = ImGui::CalcTextSize(icon);
            drawList->AddText(
                ImVec2(iconBoxMin.x + (iconBoxSize - headerIconSize.x) * 0.5f,
                       iconBoxMin.y + (iconBoxSize - headerIconSize.y) * 0.5f),
                ColorU32(EColor::AccentHover), icon);
            titleX = iconBoxMax.x + 12.0f;
        }

        if (titleFont != nullptr)
        {
            ImGui::PushFont(titleFont);
        }
        const float titleY = subtitle != nullptr && subtitle[0] != '\0'
            ? dialogPos.y + 9.0f
            : dialogPos.y + (kHeaderHeight - ImGui::GetTextLineHeight()) * 0.5f;
        drawList->AddText(ImVec2(titleX, titleY), ColorU32(EColor::Text), title ? title : "");
        if (titleFont != nullptr)
        {
            ImGui::PopFont();
        }

        if (subtitle != nullptr && subtitle[0] != '\0')
        {
            drawList->AddText(ImVec2(titleX, dialogPos.y + 32.0f), ColorU32(EColor::TextDim), subtitle);
        }

        bool closeClicked = false;
        if (allowClose)
        {
            constexpr float closeBtnSize = 28.0f;
            const ImVec2 closeBtnPos(dialogPos.x + dialogSize.x - closeBtnSize - 16.0f,
                                     dialogPos.y + (kHeaderHeight - closeBtnSize) * 0.5f);
            ImGui::SetCursorScreenPos(closeBtnPos);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::SurfaceElevated));
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
            if (ImGui::Button(ICON_FA_XMARK "##CloseModal", ImVec2(closeBtnSize, closeBtnSize)))
            {
                closeClicked = true;
            }
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }

        ImGui::SetCursorScreenPos(ImVec2(dialogPos.x, dialogPos.y + kHeaderHeight));
        return closeClicked;
    }

    FInteractiveCardState DrawInteractiveCard(const FInteractiveCardConfig& config)
    {
        FInteractiveCardState state;
        state.Min = ImGui::GetCursorScreenPos();
        const float width = config.Size.x > 0.0f ? config.Size.x : ImGui::GetContentRegionAvail().x;
        const float height = config.Size.y > 0.0f ? config.Size.y : 54.0f;
        state.Max = ImVec2(state.Min.x + width, state.Min.y + height);

        ImGui::InvisibleButton(config.Id ? config.Id : "##InteractiveCard", ImVec2(width, height));
        state.Hovered = ImGui::IsItemHovered();
        state.Clicked = ImGui::IsItemClicked();

        const ImU32 cardBg = config.Selected ? ColorU32(EColor::SurfaceHover, 0.92f)
                                             : (state.Hovered ? ColorU32(EColor::SurfaceElevated, 0.70f)
                                                              : ColorU32(EColor::Surface, 0.35f));
        const ImU32 cardBorder = config.Selected ? ColorU32(EColor::AccentHover, 0.85f)
                                                 : (state.Hovered ? ColorU32(EColor::BorderStrong, 0.60f)
                                                                  : ColorU32(EColor::Border, 0.30f));

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(state.Min, state.Max, cardBg, config.Rounding);
        drawList->AddRect(state.Min, state.Max, cardBorder, config.Rounding, 0, config.Selected ? 1.5f : 1.0f);

        if (config.Selected && config.ShowSelectionStrip)
        {
            drawList->AddRectFilled(ImVec2(state.Min.x + 2.0f, state.Min.y + 8.0f),
                                    ImVec2(state.Min.x + 5.0f, state.Max.y - 8.0f),
                                    ColorU32(EColor::AccentHover), 2.0f);
        }
        return state;
    }

    bool BeginPanelSection(const char* label, bool defaultOpen)
    {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(EColor::SurfaceHover, 0.65f));
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::Text, 0.92f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 4.0f));

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
        if (defaultOpen)
        {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        const bool open = ImGui::CollapsingHeader(label ? label : "", flags);

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);

        if (open)
        {
            ImGui::PushID(label);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
            ImGui::Dummy(ImVec2(0.0f, 1.0f));
        }
        return open;
    }

    void EndPanelSection()
    {
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    void Sparkline(const float* values, int count, ImVec2 size, ImVec4 color,
                   float scaleMin, float scaleMax, bool baselineAtZero)
    {
        if (values == nullptr || count <= 1)
        {
            ImGui::Dummy(size);
            return;
        }

        if (size.x <= 0.0f)
        {
            size.x = ImGui::GetContentRegionAvail().x;
        }
        if (size.y <= 0.0f)
        {
            size.y = ImGui::GetTextLineHeight() * 1.6f;
        }

        if (scaleMin == FLT_MAX || scaleMax == FLT_MAX)
        {
            float lo = baselineAtZero ? 0.0f : values[0];
            float hi = values[0];
            for (int i = 1; i < count; ++i)
            {
                hi = std::max(hi, values[i]);
                if (!baselineAtZero)
                {
                    lo = std::min(lo, values[i]);
                }
            }
            scaleMin = lo;
            scaleMax = hi;
        }
        const float range = std::max(0.0001f, scaleMax - scaleMin);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, origin + size, ColorU32(EColor::Background, 0.55f), 4.0f);

        const float stepX = size.x / static_cast<float>(count - 1);
        ImU32 lineCol = ImGui::GetColorU32(color);
        ImU32 fillCol = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w * 0.18f));

        std::vector<ImVec2> pts;
        pts.reserve(count);
        for (int i = 0; i < count; ++i)
        {
            const float t = (values[i] - scaleMin) / range;
            const float x = origin.x + stepX * static_cast<float>(i);
            const float y = origin.y + size.y - 2.0f - t * (size.y - 4.0f);
            pts.emplace_back(x, y);
        }

        for (int i = 0; i + 1 < count; ++i)
        {
            ImVec2 quad[4] = {pts[i], pts[i + 1],
                              ImVec2(pts[i + 1].x, origin.y + size.y),
                              ImVec2(pts[i].x, origin.y + size.y)};
            drawList->AddConvexPolyFilled(quad, 4, fillCol);
        }

        drawList->AddPolyline(pts.data(), count, lineCol, ImDrawFlags_None, 1.5f);
        ImGui::Dummy(size);
    }
} // namespace NextUI::Theme
