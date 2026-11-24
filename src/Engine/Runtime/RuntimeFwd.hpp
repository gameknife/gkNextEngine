#pragma once

class NextAudio;
class NextEngine;
class NextGameInstanceBase;
class NextLocalization;
class NextPhysics;
class NextRig;

namespace NextCVar
{
    class FCVarSystem;
}

namespace Runtime
{
    class IDebugUiProvider;
    class IRenderFrameConsumer;
    class IScreenShotService;
    class IScriptRuntime;
    class IUiHost;
    class IUiLayer;
    struct FUiFrameContext;
    struct FUiStatistics;
}

namespace Runtime::Config
{
    class Options;
    struct ShowFlags;
    struct UserSettings;
}
