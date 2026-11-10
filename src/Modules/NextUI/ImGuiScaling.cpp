#include "Modules/NextUI/ImGuiScaling.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <algorithm>

namespace NextUI::Scaling
{
    namespace
    {
        ImVec2 GetFramebufferScale()
        {
            const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
            return ImVec2(scale.x > 0.0f ? scale.x : 1.0f,
                          scale.y > 0.0f ? scale.y : 1.0f);
        }
    }

    float GetUiScale(const float baseFontSize)
    {
        if (ImGui::GetCurrentContext() == nullptr || baseFontSize <= 0.0f)
        {
            return 1.0f;
        }
        const float fontSize = ImGui::GetFontSize();
        return fontSize > 0.0f ? fontSize / baseFontSize : 1.0f;
    }

    float GetViewportUiScale(const ImGuiViewport* viewport, float baseWidth, float baseHeight)
    {
        if (!viewport || viewport->Size.x <= 0.0f || viewport->Size.y <= 0.0f || baseWidth <= 0.0f || baseHeight <= 0.0f)
        {
            return 1.0f;
        }

        return std::max(0.75f, std::min(viewport->Size.x / baseWidth, viewport->Size.y / baseHeight));
    }

    ImVec2 MainFramebufferToImGuiPoint(const ImVec2& position)
    {
        const ImVec2 scale = GetFramebufferScale();
        const ImVec2 origin = ImGui::GetMainViewport()->Pos;
        return ImVec2(origin.x + position.x / scale.x, origin.y + position.y / scale.y);
    }

    ImVec2 ImGuiToMainFramebufferPoint(const ImVec2& position)
    {
        const ImVec2 scale = GetFramebufferScale();
        const ImVec2 origin = ImGui::GetMainViewport()->Pos;
        return ImVec2((position.x - origin.x) * scale.x, (position.y - origin.y) * scale.y);
    }

    FViewportRect MainFramebufferToImGuiViewport(const ImVec2& position, const ImVec2& size)
    {
        const ImVec2 scale = GetFramebufferScale();
        return {
            .Position = MainFramebufferToImGuiPoint(position),
            .Size = ImVec2(size.x / scale.x, size.y / scale.y),
        };
    }

    FViewportRect ImGuiToMainFramebufferViewport(const ImVec2& position, const ImVec2& size)
    {
        const ImVec2 scale = GetFramebufferScale();
        return {
            .Position = ImGuiToMainFramebufferPoint(position),
            .Size = ImVec2(size.x * scale.x, size.y * scale.y),
        };
    }

    void SyncImGuiViewportToSwapChain(NextEngine& engine, const ImVec2& position, const ImVec2& size)
    {
        const FViewportRect framebufferViewport = ImGuiToMainFramebufferViewport(position, size);
        engine.GetRenderer().SwapChain().UpdateOutputViewport(
            Utilities::Math::floorToInt(framebufferViewport.Position.x),
            Utilities::Math::floorToInt(framebufferViewport.Position.y),
            Utilities::Math::ceilToInt(std::max(1.0f, framebufferViewport.Size.x)),
            Utilities::Math::ceilToInt(std::max(1.0f, framebufferViewport.Size.y)));
    }
}
