#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"

#include <algorithm>
#include <cctype>

namespace Assets
{
    namespace
    {
        std::string ToLowerExtension(std::string_view extension)
        {
            std::string normalized(extension);
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return normalized;
        }
    }

    FLoaderRegistry& FLoaderRegistry::Get()
    {
        static FLoaderRegistry registry;
        return registry;
    }

    void FLoaderRegistry::RegisterSceneLoader(std::initializer_list<std::string_view> extensions, FSceneLoaderFn loadFn)
    {
        FLoaderEntry entry;
        for (std::string_view extension : extensions)
        {
            entry.extensions.push_back(ToLowerExtension(extension));
        }
        entry.load = std::move(loadFn);

        // Re-registration with the same extension set replaces the loader so
        // repeated app init (tests, scene reloads) stays idempotent.
        for (FLoaderEntry& existing : loaders_)
        {
            if (existing.extensions == entry.extensions)
            {
                existing.load = std::move(entry.load);
                return;
            }
        }
        loaders_.push_back(std::move(entry));
    }

    void FLoaderRegistry::RegisterProcScene(std::string_view name, FProcSceneFn buildFn)
    {
        for (FProcEntry& existing : procScenes_)
        {
            if (existing.name == name)
            {
                existing.build = std::move(buildFn);
                return;
            }
        }
        procScenes_.push_back({std::string(name), std::move(buildFn)});
    }

    bool FLoaderRegistry::SupportsExtension(std::string_view extension) const
    {
        return FindSceneLoader(extension) != nullptr;
    }

    int FLoaderRegistry::GetExtensionOrder(std::string_view extension) const
    {
        const std::string normalized = ToLowerExtension(extension);
        for (size_t i = 0; i < loaders_.size(); ++i)
        {
            const auto& extensions = loaders_[i].extensions;
            if (std::find(extensions.begin(), extensions.end(), normalized) != extensions.end())
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    std::vector<std::string> FLoaderRegistry::RegisteredExtensions() const
    {
        std::vector<std::string> result;
        for (const FLoaderEntry& entry : loaders_)
        {
            result.insert(result.end(), entry.extensions.begin(), entry.extensions.end());
        }
        return result;
    }

    std::vector<std::string> FLoaderRegistry::ProcSceneNames() const
    {
        std::vector<std::string> result;
        result.reserve(procScenes_.size());
        for (const FProcEntry& entry : procScenes_)
        {
            result.push_back(entry.name);
        }
        return result;
    }

    const FSceneLoaderFn* FLoaderRegistry::FindSceneLoader(std::string_view extension) const
    {
        const std::string normalized = ToLowerExtension(extension);
        for (const FLoaderEntry& entry : loaders_)
        {
            if (std::find(entry.extensions.begin(), entry.extensions.end(), normalized) != entry.extensions.end())
            {
                return &entry.load;
            }
        }
        return nullptr;
    }

    const FProcSceneFn* FLoaderRegistry::FindProcScene(std::string_view name) const
    {
        for (const FProcEntry& entry : procScenes_)
        {
            if (entry.name == name)
            {
                return &entry.build;
            }
        }
        return nullptr;
    }

    void FLoaderRegistry::RegisterSceneSourceCheck(std::initializer_list<std::string_view> extensions,
                                                   FSceneSourceCheckFn checkFn)
    {
        FSourceCheckEntry entry;
        for (std::string_view extension : extensions)
        {
            entry.extensions.push_back(ToLowerExtension(extension));
        }
        entry.check = std::move(checkFn);

        for (FSourceCheckEntry& existing : sourceChecks_)
        {
            if (existing.extensions == entry.extensions)
            {
                existing.check = std::move(entry.check);
                return;
            }
        }
        sourceChecks_.push_back(std::move(entry));
    }

    const FSceneSourceCheckFn* FLoaderRegistry::FindSceneSourceCheck(std::string_view extension) const
    {
        const std::string normalized = ToLowerExtension(extension);
        for (const FSourceCheckEntry& entry : sourceChecks_)
        {
            if (std::find(entry.extensions.begin(), entry.extensions.end(), normalized) != entry.extensions.end())
            {
                return &entry.check;
            }
        }
        return nullptr;
    }

    void FLoaderRegistry::PublishLoadReport(const std::string& filename, FSceneSourceReport report)
    {
        // Thumbnails and previews load scenes too; a handful of entries covers every host that
        // reads its own report back right after the load it asked for.
        constexpr size_t maxReports = 8;

        std::lock_guard lock(loadReportsMutex_);
        report.serial = nextLoadReportSerial_++;
        loadReports_.erase(std::remove_if(loadReports_.begin(), loadReports_.end(),
                                          [&filename](const auto& entry) { return entry.first == filename; }),
                           loadReports_.end());
        if (loadReports_.size() >= maxReports)
        {
            loadReports_.erase(loadReports_.begin());
        }
        loadReports_.emplace_back(filename, std::move(report));
    }

    bool FLoaderRegistry::FindLoadReport(const std::string& filename, FSceneSourceReport& outReport) const
    {
        std::lock_guard lock(loadReportsMutex_);
        for (const auto& [name, report] : loadReports_)
        {
            if (name == filename)
            {
                outReport = report;
                return true;
            }
        }
        return false;
    }
}
