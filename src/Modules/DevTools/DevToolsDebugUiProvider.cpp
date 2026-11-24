#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/DevTools/DevToolsDebugUiProvider.hpp"

#include "Engine/Rendering/ExternalPassRegistry.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Modules/DevTools/AuxDrawPass.hpp"
#include "Modules/DevTools/AuxDrawSystem.hpp"
#include "Modules/DevTools/CVarEditorPanel.hpp"
#include "Modules/DevTools/ConsoleLogBuffer.hpp"
#include "Modules/DevTools/GraphicsDebugPanel.hpp"
#include "Modules/DevTools/PhysicsDebugOverlay.hpp"
#include "Modules/DevTools/UI/UiCatalog.hpp"
#include "Modules/DevTools/UiDevPanels.hpp"

namespace DevTools
{
    namespace
    {
        bool uiCatalogOpen = false;
    }

    void Install(NextEngine& engine)
    {
        engine.SetDebugDraw(std::make_shared<FAuxDrawSystem>());
        engine.SetDebugUiProvider(&DefaultDebugUiProvider());
        FUiDevPanels::Get().RegisterCVars(engine.GetCVarSystem());
        NextCVar::FCVarInfo existing;
        if (!engine.GetCVarSystem().TryGetInfo("ui.catalog", existing))
        {
            engine.GetCVarSystem().RegisterBool(
                "ui.catalog", false, &uiCatalogOpen, NextCVar::ECVarFlags::None,
                "Open the developer UI foundation catalog");
        }
    }

    namespace
    {
        class FDebugUiProvider final : public Runtime::IDebugUiProvider
        {
        public:
            FDebugUiProvider()
            {
                Runtime::Editor::AttachConsoleLogSinkToDefaultLogger();
            }

            void DrawUiPanels(NextEngine& engine,
                              const Runtime::FUiStatistics& statistics,
                              const Runtime::EUiDeveloperLayer layers,
                              const bool suppressStatsOverlay) override
            {
                FUiDevPanels& panels = FUiDevPanels::Get();
                if (Runtime::HasUiLayer(layers, Runtime::EUiDeveloperLayer::Statistics) && !suppressStatsOverlay)
                {
                    panels.DrawOverlay(statistics);
                }
                if (Runtime::HasUiLayer(layers, Runtime::EUiDeveloperLayer::Console))
                {
                    panels.RenderConsoleOverlay();
                }
                if (Runtime::HasUiLayer(layers, Runtime::EUiDeveloperLayer::Memory))
                {
                    panels.DrawMemoryStatisticsPanel(engine);
                }
                DrawUiCatalog(uiCatalogOpen);
            }

            bool HandleUiEvent(const SDL_Event& event) override
            {
                return FUiDevPanels::Get().HandleEvent(event);
            }

            void DrawPhysicsOverlay(const Assets::Scene& scene, const Assets::Camera& camera) override
            {
                Runtime::DrawPhysicsDebugOverlay(scene, camera);
            }

            void DrawGraphicsPanel(NextEngine& engine, bool& panelVisible, float topOffset) override
            {
                Runtime::GraphicsDebugPanel::DrawPanel(engine, panelVisible, topOffset);
            }

            void DrawCVarEditor(NextEngine& engine, bool& panelVisible) override
            {
                DrawCVarEditorPanel(engine, panelVisible);
            }

            bool HandleRendererShortcut(SDL_Keycode key, bool pressed, bool panelVisible, NextEngine& engine) override
            {
                return Runtime::GraphicsDebugPanel::TryHandleRendererShortcut(key, pressed, panelVisible, engine);
            }

            bool HandleViewModeShortcut(SDL_Keycode key,
                                        bool pressed,
                                        bool panelVisible,
                                        Runtime::Config::ShowFlags& showFlags) override
            {
                return Runtime::GraphicsDebugPanel::TryHandleViewModeShortcut(key, pressed, panelVisible, showFlags);
            }
        };
    }

    Runtime::IDebugUiProvider& DefaultDebugUiProvider()
    {
        static FDebugUiProvider provider;
        static bool auxDrawRegistered = false;
        if (!auxDrawRegistered)
        {
            Vulkan::RegisterExternalPassFactory(
                /*priority*/ 20,
                [](Vulkan::VulkanBaseRenderer& renderer) -> std::unique_ptr<Vulkan::IExternalRenderPass>
                { return std::make_unique<Vulkan::AuxDraw::AuxDrawPass>(renderer); });
            auxDrawRegistered = true;
        }
        return provider;
    }
}
