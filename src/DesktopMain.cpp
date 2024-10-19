#include "Utilities/Console.hpp"
#include "Utilities/Exception.hpp"
#include "Options.hpp"
#include "Runtime/Application.hpp"

#include <fmt/format.h>
#include <iostream>
#include <filesystem>

#include "Runtime/Platform/PlatformCommon.h"

const Options* GOption = nullptr;

int main(int argc, const char* argv[]) noexcept
{
    // Runtime Main Routine
    try
    {
        // Handle command line options.
        Options options(argc, argv);
        // Global GOption, can access from everywhere
        GOption = &options;
        
        // Init environment variables
#if __APPLE__
        setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "1", 1);
#endif   
        if(options.RenderDoc)
        {
#if __linux__
            setenv("ENABLE_VULKAN_RENDERDOC_CAPTURE", "1", 1);
#endif

#if __APPLE__
            setenv("MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE", "~/capture/cap.gputrace", 1);
            setenv("MVK_CONFIG_DEFAULT_GPU_CAPTURE_SCOPE_QUEUE_FAMILY_INDEX", "0", 1);
            setenv("MVK_CONFIG_DEFAULT_GPU_CAPTURE_SCOPE_QUEUE_INDEX", "0", 1);
            setenv("MTL_CAPTURE_ENABLED", "1", 1);
            setenv("MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE","2",1);
#endif
        }

        NextRenderer::PlatformInit();
        
        // Start the application.
        std::unique_ptr<NextRendererApplication> GApplication;
        GApplication.reset( new NextRendererApplication(options) );

        // Application Main Loop
        GApplication->Start();
        while (1)
        {
            if( GApplication->Tick() )
            {
                break;
            }
        }
        GApplication->End();
        
        // Shutdown
        GApplication.reset();

        return EXIT_SUCCESS;
    }
    // Exception Handle
    catch (const Options::Help&)
    {
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        Utilities::Console::Write(Utilities::Severity::Fatal, [&exception]()
        {
            const auto stacktrace = boost::get_error_info<traced>(exception);

            std::cerr << "FATAL: " << exception.what() << std::endl;

            if (stacktrace)
            {
                std::cerr << '\n' << *stacktrace << '\n';
            }
        });
    }
    catch (...)
    {
        Utilities::Console::Write(Utilities::Severity::Fatal, []()
        {
            fmt::print(stderr, "FATAL: caught unhandled exception\n");
        });
    }
    return EXIT_FAILURE;
}
