#pragma once

// On-screen overlay for agent watch mode: the step being run, where the synthetic
// cursor is, which keys were pressed, and whether checks passed. Only active under
// --agent-watch, so ordinary validation runs and their screenshots are unaffected.

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Interface/UiHost.hpp"

namespace Modules::NextValidation
{
    struct FWatchState
    {
        struct FRipple
        {
            float x = 0.0f;
            float y = 0.0f;
            double time = 0.0;
            bool secondary = false;
        };

        // Written by gnb through the "annotate" control method.
        std::string caption;
        std::string detail;
        std::string status = "running"; // running | pass | fail | done | failed
        double statusTime = -1.0;
        bool hidden = false;

        // Derived from the input commands themselves. Positions are window coordinates.
        bool hasPointer = false;
        float pointerX = 0.0f;
        float pointerY = 0.0f;
        std::vector<FRipple> ripples;
        std::string keyLabel;
        double keyTime = -1.0;
        double lookTime = -1.0;
    };

    // Seconds on a monotonic clock shared by the service and the overlay.
    double WatchNow();

#if GK_VALIDATION_WATCH_OVERLAY
    std::unique_ptr<Runtime::IUiLayer> CreateWatchOverlayLayer(std::shared_ptr<FWatchState> state);
#endif
}
