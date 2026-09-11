#include "Modules/NextDotNet/ManagedGameHostInstance.hpp"

#if GK_MANAGED_GAME_REGISTER_SCAD_LOADER
#include "Modules/ScadLoader/ScadModule.hpp"
#endif

#if GK_MANAGED_GAME_REGISTER_LDRAW_LOADER
#include "Modules/LDrawLoader/LDrawModule.hpp"
#endif

#if GK_MANAGED_GAME_REGISTER_GLTF_LOADER
#include "Modules/GltfLoader/GltfModule.hpp"
#endif

#ifndef GK_MANAGED_GAME_ID
#error "Managed game application requires GK_MANAGED_GAME_ID"
#endif

#ifndef GK_MANAGED_GAME_LINKED_MODULES
#error "Managed game application requires GK_MANAGED_GAME_LINKED_MODULES"
#endif

namespace
{
    std::vector<std::string> ParseLinkedModules()
    {
        std::vector<std::string> modules;
        const std::string_view encodedModules = GK_MANAGED_GAME_LINKED_MODULES;
        size_t begin = 0;
        while (begin < encodedModules.size())
        {
            const size_t end = encodedModules.find('|', begin);
            const std::string_view module = encodedModules.substr(begin, end - begin);
            if (!module.empty())
            {
                modules.emplace_back(module);
            }
            if (end == std::string_view::npos)
            {
                break;
            }
            begin = end + 1;
        }
        return modules;
    }

    void RegisterConfiguredLoaders()
    {
#if GK_MANAGED_GAME_REGISTER_SCAD_LOADER
        Modules::Scad::Register();
#endif
#if GK_MANAGED_GAME_REGISTER_LDRAW_LOADER
        Modules::LDraw::Register();
#endif
#if GK_MANAGED_GAME_REGISTER_GLTF_LOADER
        Modules::Gltf::Register();
#endif
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                        Runtime::Config::Options& options,
                                                        NextEngine* engine)
{
    RegisterConfiguredLoaders();

    return std::make_unique<Modules::NextDotNet::ManagedGameHostInstance>(
        config, options, engine,
        Modules::NextDotNet::FManagedGameHostOptions{
            .gameId = GK_MANAGED_GAME_ID,
            .linkedModules = ParseLinkedModules(),
        });
}
