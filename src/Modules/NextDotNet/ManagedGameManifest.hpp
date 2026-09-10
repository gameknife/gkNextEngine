#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <optional>

namespace Modules::NextDotNet
{
    /// Everything that used to differ between one C# game's native shell and another's, expressed
    /// as data. See docs/designs/managed-game-launcher-design.md section 3.
    ///
    /// One manifest is the single declaration of a managed game: the per-game executable reads it
    /// to configure its window and load its assembly, and gkNextLauncher reads the same file to
    /// populate its menu. Two sources of truth here would mean a game that behaves differently
    /// depending on how it was started.
    ///
    /// A manifest is also the root of a game *project*: it sits in projects/<Game>/ next to the
    /// game's Content/ and its C# Scripts/, and the directory it was read from is what every
    /// project-relative path in it is resolved against.
    struct FManagedGameManifest
    {
        struct FWindow
        {
            std::string title;
            int width = 1280;
            int height = 720;
            bool forceSDR = true;
        };

        /// Only the flags a manifest actually mentions are applied; the rest keep engine defaults.
        /// A plain bool would silently force every unmentioned flag to false.
        struct FShowFlagOverrides
        {
            std::optional<bool> debugGraphicsPanel;
            std::optional<bool> debugPhysicsOverlay;
            std::optional<bool> overlay;
        };

        /// Stable identifier, also the menu sort key. Defaults to the file stem.
        std::string id;
        std::string displayName;
        /// Window icon. An engine asset path, or a path under Content/ (resolved on load).
        std::string icon;

        /// Managed assembly, relative to <bin>/csharp — the same path shape FConfig::gameAssembly
        /// takes, and the same subdirectory gk_dotnet_managed_game(... DIR ...) publishes into.
        std::string assembly;

        /// C# project file, relative to the project directory in the source tree
        /// ("Scripts/FlappyCSharp.csproj"). Optional, and only meaningful where that tree exists:
        /// it lets a host rebuild the game without leaving it, which is the whole point of running
        /// managed code under CoreCLR. An installed build simply has no project to point at.
        std::string project;

        FWindow window;

        /// Native modules the game needs at runtime. Purely declarative: modules are static
        /// libraries chosen at link time, so a host cannot acquire a missing one. The launcher
        /// checks this against what it was built with and refuses the game up front rather than
        /// letting it fail halfway into a scene it cannot load.
        std::vector<std::string> requiredModules;

        /// Scene to request once the game is initialised. Empty means the game loads its own from
        /// managed code (what Brotato3D does through Engine.RequestLoadScene). A path under
        /// Content/ is resolved to the project's content on load.
        std::string initialScene;

        FShowFlagOverrides showFlags;

        bool hotReload = true;
        bool compileManagedSources = false;

        /// The project directory: where this manifest was read from, runtime-relative
        /// ("assets/projects/Flappy") or absolute. Derived, never written to the file.
        std::string directory;

        /// Where this manifest was read from. Only used in diagnostics.
        std::string sourcePath;

        /// Runtime path of the project's Content/ directory — the prefix managed code gets through
        /// GameContent.Path(). Empty for a manifest that was not read from a file.
        std::string ContentRoot() const;
    };

    /// Game projects, relative to the runtime root. CMake copies each projects/<Game>/ manifest
    /// and Content/ here, so at runtime a project is one more subtree of the asset namespace and
    /// survives being paked or packaged for a device like any other asset.
    inline constexpr const char* kManagedGameProjectsDirectory = "assets/projects";

    /// A project's runtime content, beside its manifest. The only project subdirectory that
    /// reaches the runtime tree.
    inline constexpr const char* kManagedGameContentDirectory = "Content";

    /// A project's C# sources, beside its manifest. Never copied to the runtime tree: the
    /// published assembly under <bin>/csharp is what runs.
    inline constexpr const char* kManagedGameScriptsDirectory = "Scripts";

    /// Reads one manifest. Returns nullopt and logs the reason on a missing file, malformed JSON,
    /// or a missing required field.
    std::optional<FManagedGameManifest> LoadManagedGameManifest(const std::string& path);

    /// Every <root>/<Game>/*.game.json, loose files and mounted pak entries alike, sorted by id.
    /// Unreadable manifests are logged and skipped: one bad file must not hide the rest.
    std::vector<FManagedGameManifest> ScanManagedGameManifests(const std::string& root = kManagedGameProjectsDirectory);

    /// The source tree's projects/ directory, or empty in a build that shipped without one — the
    /// normal state of an installed build. Anything that writes into a project or rebuilds one has
    /// to check this first. GK_GAME_PROJECTS_SOURCES overrides it (tests point it at a scratch
    /// directory so they never write into the repository).
    std::filesystem::path GameProjectsSourceRoot();

    /// The project's directory in the source tree, where its Scripts/ and editable Content/ are,
    /// or empty when there is none. A manifest is normally read from its runtime copy, so this maps
    /// assets/projects/<Game> back to projects/<Game>; a manifest read straight from a source
    /// directory maps to that directory.
    std::filesystem::path ResolveProjectSourceDirectory(const FManagedGameManifest& manifest);

    /// Copies a project's manifests and Content/ from the source tree into its runtime directory —
    /// what CMake's Assets target does for every project, done for one without a C++ build. Runs
    /// after a rebuild and after creating a project, so an edited config or a brand-new game is
    /// visible to the running host immediately. A no-op (returning true) when the manifest was
    /// read from the source tree itself.
    bool SyncProjectContentToRuntime(const FManagedGameManifest& manifest, std::string& outError);
}
