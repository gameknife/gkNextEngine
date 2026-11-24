#include "Engine/Common/CoreMinimal.hpp"
#include "MagicaLegoStyle.hpp"
#include "Modules/NextUI/NextUI.hpp"
#include <imgui.h>

namespace MagicaLego::Style
{
    void ApplyStyle()
    {
        NextUI::ApplyTheme();
    }
}
