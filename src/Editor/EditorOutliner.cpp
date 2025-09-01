#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "EditorGUI.h"
#include "Assets/Model.hpp"
#include "Assets/Node.h"
#include "Assets/Scene.hpp"


void DrawNode(Assets::Scene* scene, Assets::Node* node)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    bool selected = scene->GetSelectedId() == node->GetInstanceId();
    ImGuiTreeNodeFlags flag = ImGuiTreeNodeFlags_FramePadding |
        (selected ? ImGuiTreeNodeFlags_Selected : 0) |
        (node->Children().empty() ? ImGuiTreeNodeFlags_Leaf : 0);

    
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? Editor::ActiveColor : ImGui::GetColorU32(ImGuiCol_Text));
    if (ImGui::TreeNodeEx(((node->GetModel() == -1 ? ICON_FA_CIRCLE_NOTCH : ICON_FA_CUBE) + std::string(" ") + node->GetName()).c_str(), flag))
    {
        ImGui::PopStyleColor();
        if (ImGui::IsItemClicked())
        {
            scene->SetSelectedId(node->GetInstanceId());
        }
        auto& childrenNodes = node->Children();
        for (auto& child : childrenNodes)
        {
            DrawNode(scene, child.get());
        }
        ImGui::TreePop();
    }
    else
    {
        ImGui::PopStyleColor();
    }
}


void Editor::GUI::ShowSidebar(Assets::Scene* scene)
{
    ImGui::Begin("Outliner", NULL);

    {
        ImGui::TextDisabled("NOTE");
        ImGui::SameLine();
        utils::HelpMarker
        ("ALL SCENE NODES\n"
            "limited to 1000 nodes\n"
            "select and view node properties\n");
        ImGui::Separator();
        
        ImGui::Text("Nodes");
        ImGui::Separator();

        ImGui::BeginChild("ListBox", ImVec2(0, -50));
        
        if (ImGui::BeginTable("NodesList", 1, ImGuiTableFlags_NoBordersInBodyUntilResize | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("NodeName");
            auto& allnodes = scene->Nodes();
            uint32_t limit = 1000;
            for (auto& node : allnodes)
            {
                if (node->GetParent() != nullptr)
                {
                    continue;
                }

                DrawNode(scene, node.get());
              
                if (limit-- <= 0)
                {
                    break;
                }
            }
            ImGui::EndTable();
        }

        ImGui::EndChild();

        ImGui::Spacing();

        ImGui::Text("%d Nodes", scene->Nodes().size());

        ImGui::Spacing();
        
        if ((ImGui::GetIO().KeyAlt) && (ImGui::IsKeyPressed(ImGuiKey_F4)))
        {
            state = false;
        }
    }
    ImGui::End();
}
