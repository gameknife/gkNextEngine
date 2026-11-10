#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#include "Modules/ScadLoader/FScadLoader.h"
#include "Modules/ScadLoader/FScadShared.h"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"

#include <chrono>

namespace Modules::Scad
{
    void Register()
    {
        Assets::FLoaderRegistry::Get().RegisterSceneLoader(
            {".scad"},
            [](const std::string& filename, Assets::EnvironmentSetting& camera,
               std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
               std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
               std::vector<Assets::AnimationTrack>& tracks, std::vector<Assets::Skeleton>& skeletons)
            {
                Assets::ScadLoadOptions options;
                if (NextEngine* engine = NextEngine::GetInstance())
                {
                    options.scadToWorldScale = engine->GetUserSettings().ScadToWorldScale;
                }
                return Assets::FScadLoader::LoadScadScene(
                    filename, camera, nodes, models, materials, lights, tracks, skeletons, options);
            });

        // Parse-only: the whole use/include closure is read and parsed, nothing is evaluated, so
        // a host can refuse a broken file before the scene it is editing gets discarded.
        Assets::FLoaderRegistry::Get().RegisterSceneSourceCheck(
            {".scad"},
            [](const std::string& filename, Assets::FSceneSourceReport& outReport)
            {
                const auto started = std::chrono::steady_clock::now();
                outReport = {};
                Assets::Scad::ScadProgram program;
                outReport.ok =
                    Assets::Scad::LoadScadProgram(filename, program, outReport.error, &outReport.dependencies);
                outReport.milliseconds =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
                return outReport.ok;
            });
    }
}
