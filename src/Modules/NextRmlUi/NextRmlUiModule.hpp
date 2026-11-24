#pragma once

class NextEngine;

namespace NextUI
{
    class RmlUiSystem;
}

namespace Modules::NextRmlUi
{
    // Registers the RmlUi document layer factory on the engine's IUiHost.
    // Call from the application entry before NextEngine::Start.
    void Install(NextEngine& engine);

    // Typed accessor for applications scripting RmlUi documents directly.
    // Returns nullptr until the engine's UI host has created the layer.
    NextUI::RmlUiSystem* Get(NextEngine& engine);
}
