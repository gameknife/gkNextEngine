#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextRmlUi/NextRmlUiModule.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/UiHost.hpp"
#include "Modules/NextRmlUi/RmlUiSystem.hpp"

namespace Modules::NextRmlUi
{
    void Install(NextEngine& engine)
    {
        engine.RegisterUiLayerFactory([](NextEngine& owner) -> std::unique_ptr<Runtime::IUiLayer>
        {
            return std::make_unique<NextUI::RmlUiSystem>(owner);
        });
    }

    NextUI::RmlUiSystem* Get(NextEngine& engine)
    {
        if (Runtime::IUiHost* uiHost = engine.GetUiHost())
        {
            return static_cast<NextUI::RmlUiSystem*>(uiHost->FindLayer(NextUI::RmlUiSystem::LayerId));
        }
        return nullptr;
    }
}
