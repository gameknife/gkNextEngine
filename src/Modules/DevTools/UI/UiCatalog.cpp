#include "Engine/Common/CoreMinimal.hpp"

#include "UiCatalog.hpp"

#include "Modules/NextUI/NextUI.hpp"

namespace DevTools
{
    void DrawUiCatalog(bool& open)
    {
        if (!open)
        {
            return;
        }
        NextUI::FScopedWindow window("UI Foundation Catalog###UiFoundationCatalog", &open);
        if (!window)
        {
            return;
        }

        ImGui::TextUnformatted("Semantic buttons");
        for (const auto variant : {
                 NextUI::EButtonVariant::Primary,
                 NextUI::EButtonVariant::Secondary,
                 NextUI::EButtonVariant::Ghost,
                 NextUI::EButtonVariant::Toolbar,
                 NextUI::EButtonVariant::Danger})
        {
            NextUI::FButtonOptions options;
            options.variant = variant;
            options.tooltip = "Unified tooltip, including disabled hover";
            NextUI::Button("Button", options);
            ImGui::SameLine();
        }
        ImGui::NewLine();
        NextUI::Button("Active", {.variant = NextUI::EButtonVariant::Toolbar, .active = true});
        ImGui::SameLine();
        NextUI::Button("Disabled", {.tooltip = "Disabled reason", .disabled = true});

        ImGui::SeparatorText("Containers");
        {
            NextUI::FScopedToolbar toolbar("##CatalogToolbar", ImVec2(0.0f, 38.0f));
            if (toolbar)
            {
                NextUI::IconButton("A", "Toolbar icon");
                ImGui::SameLine();
                NextUI::IconButton("B", "Selected toolbar icon", true);
            }
        }
        {
            NextUI::FScopedInsetPanel inset("##CatalogInset", ImVec2(0.0f, 90.0f));
            if (inset)
            {
                NextUI::LabeledRow("Labeled row");
                ImGui::SetNextItemWidth(-1.0f);
                static char catalogText[32] = "Value";
                ImGui::InputText("##CatalogText", catalogText, sizeof(catalogText));
            }
        }

        ImGui::SeparatorText("Combo option");
        if (ImGui::BeginCombo("##CatalogCombo", "Selected"))
        {
            NextUI::ComboOption("Selected", true);
            NextUI::ComboOption("Alternative", false);
            ImGui::EndCombo();
        }
    }
}
