#pragma once

class NextEngine;

namespace NextUI
{
    class FUiSystem;
}

namespace Modules::NextUI
{
    void Install(NextEngine& engine);
    ::NextUI::FUiSystem* Get(NextEngine& engine);
}
