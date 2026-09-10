#include "Modules/NextDotNet/ManagedGameManifest.hpp"

#include "Engine/Utilities/FileHelper.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>

namespace Modules::NextDotNet
{
    namespace
    {
        using json = nlohmann::json;

        constexpr std::string_view kManifestExtension = ".game.json";

        bool IsManifestFilename(std::string_view filename)
        {
            return filename.size() > kManifestExtension.size() &&
                   filename.compare(filename.size() - kManifestExtension.size(), kManifestExtension.size(),
                                    kManifestExtension) == 0;
        }

        /// Reads an asset-relative or absolute path through the pak system when one is mounted,
        /// falling back to a loose file. Mirrors CVarSystem's config reader: manifests are shipped
        /// content and must survive being paked.
        bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& outData)
        {
            if (auto* package = Utilities::Package::FPackageFileSystem::TryGetInstance())
            {
                if (package->LoadFile(path, outData))
                {
                    return true;
                }
            }

            const std::filesystem::path loosePath = Utilities::FileHelper::GetRuntimeFilePath(path);
            std::ifstream file(loosePath, std::ios::binary);
            if (!file.is_open())
            {
                return false;
            }
            outData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            return true;
        }

        void ReadOptionalBool(const json& source, const char* key, std::optional<bool>& target)
        {
            if (const auto it = source.find(key); it != source.end() && it->is_boolean())
            {
                target = it->get<bool>();
            }
        }

        EMobileControls ReadMobileControls(const json& source)
        {
            const auto it = source.find("mobileControls");
            if (it == source.end())
            {
                return EMobileControls::None;
            }
            if (!it->is_string())
            {
                SPDLOG_WARN("[game] manifest 'mobileControls' must be a string; disabling touch controls");
                return EMobileControls::None;
            }

            const std::string value = it->get<std::string>();
            if (value == "dualStick")
            {
                return EMobileControls::DualStick;
            }
            if (value == "none")
            {
                return EMobileControls::None;
            }

            SPDLOG_WARN("[game] unknown mobileControls '{}'; disabling touch controls", value);
            return EMobileControls::None;
        }

        std::string StemOf(const std::string& path)
        {
            const std::string filename = std::filesystem::path(path).filename().string();
            if (IsManifestFilename(filename))
            {
                return filename.substr(0, filename.size() - kManifestExtension.size());
            }
            return std::filesystem::path(filename).stem().string();
        }

        /// A manifest path that starts with Content/ names the project's own content, and becomes a
        /// runtime asset path under the project directory. Anything else — an engine asset such as
        /// assets/icons/x.png, or a built-in scene name such as Empty.proc — passes through as is.
        std::string ResolveContentPath(const FManagedGameManifest& manifest, const std::string& path)
        {
            const std::string prefix = std::string(kManagedGameContentDirectory) + "/";
            if (manifest.directory.empty() || path.rfind(prefix, 0) != 0)
            {
                return path;
            }
            return manifest.directory + "/" + path;
        }
    }

    std::string FManagedGameManifest::ContentRoot() const
    {
        return directory.empty() ? std::string() : directory + "/" + kManagedGameContentDirectory;
    }

    std::optional<FManagedGameManifest> LoadManagedGameManifest(const std::string& path)
    {
        std::vector<uint8_t> data;
        if (!ReadFileBytes(path, data))
        {
            SPDLOG_ERROR("[game] manifest not found: {}", path);
            return std::nullopt;
        }

        FManagedGameManifest manifest;
        manifest.sourcePath = path;
        manifest.id = StemOf(path);
        manifest.directory = Utilities::FileHelper::NormalizePathString(std::filesystem::path(path).parent_path());

        try
        {
            const json root = json::parse(data.begin(), data.end());

            if (const auto it = root.find("id"); it != root.end() && it->is_string())
            {
                manifest.id = it->get<std::string>();
            }
            manifest.displayName = root.value("displayName", manifest.id);
            manifest.icon = ResolveContentPath(manifest, root.value("icon", std::string()));
            manifest.assembly = root.value("assembly", std::string());
            manifest.project = root.value("project", std::string());
            manifest.initialScene = ResolveContentPath(manifest, root.value("initialScene", std::string()));
            manifest.mobileControls = ReadMobileControls(root);
            manifest.hotReload = root.value("hotReload", true);
            manifest.compileManagedSources = root.value("compileManagedSources", false);

            if (const auto window = root.find("window"); window != root.end() && window->is_object())
            {
                manifest.window.title = window->value("title", manifest.displayName);
                manifest.window.width = window->value("width", manifest.window.width);
                manifest.window.height = window->value("height", manifest.window.height);
                manifest.window.forceSDR = window->value("forceSDR", manifest.window.forceSDR);
            }
            if (manifest.window.title.empty())
            {
                manifest.window.title = manifest.displayName;
            }

            if (const auto modules = root.find("requiredModules"); modules != root.end() && modules->is_array())
            {
                for (const auto& entry : *modules)
                {
                    if (entry.is_string())
                    {
                        manifest.requiredModules.push_back(entry.get<std::string>());
                    }
                }
            }

            if (const auto flags = root.find("showFlags"); flags != root.end() && flags->is_object())
            {
                ReadOptionalBool(*flags, "debugGraphicsPanel", manifest.showFlags.debugGraphicsPanel);
                ReadOptionalBool(*flags, "debugPhysicsOverlay", manifest.showFlags.debugPhysicsOverlay);
                ReadOptionalBool(*flags, "overlay", manifest.showFlags.overlay);
            }
        }
        catch (const std::exception& error)
        {
            SPDLOG_ERROR("[game] manifest {} is not valid JSON: {}", path, error.what());
            return std::nullopt;
        }

        // The assembly is the one field with no sensible default: without it there is no game.
        if (manifest.assembly.empty())
        {
            SPDLOG_ERROR("[game] manifest {} has no 'assembly' field", path);
            return std::nullopt;
        }

        return manifest;
    }

    std::vector<FManagedGameManifest> ScanManagedGameManifests(const std::string& root)
    {
        // A paked build and a loose tree can both be present; collect names first so a manifest
        // shipped in a pak and also sitting on disk is only loaded once.
        std::set<std::string> manifestPaths;

        // One level of projects, and manifests only directly inside each: a file that happens to
        // be named *.game.json somewhere in a project's Content/ is content, not another game.
        std::error_code ec;
        const std::filesystem::path looseRoot = Utilities::FileHelper::GetRuntimeFilePath(root);
        for (const auto& projectEntry : std::filesystem::directory_iterator(looseRoot, ec))
        {
            if (!projectEntry.is_directory(ec))
            {
                continue;
            }
            const std::string projectName = projectEntry.path().filename().string();
            std::error_code fileError;
            for (const auto& fileEntry : std::filesystem::directory_iterator(projectEntry.path(), fileError))
            {
                const std::string filename = fileEntry.path().filename().string();
                if (fileEntry.is_regular_file(fileError) && IsManifestFilename(filename))
                {
                    manifestPaths.insert(root + "/" + projectName + "/" + filename);
                }
            }
        }

        if (auto* package = Utilities::Package::FPackageFileSystem::TryGetInstance())
        {
            const std::string prefix = Utilities::FileHelper::NormalizePathString(root) + "/";
            for (const std::string& entry : package->ListMountedEntries(prefix))
            {
                const std::string_view rest = std::string_view(entry).substr(prefix.size());
                const size_t slash = rest.find('/');
                if (slash == std::string_view::npos || rest.find('/', slash + 1) != std::string_view::npos)
                {
                    continue;
                }
                if (IsManifestFilename(rest.substr(slash + 1)))
                {
                    manifestPaths.insert(entry);
                }
            }
        }

        std::vector<FManagedGameManifest> manifests;
        manifests.reserve(manifestPaths.size());
        for (const std::string& path : manifestPaths)
        {
            if (auto manifest = LoadManagedGameManifest(path))
            {
                manifests.push_back(std::move(*manifest));
            }
        }

        std::sort(manifests.begin(), manifests.end(),
                  [](const FManagedGameManifest& lhs, const FManagedGameManifest& rhs)
                  {
                      return lhs.id < rhs.id;
                  });
        return manifests;
    }

    /// Not resolved through the asset path, for the same reason DotNetRuntime::ManagedSourceRoot()
    /// is not: the runtime tree holds a copy of each project's manifest and Content/ but never its
    /// Scripts/, so resolving projects/ against the runtime root finds nothing to rebuild. The
    /// baked source path is the only thing that knows where the real projects are.
    std::filesystem::path GameProjectsSourceRoot()
    {
        if (const char* fromEnv = std::getenv("GK_GAME_PROJECTS_SOURCES"); fromEnv != nullptr && *fromEnv != '\0')
        {
            return std::filesystem::path(fromEnv);
        }
#if defined(GK_GAME_PROJECTS_SOURCE_ROOT)
        std::error_code ec;
        const std::filesystem::path baked(GK_GAME_PROJECTS_SOURCE_ROOT);
        if (std::filesystem::exists(baked, ec))
        {
            return baked;
        }
#endif
        return {};
    }

    std::filesystem::path ResolveProjectSourceDirectory(const FManagedGameManifest& manifest)
    {
        if (manifest.directory.empty())
        {
            return {};
        }

        std::error_code ec;
        const std::filesystem::path directory(manifest.directory);

        // The usual case: read from assets/projects/<Game>, whose source is projects/<Game>.
        if (const std::filesystem::path sourceRoot = GameProjectsSourceRoot(); !sourceRoot.empty())
        {
            const std::filesystem::path candidate = sourceRoot / directory.filename();
            if (std::filesystem::is_directory(candidate, ec))
            {
                return candidate;
            }
        }

        // Read straight from a project directory somewhere else on disk: that directory is the
        // source. The runtime copy never qualifies, because it has no Scripts/ to build from.
        if (directory.is_absolute() && std::filesystem::is_directory(directory, ec) &&
            (manifest.project.empty() || std::filesystem::exists(directory / manifest.project, ec)))
        {
            return directory;
        }
        return {};
    }

    bool SyncProjectContentToRuntime(const FManagedGameManifest& manifest, std::string& outError)
    {
        const std::filesystem::path source = ResolveProjectSourceDirectory(manifest);
        if (source.empty())
        {
            outError = "'" + manifest.id + "' has no project directory in the source tree";
            return false;
        }

        const std::filesystem::path runtime =
            Utilities::FileHelper::GetRuntimeFilePath(kManagedGameProjectsDirectory) / source.filename();

        std::error_code ec;
        if (std::filesystem::exists(runtime, ec) && std::filesystem::equivalent(source, runtime, ec))
        {
            return true;
        }

        std::filesystem::create_directories(runtime, ec);
        if (ec)
        {
            outError = "could not create " + runtime.string() + ": " + ec.message();
            return false;
        }

        for (const auto& entry : std::filesystem::directory_iterator(source, ec))
        {
            if (entry.is_regular_file() && IsManifestFilename(entry.path().filename().string()))
            {
                std::error_code copyError;
                std::filesystem::copy_file(entry.path(), runtime / entry.path().filename(),
                                           std::filesystem::copy_options::overwrite_existing, copyError);
                if (copyError)
                {
                    outError = "could not copy " + entry.path().string() + ": " + copyError.message();
                    return false;
                }
            }
        }

        const std::filesystem::path content = source / kManagedGameContentDirectory;
        if (std::filesystem::is_directory(content, ec))
        {
            std::filesystem::copy(content, runtime / kManagedGameContentDirectory,
                                  std::filesystem::copy_options::recursive |
                                      std::filesystem::copy_options::overwrite_existing,
                                  ec);
            if (ec)
            {
                outError = "could not copy " + content.string() + ": " + ec.message();
                return false;
            }
        }
        return true;
    }
}
