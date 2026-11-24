#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <imgui.h>

namespace NextUI
{
    class FScopedStyle final
    {
    public:
        FScopedStyle() = default;
        ~FScopedStyle()
        {
            if (colorCount_ > 0) ImGui::PopStyleColor(colorCount_);
            if (variableCount_ > 0) ImGui::PopStyleVar(variableCount_);
        }

        GK_NON_COPIABLE(FScopedStyle)

        FScopedStyle& Add(const ImGuiCol index, const ImVec4 value)
        {
            ImGui::PushStyleColor(index, value);
            ++colorCount_;
            return *this;
        }

        FScopedStyle& Add(const ImGuiStyleVar index, const float value)
        {
            ImGui::PushStyleVar(index, value);
            ++variableCount_;
            return *this;
        }

        FScopedStyle& Add(const ImGuiStyleVar index, const ImVec2 value)
        {
            ImGui::PushStyleVar(index, value);
            ++variableCount_;
            return *this;
        }

    private:
        int colorCount_ = 0;
        int variableCount_ = 0;
    };

    class FScopedId final
    {
    public:
        explicit FScopedId(const char* id) { ImGui::PushID(id); }
        explicit FScopedId(const int id) { ImGui::PushID(id); }
        ~FScopedId() { ImGui::PopID(); }
        GK_NON_COPIABLE(FScopedId)
    };

    class FScopedDisabled final
    {
    public:
        explicit FScopedDisabled(const bool disabled = true) { ImGui::BeginDisabled(disabled); }
        ~FScopedDisabled() { ImGui::EndDisabled(); }
        GK_NON_COPIABLE(FScopedDisabled)
    };

    class FScopedWindow final
    {
    public:
        FScopedWindow(const char* name, bool* open = nullptr, const ImGuiWindowFlags flags = 0)
            : visible_(ImGui::Begin(name, open, flags))
        {
        }
        ~FScopedWindow() { ImGui::End(); }
        GK_NON_COPIABLE(FScopedWindow)
        explicit operator bool() const { return visible_; }

    private:
        bool visible_ = false;
    };

    class FScopedChild final
    {
    public:
        FScopedChild(const char* id,
                     const ImVec2 size = ImVec2(),
                     const ImGuiChildFlags childFlags = 0,
                     const ImGuiWindowFlags windowFlags = 0)
            : visible_(ImGui::BeginChild(id, size, childFlags, windowFlags))
        {
        }
        ~FScopedChild() { ImGui::EndChild(); }
        GK_NON_COPIABLE(FScopedChild)
        explicit operator bool() const { return visible_; }

    private:
        bool visible_ = false;
    };
}
