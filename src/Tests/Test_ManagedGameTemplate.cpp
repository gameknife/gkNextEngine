#include "Modules/NextDotNet/ManagedGameTemplate.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>

// Covers the half of "new game project" that writes files, and the project layout every managed game
// shares (projects/<Game>/ with a manifest, Content/ and Scripts/). The dialog around it is ImGui and
// is checked by eye; what must not regress is that a bad name is refused *before* a directory
// appears, that a good one produces a project whose tokens are actually substituted, and that a
// manifest's project-relative paths resolve to where its content really is.

using namespace Modules::NextDotNet;

namespace
{
    /// Points GameProjectsSourceRoot() at a scratch tree, so a test that creates a project cannot
    /// write into the repository's own projects/. Empty removes the override.
    void SetProjectsSourceOverride(const std::string& value)
    {
#if defined(_WIN32)
        _putenv_s("GK_GAME_PROJECTS_SOURCES", value.c_str());
#else
        if (value.empty())
        {
            unsetenv("GK_GAME_PROJECTS_SOURCES");
        }
        else
        {
            setenv("GK_GAME_PROJECTS_SOURCES", value.c_str(), 1);
        }
#endif
    }

    std::string ReadWholeFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    void WriteWholeFile(const std::filesystem::path& path, const std::string& contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary | std::ios::trunc) << contents;
    }

    /// Paths built from different spellings of the same root (a backslashed temp path, a manifest's
    /// generic directory string) are compared as generic strings, not as path objects.
    std::string Generic(const std::filesystem::path& path)
    {
        return path.lexically_normal().generic_string();
    }

    /// Restores the projects override even when a REQUIRE bails out of the test early; a leaked
    /// override would silently point every later test at a deleted scratch tree.
    struct FProjectsSourceOverride
    {
        explicit FProjectsSourceOverride(const std::filesystem::path& path) { SetProjectsSourceOverride(path.string()); }
        ~FProjectsSourceOverride() { SetProjectsSourceOverride({}); }
    };

    /// A fresh directory under the system temp root, removed again when the test ends.
    struct FScratchDirectory
    {
        std::filesystem::path path;

        explicit FScratchDirectory(const char* name)
            : path(std::filesystem::temp_directory_path() / name)
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
            std::filesystem::create_directories(path, ec);
        }

        ~FScratchDirectory()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };
}

TEST_CASE("DeriveGameId turns a project name into a manifest id", "[Unit][DotNet][Template]")
{
    CHECK(DeriveGameId("MySpaceGame") == "myspacegame");
    CHECK(DeriveGameId("Space_Game 2") == "spacegame2");
    // An id that starts with a digit reads as a number wherever it is used, so it gets a prefix.
    CHECK(DeriveGameId("3DShooter") == "g3dshooter");
    CHECK(DeriveGameId("") == "");
}

TEST_CASE("the shipped game templates are readable", "[Unit][DotNet][Template]")
{
    const std::vector<FGameTemplate> templates = ScanGameTemplates();
    REQUIRE_FALSE(templates.empty());

    for (const FGameTemplate& gameTemplate : templates)
    {
        INFO("template " << gameTemplate.id);
        CHECK_FALSE(gameTemplate.id.empty());
        CHECK_FALSE(gameTemplate.displayName.empty());
        CHECK_FALSE(gameTemplate.description.empty());
        // Every template must produce a buildable project, which means a csproj and a game class,
        // laid out the way a project is: C# under Scripts/.
        CHECK(std::filesystem::exists(gameTemplate.directory / "files" / "Scripts" / "__ProjectName__.csproj"));
        CHECK(std::filesystem::exists(gameTemplate.directory / "files" / "Scripts" / "__ProjectName__Game.cs"));
    }

    // Sorted by (sortOrder, id), so a menu built from this is stable across runs.
    for (size_t i = 1; i < templates.size(); ++i)
    {
        const bool ordered = templates[i - 1].sortOrder < templates[i].sortOrder ||
                             (templates[i - 1].sortOrder == templates[i].sortOrder &&
                              templates[i - 1].id < templates[i].id);
        CHECK(ordered);
    }
}

TEST_CASE("a new game request is validated before anything is written", "[Unit][DotNet][Template]")
{
    const auto request = [](std::string projectName, std::string gameId)
    {
        FNewGameRequest value;
        value.templateId = "blank";
        value.projectName = std::move(projectName);
        value.displayName = "Test Game";
        value.gameId = std::move(gameId);
        return value;
    };

    std::string error;
    CHECK(ValidateNewGameRequest(request("MyTestGame", "mytestgame"), error));

    CHECK_FALSE(ValidateNewGameRequest(request("", "mytestgame"), error));
    CHECK_FALSE(ValidateNewGameRequest(request("9Lives", "ninelives"), error));
    CHECK_FALSE(ValidateNewGameRequest(request("My Game", "mygame"), error));
    CHECK_FALSE(ValidateNewGameRequest(request("My.Game", "mygame"), error));
    // The GkNext prefix belongs to the shared managed assemblies.
    CHECK_FALSE(ValidateNewGameRequest(request("GkNextThing", "gknextthing"), error));
    CHECK_FALSE(ValidateNewGameRequest(request("MyTestGame", "MyTestGame"), error));
    CHECK_FALSE(ValidateNewGameRequest(request("MyTestGame", "my game"), error));
    // An id that already exists: 'sandbox' is committed as projects/Sandbox.
    CHECK_FALSE(ValidateNewGameRequest(request("MyTestGame", "sandbox"), error));
    // A project directory that already exists, in any letter case: projects/Flappy.
    CHECK_FALSE(ValidateNewGameRequest(request("flappy", "someotherid"), error));

    FNewGameRequest noTemplate = request("MyTestGame", "mytestgame");
    noTemplate.templateId.clear();
    CHECK_FALSE(ValidateNewGameRequest(noTemplate, error));

    // Every rejection has to say something the dialog can show; an empty message is a silent fail.
    CHECK_FALSE(error.empty());
}

TEST_CASE("creating a game writes a substituted project and a manifest", "[Unit][DotNet][Template]")
{
    // Scanned before the override, so the templates come from the repository rather than the
    // scratch tree the project is written into.
    const std::vector<FGameTemplate> templates = ScanGameTemplates();
    REQUIRE_FALSE(templates.empty());

    const auto blank = std::find_if(templates.begin(), templates.end(),
                                    [](const FGameTemplate& t) { return t.id == "blank"; });
    REQUIRE(blank != templates.end());

    const FScratchDirectory scratch("gkNextTemplateTest");
    const std::filesystem::path projects = scratch.path / "projects";
    std::filesystem::create_directories(projects);
    const FProjectsSourceOverride projectsOverride(projects);

    FNewGameRequest request;
    request.templateId = blank->id;
    request.projectName = "TemplateProbeGame";
    request.displayName = "Template Probe";
    request.gameId = "templateprobegame";

    const FNewGameResult result = CreateManagedGame(*blank, request);
    INFO(result.error);
    REQUIRE(result.created);

    // One directory per game, holding everything the game owns.
    CHECK(Generic(result.projectDirectory) == Generic(projects / "TemplateProbeGame"));
    CHECK(Generic(result.manifestFile) == Generic(result.projectDirectory / "templateprobegame.game.json"));
    CHECK(Generic(result.projectFile) == Generic(result.projectDirectory / "Scripts" / "TemplateProbeGame.csproj"));
    CHECK(std::filesystem::exists(result.manifestFile));
    CHECK(std::filesystem::exists(result.projectFile));
    CHECK(std::filesystem::is_directory(result.projectDirectory / "Content"));
    // The blank template ships a config its code reads through GameContent.
    CHECK(std::filesystem::exists(result.projectDirectory / "Content" / "configs" / "tuning.json"));

    CHECK(result.manifest.assembly == "templateprobegame/TemplateProbeGame.dll");
    CHECK(result.manifest.project == "Scripts/TemplateProbeGame.csproj");
    CHECK(result.manifest.directory == "assets/projects/TemplateProbeGame");
    CHECK(result.manifest.ContentRoot() == "assets/projects/TemplateProbeGame/Content");
    // A rebuild reads the manifest's runtime copy and has to find its way back to the sources.
    CHECK(Generic(ResolveProjectSourceDirectory(result.manifest)) == Generic(result.projectDirectory));

    // The publish subdirectory a host derives from the assembly path has to be the manifest id, or
    // a rebuild publishes somewhere the next load will not look.
    CHECK(std::filesystem::path(result.manifest.assembly).parent_path().string() == result.manifest.id);

    // What was written reads back as the same game.
    const std::optional<FManagedGameManifest> reloaded = LoadManagedGameManifest(result.manifestFile.string());
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->id == "templateprobegame");
    CHECK(reloaded->project == "Scripts/TemplateProbeGame.csproj");

    const std::string gameSource =
        ReadWholeFile(result.projectDirectory / "Scripts" / "TemplateProbeGameGame.cs");
    CHECK_FALSE(gameSource.empty());
    CHECK(gameSource.find("{{") == std::string::npos);
    CHECK(gameSource.find("__ProjectName__") == std::string::npos);
    CHECK(gameSource.find("namespace TemplateProbeGame;") != std::string::npos);
    CHECK(gameSource.find("class TemplateProbeGameGame") != std::string::npos);

    const std::string projectFile = ReadWholeFile(result.projectFile);
    CHECK(projectFile.find("<AssemblyName>TemplateProbeGame</AssemblyName>") != std::string::npos);

    // When the host has a runtime asset tree, the new project is mirrored into it so it can be
    // played without a C++ build.
    if (!result.runtimeProjectDirectory.empty())
    {
        CHECK(std::filesystem::exists(result.runtimeProjectDirectory / "templateprobegame.game.json"));
        CHECK(std::filesystem::exists(result.runtimeProjectDirectory / "Content" / "configs" / "tuning.json"));
    }

    // A second attempt with the same names must be refused rather than half-overwriting the first.
    std::string error;
    CHECK_FALSE(ValidateNewGameRequest(request, error));

    std::error_code ec;
    if (!result.runtimeProjectDirectory.empty())
    {
        std::filesystem::remove_all(result.runtimeProjectDirectory, ec);
    }
}

TEST_CASE("a game project's manifest resolves its own content", "[Unit][DotNet][Template]")
{
    const FScratchDirectory scratch("gkNextProjectLayoutTest");
    const std::filesystem::path root = scratch.path / "projects";

    WriteWholeFile(root / "Alpha" / "alpha.game.json", R"({
        "id": "alpha",
        "assembly": "alpha/Alpha.dll",
        "project": "Scripts/Alpha.csproj",
        "icon": "Content/icon.png",
        "initialScene": "Content/scenes/arena.scad"
    })");
    WriteWholeFile(root / "Alpha" / "Scripts" / "Alpha.csproj", "<Project />");
    WriteWholeFile(root / "Alpha" / "Content" / "configs" / "tuning.json", "{}");
    // Manifest-shaped content is content: a file named like a manifest inside Content/ must not
    // show up as a second game.
    WriteWholeFile(root / "Alpha" / "Content" / "decoy.game.json", R"({"id": "decoy", "assembly": "x/X.dll"})");

    // Engine asset paths and built-in scene names are not project content and pass through.
    WriteWholeFile(root / "Beta" / "beta.game.json", R"({
        "id": "beta",
        "assembly": "beta/Beta.dll",
        "icon": "assets/icons/Beta.png",
        "initialScene": "Empty.proc"
    })");

    const std::vector<FManagedGameManifest> manifests = ScanManagedGameManifests(root.string());
    REQUIRE(manifests.size() == 2);
    CHECK(manifests[0].id == "alpha");
    CHECK(manifests[1].id == "beta");

    const FManagedGameManifest& alpha = manifests[0];
    const std::string alphaDirectory = Generic(root / "Alpha");
    CHECK(alpha.directory == alphaDirectory);
    CHECK(alpha.ContentRoot() == alphaDirectory + "/Content");
    CHECK(alpha.icon == alphaDirectory + "/Content/icon.png");
    CHECK(alpha.initialScene == alphaDirectory + "/Content/scenes/arena.scad");
    // Read straight from a project directory with Scripts/ in it: that directory is the source.
    CHECK(Generic(ResolveProjectSourceDirectory(alpha)) == alphaDirectory);

    const FManagedGameManifest& beta = manifests[1];
    CHECK(beta.icon == "assets/icons/Beta.png");
    CHECK(beta.initialScene == "Empty.proc");

    // The shipped projects are where every host looks for games.
    const std::vector<FManagedGameManifest> shipped = ScanManagedGameManifests();
    for (const char* id : {"brotato3d", "flappy", "sandbox"})
    {
        INFO("game " << id);
        const auto it = std::find_if(shipped.begin(), shipped.end(),
                                     [id](const FManagedGameManifest& m) { return m.id == id; });
        REQUIRE(it != shipped.end());
        CHECK(it->directory.rfind(kManagedGameProjectsDirectory, 0) == 0);
        CHECK_FALSE(it->project.empty());
    }
}
