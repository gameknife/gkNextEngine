#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/NextUIModule.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextUI/UiSystem.hpp"

namespace Modules::NextUI
{
    void Install(NextEngine& engine)
    {
        engine.SetUiHostFactory(
            [](NextEngine& owner, Runtime::FUiHostConfig config) -> std::unique_ptr<Runtime::IUiHost>
            {
                return std::make_unique<::NextUI::FUiSystem>(owner, std::move(config));
            });
    }

    ::NextUI::FUiSystem* Get(NextEngine& engine)
    {
        return static_cast<::NextUI::FUiSystem*>(engine.GetUiHost());
    }
}
