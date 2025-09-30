#pragma once

#include "EditorGUI.h"
#include "Runtime/Engine.hpp"
#include "Runtime/ModelViewController.hpp"

class EditorInterface;

namespace Assets
{
    class Scene;    
}

class EditorGameInstance : public NextGameInstanceBase
{
public:
    EditorGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);
    ~EditorGameInstance() override = default;

    // overrides
    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override {};

    void OnSceneLoaded() override;

    void OnPreConfigUI() override;
    bool OnRenderUI() override;
    void OnInitUI() override;

    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;

    bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const override;
    
    // quick access engine
    NextEngine& GetEngine() { return *engine_; }

private:
    NextEngine* engine_;

    std::unique_ptr<EditorInterface> editorUserInterface_;
    ModelViewController modelViewController_;
};

inline bool EditorGameInstance::OverrideRenderCamera(Assets::Camera& OutRenderCamera) const
{
    OutRenderCamera.ModelView = modelViewController_.ModelView();
    OutRenderCamera.FieldOfView = modelViewController_.FieldOfView();
    return true;
}
