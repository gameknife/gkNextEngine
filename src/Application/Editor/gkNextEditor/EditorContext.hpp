#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Core/EditorSettings.hpp"

class NextEngine;
class EditorActionDispatcher;
class EditorGameInstance;

namespace Runtime
{
    class IUiHost;
}

namespace NextUI
{
    class GizmoController;
}

namespace Assets
{
    class Scene;
}

struct EditorContext final
{
    NextEngine& engine;
    Assets::Scene& scene;
    Runtime::IUiHost& ui;
    EditorActionDispatcher& actions;
    Editor::EditorSettings& settings;
    NextUI::GizmoController* gizmoController = nullptr;
    EditorGameInstance* editor = nullptr;
};
