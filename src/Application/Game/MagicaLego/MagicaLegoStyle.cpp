#include "Engine/Common/CoreMinimal.hpp"
#include "MagicaLegoStyle.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include <imgui.h>

namespace MagicaLego::Style
{
    void ApplyStyle()
    {
        NextUI::Theme::ApplyProfessionalTheme();
    }
}
