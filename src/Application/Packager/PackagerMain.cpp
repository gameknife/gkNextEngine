#include <iostream>
//#include <boost/program_options.hpp>
#include <cxxopts.hpp>
#include "Utilities/FileHelper.hpp"
#include "Runtime/Engine.hpp"

//using namespace boost::program_options;

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<NextGameInstanceVoid>(config, options, engine);
}

int main(int argc, const char* argv[]) noexcept
{
    // Runtime Main Routine
    try
    {
        std::string pakPath;
        std::string srcPath;
        std::string rootPath;
        std::string regex;
                
        const int lineLength = 120;
        cxxopts::Options options("options", "");
        options.add_options()
            ("out", "abs path", cxxopts::value<std::string>(pakPath)->default_value("out.pak"))
            ("src", "based project root path, like assets/textures", cxxopts::value<std::string>(srcPath)->default_value("assets"))
            ("regex", "if not empty, only pak files match the regex will be packed.", cxxopts::value<std::string>(regex)->default_value(""))
            
            ("h,help", "Print usage");

        auto result = options.parse(argc, argv);
        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            exit(0);
        }

        Utilities::Package::FPackageFileSystem packageSystem(Utilities::Package::EPM_OsFile);
        packageSystem.PakAll(pakPath, srcPath, "", regex);

        return EXIT_SUCCESS;
    }
    catch (...)
    {
        return EXIT_FAILURE;
    }
    return EXIT_FAILURE;
}
