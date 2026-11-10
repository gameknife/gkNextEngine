// What a host that rebuilds a SCAD level while it runs relies on from the loader: the parse-only
// source check that keeps a broken file from blanking the scene, the closure it reports for file
// watching, the load report with warning text, vector parameters in node metadata, and the
// collision opt-out. ScadPlay's live loop and its agent context are built on exactly these.

#include <catch2/catch_all.hpp>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Modules/ScadLoader/FScadLoader.h"
#include "Modules/ScadLoader/ScadModule.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

namespace
{
    class LiveScadDir
    {
    public:
        LiveScadDir()
        {
            const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            dir_ = std::filesystem::temp_directory_path() / ("scad_live_test_" + suffix);
            std::filesystem::create_directories(dir_ / "lib");
        }
        ~LiveScadDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
        std::filesystem::path Write(const std::string& name, const std::string& contents) const
        {
            const std::filesystem::path path = dir_ / name;
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            REQUIRE(out.is_open());
            out << contents;
            return path;
        }

    private:
        std::filesystem::path dir_;
    };

    struct FLoadedScad
    {
        Assets::EnvironmentSetting environment;
        std::vector<std::shared_ptr<Assets::Node>> nodes;
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;

        bool Load(const std::filesystem::path& path)
        {
            return Assets::FScadLoader::LoadScadScene(path.string(), environment, nodes, models, materials, lights,
                                                      tracks, skeletons);
        }

        const Assets::Node* Find(const std::string& name) const
        {
            for (const auto& node : nodes)
            {
                if (node->GetName() == name)
                {
                    return node.get();
                }
            }
            return nullptr;
        }
    };

    bool ContainsPath(const std::vector<std::string>& paths, const std::filesystem::path& expected)
    {
        const std::filesystem::path normal = expected.lexically_normal();
        return std::any_of(paths.begin(), paths.end(),
                           [&normal](const std::string& path)
                           {
                               std::error_code ec;
                               return std::filesystem::equivalent(std::filesystem::path(path), normal, ec);
                           });
    }
} // namespace

TEST_CASE("Scad metadata joins flat vectors and drops nested ones", "[Unit][Scad][Live]")
{
    LiveScadDir dir;
    const std::filesystem::path main = dir.Write(
        "vectors.scad",
        "module door(on = [\"plate_a\", \"plate_b\"], travel = [0, 0, 3], path = [[0, 0], [1, 1]],\n"
        "            bad = [\"x,y\"], flags = [true, false]) { cube(1); }\n"
        "door();\n");

    FLoadedScad scene;
    REQUIRE(scene.Load(main));
    const Assets::Node* door = scene.Find("door");
    REQUIRE(door != nullptr);
    // A string element holding the separator cannot round-trip, so the whole vector goes, like a
    // nested one; everything else keeps its order.
    CHECK(door->GetMetadata() == "on=plate_a,plate_b;travel=0,0,3;flags=true,false");
}

TEST_CASE("Scad collision none opts a module subtree out of collision", "[Unit][Scad][Live]")
{
    LiveScadDir dir;
    const std::filesystem::path main = dir.Write(
        "collision.scad",
        "module gem() { color([1, 0, 0]) sphere(0.3); }\n"
        "module pickup(collision = \"none\") { cube(0.5); gem(); }\n"
        "module wall() { cube([4, 0.2, 2]); }\n"
        "pickup();\n"
        "translate([3, 0, 0]) wall();\n");

    FLoadedScad scene;
    REQUIRE(scene.Load(main));

    const auto rayCastVisible = [](const Assets::Node* node)
    {
        REQUIRE(node != nullptr);
        const auto* render = node->GetComponent<Runtime::RenderComponent>();
        REQUIRE(render != nullptr);
        return render->GetRayCastVisible();
    };
    CHECK_FALSE(rayCastVisible(scene.Find("pickup")));
    // Inherited: the gem is its own module node under the pickup.
    CHECK_FALSE(rayCastVisible(scene.Find("gem")));
    CHECK(rayCastVisible(scene.Find("wall")));
}

TEST_CASE("Scad load report carries warning text, counts and the include closure", "[Unit][Scad][Live]")
{
    LiveScadDir dir;
    const std::filesystem::path kit = dir.Write("lib/kit_live.scad", "module block() { cube(1); }\n");
    const std::filesystem::path main =
        dir.Write("level.scad", "use <lib/kit_live.scad>\nblock();\nnot_a_module_anywhere();\n");

    FLoadedScad scene;
    REQUIRE(scene.Load(main));

    Assets::FSceneSourceReport report;
    REQUIRE(Assets::FLoaderRegistry::Get().FindLoadReport(main.string(), report));
    CHECK(report.ok);
    CHECK(report.error.empty());
    CHECK(report.nodeCount >= 1);
    CHECK(report.triangleCount > 0);
    CHECK(report.warningCount >= 1);
    CHECK(std::any_of(report.warnings.begin(), report.warnings.end(),
                      [](const std::string& warning)
                      { return warning.find("not_a_module_anywhere") != std::string::npos; }));
    REQUIRE(report.dependencies.size() == 2);
    CHECK(ContainsPath({report.dependencies.front()}, main));
    CHECK(ContainsPath(report.dependencies, kit));
}

TEST_CASE("Scad source check refuses a syntax error without building anything", "[Unit][Scad][Live]")
{
    Modules::Scad::Register();
    const Assets::FSceneSourceCheckFn* check = Assets::FLoaderRegistry::Get().FindSceneSourceCheck(".SCAD");
    REQUIRE(check != nullptr);

    LiveScadDir dir;
    const std::filesystem::path kit = dir.Write("lib/kit_live.scad", "module block() { cube(1); }\n");
    const std::filesystem::path good = dir.Write("good.scad", "use <lib/kit_live.scad>\nblock();\n");
    const std::filesystem::path broken = dir.Write("broken.scad", "use <lib/kit_live.scad>\nblock(\n");
    const std::filesystem::path missing = dir.Write("missing.scad", "use <lib/kit_gone.scad>\nblock();\n");

    Assets::FSceneSourceReport report;
    CHECK((*check)(good.string(), report));
    CHECK(report.ok);
    CHECK(ContainsPath(report.dependencies, kit));

    CHECK_FALSE((*check)(broken.string(), report));
    CHECK_FALSE(report.ok);
    CHECK(report.error.find("broken.scad") != std::string::npos);

    // The file that could not be read is still reported, so a watcher sees it appear.
    CHECK_FALSE((*check)(missing.string(), report));
    CHECK(report.error.find("kit_gone.scad") != std::string::npos);
    CHECK(std::any_of(report.dependencies.begin(), report.dependencies.end(),
                      [](const std::string& path) { return path.find("kit_gone.scad") != std::string::npos; }));
}

TEST_CASE("Scad scene node instance ids are unique across parallel top-level statements", "[Unit][Scad][Live]")
{
    // Enough top-level calls to take the evaluator's parallel path, each with a nested call.
    LiveScadDir dir;
    std::string source = "module inner() { cube(0.5); }\nmodule outer() { cube(1); inner(); }\n";
    for (int i = 0; i < 12; ++i)
    {
        source += "translate([" + std::to_string(i * 2) + ", 0, 0]) outer();\n";
    }
    const std::filesystem::path main = dir.Write("ids.scad", source);

    FLoadedScad scene;
    REQUIRE(scene.Load(main));
    REQUIRE(scene.nodes.size() >= 24);
    std::unordered_set<uint32_t> ids;
    for (const auto& node : scene.nodes)
    {
        CHECK(ids.insert(node->GetInstanceId()).second);
    }
}
