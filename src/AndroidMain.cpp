#include "Vulkan/Enumerate.hpp"
#include "Vulkan/Strings.hpp"
#include "Utilities/Console.hpp"
#include "Utilities/Exception.hpp"
#include "Options.hpp"
#include "Runtime/Application.hpp"
#include <algorithm>
#include <cstdlib>
#include <fmt/format.h>
#include <filesystem>

#include "Runtime/Platform/PlatformCommon.h"

#include <imgui_impl_android.h>
#include <android/log.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

std::unique_ptr<NextRendererApplication> GApplication = nullptr;
const Options* GOption = nullptr;

void handle_cmd(android_app* app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        // The window is being shown, get it ready.
        {
            MakeExternalDirectory(app, "assets/fonts");
            MakeExternalDirectory(app, "assets/models");
            MakeExternalDirectory(app, "assets/shaders");
            MakeExternalDirectory(app, "assets/textures");
            MakeExternalDirectory(app, "assets/locale");
            
            const char* argv[] = { "gkNextRenderer", "--renderer=1" };
            GOption = new Options(2, argv);
            GApplication.reset(new NextRendererApplication(*GOption, app->window));
            GApplication->Start();
        }
        break;
    case APP_CMD_TERM_WINDOW:
        // The window is being hidden or closed, clean it up.
        {
            delete GOption;
        }
        break;
    }
}

static int32_t engine_handle_input(struct android_app* app) {
    ImGuiIO& io = ImGui::GetIO();
    //auto* engine = (struct engine*)app->userData;
    auto ib = android_app_swap_input_buffers(app);
    if (ib && ib->motionEventsCount) {
        for (int i = 0; i < ib->motionEventsCount; i++) {
            auto *event = &ib->motionEvents[i];
            int32_t ptrIdx = 0;
            switch (event->action & AMOTION_EVENT_ACTION_MASK) {
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_UP:
                // Retrieve the index for the starting and the ending of any secondary pointers
                ptrIdx = (event->action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                         AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_UP:
                io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
                io.AddMousePosEvent(GameActivityPointerAxes_getAxisValue(
                &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X) * 0.5, GameActivityPointerAxes_getAxisValue(
            &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y) * 0.5);
                io.AddMouseButtonEvent(0, event->action == AMOTION_EVENT_ACTION_DOWN);

                GApplication->OnTouch(event->action == AMOTION_EVENT_ACTION_DOWN, GameActivityPointerAxes_getAxisValue(
                        &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y) * 0.5,
                        GameActivityPointerAxes_getAxisValue(
                    &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X) * 0.5);

                break;
            case AMOTION_EVENT_ACTION_MOVE:
                // Process the move action: the new coordinates for all active touch pointers
                // are inside the event->pointers[]. Compare with our internally saved
                // coordinates to find out which pointers are actually moved. Note that there is
                // no index embedded inside event->action for AMOTION_EVENT_ACTION_MOVE (there
                // might be multiple pointers moved at the same time).
                //...
                    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
                    io.AddMousePosEvent(GameActivityPointerAxes_getAxisValue(
                        &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X) * 0.5,
                        GameActivityPointerAxes_getAxisValue(
                    &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y) * 0.5);

                GApplication->OnTouchMove(GameActivityPointerAxes_getAxisValue(
                        &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y) * 0.5,
                        GameActivityPointerAxes_getAxisValue(
                    &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X) * 0.5);
               break;
            }
        }
        android_app_clear_motion_events(ib);
    }

    // Process the KeyEvent in a similar way.
    //...

return 0;
}

void android_main(struct android_app* app)
{
    //std::cout.rdbuf(new androidbuf);
    
    app->onAppCmd = handle_cmd;

    // Used to poll the events in the main loop
    int events;
    android_poll_source* source;

    // Main loop
    do {
        if (ALooper_pollAll(GApplication != nullptr ? 1 : 0, nullptr,
                            &events, (void**)&source) >= 0) {
            if (source != NULL) source->process(app, source);
                            }
        
        engine_handle_input(app);
        
        // render if vulkan is ready
        if (GApplication != nullptr) {
            GApplication->Tick();
        }
    } while (app->destroyRequested == 0);

    //delete std::cout.rdbuf(0);
}
