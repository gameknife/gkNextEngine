#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/AssetsFwd.hpp"

#include <initializer_list>
#include <mutex>
#include <string_view>

namespace Assets
{
    // Scene loader entry point for one file format. Signature mirrors
    // Runtime::Scene::SceneList::LoadScene so registered loaders can plug
    // straight into the dispatch there.
    using FSceneLoaderFn = std::function<bool(
        const std::string& filename, EnvironmentSetting& camera,
        std::vector<std::shared_ptr<Node>>& nodes, std::vector<Model>& models,
        std::vector<FMaterial>& materials, std::vector<LightObject>& lights,
        std::vector<AnimationTrack>& tracks, std::vector<Skeleton>& skeletons)>;

    // Procedural scene builder (a ".proc" entry in the scene list).
    using FProcSceneFn = std::function<void(
        EnvironmentSetting& camera,
        std::vector<std::shared_ptr<Node>>& nodes, std::vector<Model>& models,
        std::vector<FMaterial>& materials, std::vector<LightObject>& lights,
        std::vector<AnimationTrack>& tracks)>;

    // What a loader found out about a scene source file, for hosts that edit
    // scenes while they run (a game whose level is a file an agent rewrites).
    // A source check fills ok/error/dependencies without building anything; a
    // load additionally fills the counts and warnings.
    struct FSceneSourceReport
    {
        bool ok = false;
        // First read / lex / parse failure, empty when ok.
        std::string error;
        // Non-fatal evaluation findings ("unknown module 'foo'"), capped by the loader.
        std::vector<std::string> warnings;
        int warningCount = 0;
        // Every file the scene is built from, as absolute paths, main file first. A
        // host watches these to know when to rebuild.
        std::vector<std::string> dependencies;
        size_t nodeCount = 0;
        size_t triangleCount = 0;
        double milliseconds = 0.0;
        // Stamped by PublishLoadReport, increasing across every file: a host that asked for a
        // load compares it with the serial it saw before to tell "this load finished" -- even one
        // that failed and therefore never reached the game's scene-loaded hook.
        uint64_t serial = 0;
    };

    // Cheap pre-flight of a scene source: reads and parses the file and everything
    // it pulls in, but evaluates nothing. Exists because the engine discards the
    // current scene before the replacement is parsed -- a live editor that loads a
    // file with a syntax error would otherwise blank the world it is editing.
    using FSceneSourceCheckFn = std::function<bool(const std::string& filename, FSceneSourceReport& outReport)>;

    // Registry decoupling the engine core from optional scene loaders and
    // application demo scenes. Format modules and applications register
    // their formats / procedural scenes explicitly at startup.
    class FLoaderRegistry final
    {
    public:
        static FLoaderRegistry& Get();

        // extensions: lowercase, dot-prefixed (".ldr"). Registration order
        // defines the grouping order in the scene list.
        void RegisterSceneLoader(std::initializer_list<std::string_view> extensions, FSceneLoaderFn loadFn);
        void RegisterProcScene(std::string_view name, FProcSceneFn buildFn);

        bool SupportsExtension(std::string_view extension) const;
        // Sort key for scene list grouping: 0 for the first registered loader,
        // 1 for the second, ...; -1 when the extension is unknown.
        int GetExtensionOrder(std::string_view extension) const;
        std::vector<std::string> RegisteredExtensions() const;
        std::vector<std::string> ProcSceneNames() const;

        const FSceneLoaderFn* FindSceneLoader(std::string_view extension) const;
        const FProcSceneFn* FindProcScene(std::string_view name) const;

        // Optional per-format source checks; formats without one report nothing.
        void RegisterSceneSourceCheck(std::initializer_list<std::string_view> extensions, FSceneSourceCheckFn checkFn);
        const FSceneSourceCheckFn* FindSceneSourceCheck(std::string_view extension) const;

        // A loader that knows more than "succeeded / failed" publishes it here, keyed
        // by the filename it was asked to load. Loads run on worker threads, so both
        // calls lock; only the most recent few files are kept.
        void PublishLoadReport(const std::string& filename, FSceneSourceReport report);
        bool FindLoadReport(const std::string& filename, FSceneSourceReport& outReport) const;

    private:
        struct FLoaderEntry
        {
            std::vector<std::string> extensions;
            FSceneLoaderFn load;
        };

        struct FProcEntry
        {
            std::string name;
            FProcSceneFn build;
        };

        struct FSourceCheckEntry
        {
            std::vector<std::string> extensions;
            FSceneSourceCheckFn check;
        };

        std::vector<FLoaderEntry> loaders_;
        std::vector<FProcEntry> procScenes_;
        std::vector<FSourceCheckEntry> sourceChecks_;

        mutable std::mutex loadReportsMutex_;
        std::vector<std::pair<std::string, FSceneSourceReport>> loadReports_;
        uint64_t nextLoadReportSerial_ = 1;
    };
}
