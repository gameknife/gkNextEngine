#include "Engine/Common/CoreMinimal.hpp"
#include <catch2/catch_all.hpp>
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Runtime/Scene/TerrainGrass.hpp"
#include "Engine/Runtime/Components/TerrainBiome.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace
{
    std::shared_ptr<Runtime::TerrainComponent::FGridData> GrassTestGrid()
    {
        auto grid = std::make_shared<Runtime::TerrainComponent::FGridData>();
        grid->cellsX = grid->cellsY = 8;
        grid->sizeX = grid->sizeY = 64;
        grid->seed = 1741;
        grid->localToWorld = glm::rotate(glm::dmat4(1), -glm::half_pi<double>(), glm::dvec3(1, 0, 0));
        grid->worldToLocal = glm::inverse(grid->localToWorld);
        for (int y = 0; y <= 8; ++y)
            for (int x = 0; x <= 8; ++x)
                grid->verts.emplace_back(x * 8 - 32, y * 8 - 32, 2.0);
        grid->diagFlip.resize(64, 0);
        grid->biome.resize(64, uint8_t(Runtime::ETerrainBiome::Grass));
        grid->flags.resize(64, 0);
        grid->waterLevel.resize(64, 3);
        return grid;
    }
}

TEST_CASE("Runtime grass follows terrain semantics and exact height", "[Unit][TerrainGrass]")
{
    Runtime::TerrainComponent terrain;
    auto grid = GrassTestGrid();
    terrain.SetGridData(grid);
    const std::array<const Runtime::TerrainComponent*, 1> terrains{&terrain};
    using Grass = Runtime::FTerrainGrassSystem;
    const auto first = Grass::GenerateTile(terrains, 0, 0, 1.0f);
    REQUIRE(first.size() > 110);
    const auto again = Grass::GenerateTile(terrains, 0, 0, 1.0f);
    REQUIRE(first.size() == again.size());
    for (size_t i = 0; i < first.size(); ++i)
    {
        CHECK(first[i].position == again[i].position);
        CHECK(first[i].position.y == Catch::Approx(1.975f));
        CHECK(first[i].normal.y == Catch::Approx(1.0f));
        CHECK(first[i].position.x >= 0.0f);
        CHECK(first[i].position.x < 8.0f);
    }
    CHECK(Grass::GenerateTile(terrains, 4, 0, 1).empty()); // strict edge, no clamped growth
    CHECK(Grass::GenerateTile(terrains, 0, 0, 0).empty());
    const auto thin = Grass::GenerateTile(terrains, 0, 0, 0.3f);
    REQUIRE(thin.size() < first.size());
    for (const auto& p : thin)
        CHECK(std::any_of(first.begin(), first.end(), [&](const auto& q) { return p.position == q.position; }));
    for (uint8_t biome = 3; biome < uint8_t(Runtime::ETerrainBiome::Count); ++biome)
    {
        std::fill(grid->biome.begin(), grid->biome.end(), biome);
        CHECK(Grass::GenerateTile(terrains, 0, 0, 1).empty());
    }
    std::fill(grid->biome.begin(), grid->biome.end(), 0);
    for (const uint8_t flag : {terrain.kFlagWater, terrain.kFlagRoad, terrain.kFlagPad})
    {
        std::fill(grid->flags.begin(), grid->flags.end(), flag);
        CHECK(Grass::GenerateTile(terrains, 0, 0, 1).empty());
    }
    std::fill(grid->flags.begin(), grid->flags.end(), 0);
    grid->seed++;
    CHECK(Grass::GenerateTile(terrains, 0, 0, 1)[0].seed != first[0].seed);
    for (auto& vertex : grid->verts) vertex.z = vertex.x; // 45-degree hillside
    CHECK(Grass::GenerateTile(terrains, 0, 0, 1).empty());
}

TEST_CASE("Grass poses share roots and preserve topology without extra LOD BLAS", "[Unit][TerrainGrass]")
{
    using Grass = Runtime::FTerrainGrassSystem;
    const std::array<uint32_t, 3> triangles = {120, 144, 256};
    for (uint32_t lod = 0; lod < Grass::lodCount; ++lod)
    {
        const auto rest = Grass::CreatePose(0, lod);
        float maxTipBend = 0.0f;
        for (uint32_t pose = 0; pose < Grass::poseCount; ++pose)
        {
            auto bent = Grass::CreatePose(pose, lod);
            bent.BuildLods();
            CHECK(bent.CPULodIndices().empty());
            REQUIRE(bent.NumberOfVertices() == rest.NumberOfVertices());
            CHECK(bent.CPUIndices() == rest.CPUIndices());
            CHECK(bent.NumberOfIndices() / 3 == triangles[lod]);
            bool moved = false;
            for (size_t i = 0; i < rest.CPUVertices().size(); ++i)
            {
                const auto& a = rest.CPUVertices()[i];
                const auto& b = bent.CPUVertices()[i];
                if (a.Position.y == 0.0f) CHECK(a.Position == b.Position);
                if (pose == Grass::poseCount - 1)
                    maxTipBend = std::max(maxTipBend, std::abs(b.Position.x - a.Position.x));
                moved |= a.Position != b.Position;
                CHECK(std::isfinite(b.Normal.x));
                CHECK(glm::length(b.Normal) == Catch::Approx(1.0f));
            }
            if (pose > 0) CHECK(moved);
        }
        CHECK(maxTipBend > 0.3f); // pronounced gusts, with the same anchored roots
    }
    CHECK(Grass::SelectPose(-1, 0) == 0);
    CHECK(Grass::SelectPose(2, 0) == 8);
    CHECK(Grass::SelectPose(0.57f, 4) == 4); // hysteresis at a quantisation boundary
    CHECK(Grass::SelectPose(0.60f, 4) == 5);
}

TEST_CASE("Low-poly grass blades are closed solids with outward faces", "[Unit][TerrainGrass]")
{
    for (uint32_t lod = 0; lod < Runtime::FTerrainGrassSystem::lodCount; ++lod)
    for (uint32_t pose = 0; pose < Runtime::FTerrainGrassSystem::poseCount; ++pose)
    {
        const auto model = Runtime::FTerrainGrassSystem::CreatePose(pose, lod);
        const auto& vertices = model.CPUVertices();
        const auto& indices = model.CPUIndices();
        std::map<std::array<float, 6>, uint32_t> edgeCounts;
        double volume = 0.0;
        for (size_t triangle = 0; triangle < indices.size(); triangle += 3)
        {
            const glm::dvec3 a = vertices[indices[triangle]].Position;
            const glm::dvec3 b = vertices[indices[triangle + 1]].Position;
            const glm::dvec3 c = vertices[indices[triangle + 2]].Position;
            volume += glm::dot(a, glm::cross(b, c)) / 6.0;
            for (size_t edge = 0; edge < 3; ++edge)
            {
                const auto& start = vertices[indices[triangle + edge]].Position;
                const auto& end = vertices[indices[triangle + (edge + 1) % 3]].Position;
                std::array<float, 3> first{start.x, start.y, start.z};
                std::array<float, 3> second{end.x, end.y, end.z};
                if (second < first) std::swap(first, second);
                ++edgeCounts[{first[0], first[1], first[2], second[0], second[1], second[2]}];
            }
        }
        CHECK(volume > 0.003); // Double-sided zero-thickness ribbons have zero enclosed volume.
        for (const auto& [edge, count] : edgeCounts) CHECK(count == 2);
    }
}

TEST_CASE("Grass asset staging survives the engine's unload-after-prepare lifecycle", "[Unit][TerrainGrass]")
{
    Runtime::FTerrainGrassSystem grass;
    auto node = Assets::Node::CreateNode("terrain", {}, glm::quat(1, 0, 0, 0), glm::vec3(1), 1);
    node->AddComponent(std::make_shared<Runtime::TerrainComponent>());
    const std::array nodes{node};
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    grass.Prepare(nodes, models, materials);
    REQUIRE(grass.MeshCount() == 27);
    grass.Clear(); // OnSceneUnloaded follows BeforeSceneRebuild in NextEngine
    CHECK(grass.MeshCount() == 27);
    CHECK(grass.ActiveCount() == 0);
    CHECK(models.size() == 27);
    grass.Prepare({}, models, materials); // switching to a non-terrain scene
    CHECK(grass.MeshCount() == 0);
    CHECK(models.size() == 27);
}

TEST_CASE("Grass selects translated terrain tiles and biome-specific coverage", "[Unit][TerrainGrass]")
{
    Runtime::TerrainComponent nearTerrain;
    Runtime::TerrainComponent farTerrain;
    auto nearGrid = GrassTestGrid();
    auto farGrid = GrassTestGrid();
    farGrid->localToWorld = glm::translate(glm::dmat4(1), glm::dvec3(96, 5, 0)) * farGrid->localToWorld;
    farGrid->worldToLocal = glm::inverse(farGrid->localToWorld);
    nearTerrain.SetGridData(nearGrid);
    farTerrain.SetGridData(farGrid);
    const std::array<const Runtime::TerrainComponent*, 2> terrains{&nearTerrain, &farTerrain};
    using Grass = Runtime::FTerrainGrassSystem;
    auto green = Grass::GenerateTile(terrains, 12, 0, 1);
    REQUIRE(green.size() > 40);
    for (const auto& p : green) CHECK(p.position.y == Catch::Approx(6.975f));
    std::fill(farGrid->biome.begin(), farGrid->biome.end(), uint8_t(Runtime::ETerrainBiome::GrassDark));
    const auto dark = Grass::GenerateTile(terrains, 12, 0, 1);
    CHECK(dark.size() == green.size());
    std::fill(farGrid->biome.begin(), farGrid->biome.end(), uint8_t(Runtime::ETerrainBiome::DryGrass));
    const auto dry = Grass::GenerateTile(terrains, 12, 0, 1);
    REQUIRE_FALSE(dry.empty());
    CHECK(dry.size() < green.size());
    for (const auto& p : dry) CHECK(p.biome == uint8_t(Runtime::ETerrainBiome::DryGrass));
    // A duplicated terrain producer cannot create duplicate grass at a seam.
    const std::array<const Runtime::TerrainComponent*, 2> duplicate{&farTerrain, &farTerrain};
    CHECK(Grass::GenerateTile(duplicate, 12, 0, 1).size() == dry.size());
}

TEST_CASE("Grass patch rings crossfade continuously out to four times the old radius", "[Unit][TerrainGrass]")
{
    using Grass = Runtime::FTerrainGrassSystem;
    CHECK(Grass::maxRadius == 48.0f * 4.0f);
    for (float distance = 36; distance <= 48; distance += 0.25f)
    {
        CHECK(Grass::Coverage(distance, 192, 0) + Grass::Coverage(distance, 192, 1) == Catch::Approx(1));
        CHECK(Grass::Coverage(distance, 192, 2) == 0);
    }
    for (float distance = 80; distance <= 96; distance += 0.25f)
    {
        CHECK(Grass::Coverage(distance, 192, 1) + Grass::Coverage(distance, 192, 2) == Catch::Approx(1));
        CHECK(Grass::Coverage(distance, 192, 0) == 0);
    }
    CHECK(Grass::Coverage(0, 192, 0) == 1);
    CHECK(Grass::Coverage(160, 192, 2) == 1);
    CHECK(Grass::Coverage(180, 192, 2) > Grass::Coverage(188, 192, 2));
    for (uint32_t lod = 0; lod < Grass::lodCount; ++lod)
    {
        CHECK(Grass::Coverage(192, 192, lod) == 0);
        CHECK(Grass::Coverage(200, 192, lod) == 0);
        CHECK(Grass::Coverage(48, 48, lod) == 0);
    }
}

TEST_CASE("Large grass patches reduce instance density and respect the whole footprint", "[Unit][TerrainGrass]")
{
    Runtime::TerrainComponent terrain;
    auto grid = GrassTestGrid();
    terrain.SetGridData(grid);
    const std::array<const Runtime::TerrainComponent*, 1> terrains{&terrain};
    using Grass = Runtime::FTerrainGrassSystem;
    const auto nearPlacements = Grass::GenerateTile(terrains, 0, 0, 1, 0);
    const auto middle = Grass::GenerateTile(terrains, 0, 0, 1, 1);
    const auto farPlacements = Grass::GenerateTile(terrains, 0, 0, 1, 2);
    REQUIRE_FALSE(farPlacements.empty());
    CHECK(nearPlacements.size() > middle.size() * 5);
    CHECK(middle.size() > farPlacements.size());
    CHECK(farPlacements.size() <= 4);
    for (uint32_t lod = 0; lod < Grass::lodCount; ++lod)
    {
        const auto generated = Grass::GenerateTile(terrains, 0, 0, 1, lod);
        const auto again = Grass::GenerateTile(terrains, 0, 0, 1, lod);
        REQUIRE(generated.size() == again.size());
        for (size_t i = 0; i < generated.size(); ++i)
        {
            CHECK(generated[i].lod == lod);
            CHECK(generated[i].position == again[i].position);
            CHECK(generated[i].position.y == Catch::Approx(1.975f));
        }
    }
    // The cell just north of the candidate tile is wet: large patches must not spill into it.
    grid->flags[4 * 8 + 4] = terrain.kFlagWater;
    const auto edge = Grass::GenerateTile(terrains, 0, 0, 1, 2);
    CHECK(edge.size() < farPlacements.size());
    for (const auto& p : edge) CHECK(p.position.z >= 3.5f);
}

TEST_CASE("Grass exclusion distances are continuous and measured in world metres", "[Unit][TerrainGrass]")
{
    using Grass = Runtime::FTerrainGrassSystem;
    Runtime::TerrainComponent terrain;
    auto grid = GrassTestGrid();
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 4; ++x)
            grid->flags[y * 8 + x] = terrain.kFlagRoad; // left half, boundary at world X=0
    terrain.SetGridData(grid);
    CHECK(Grass::EdgeDistance(terrain, -1, 0, 8) == 0);
    CHECK(Grass::EdgeDistance(terrain, 0.25f, 0, 8) == Catch::Approx(0.25f));
    CHECK(Grass::EdgeDistance(terrain, 1.25f, 0, 8) == Catch::Approx(1.25f));
    CHECK(Grass::EdgeDistance(terrain, 7.99f, 0, 12) == Catch::Approx(7.99f));
    CHECK(Grass::EdgeDistance(terrain, 8.01f, 0, 12) == Catch::Approx(8.01f));
    CHECK(Grass::EdgeDistance(terrain, 20, 0, 8) == Catch::Approx(8));
    grid->localToWorld = glm::translate(glm::dmat4(1), glm::dvec3(10, 5, 20)) *
                         glm::rotate(glm::dmat4(1), 0.6, glm::dvec3(0, 1, 0)) *
                         glm::scale(glm::dmat4(1), glm::dvec3(2, 1, 3)) * grid->localToWorld;
    grid->worldToLocal = glm::inverse(grid->localToWorld);
    const auto point = grid->localToWorld * glm::dvec4(1.5, -2, 2, 1);
    CHECK(Grass::EdgeDistance(terrain, float(point.x), float(point.z), 8) == Catch::Approx(3).margin(0.00001));

    auto cornerGrid = GrassTestGrid();
    cornerGrid->flags[3 * 8 + 3] = terrain.kFlagRoad; // rectangle X [-8,0], Z [0,8]
    terrain.SetGridData(cornerGrid);
    CHECK(Grass::EdgeDistance(terrain, 3, 11, 8) == Catch::Approx(std::sqrt(18.0f)));
}

TEST_CASE("Grass thins and shortens deterministically beside roads water pads and bare soil", "[Unit][TerrainGrass]")
{
    using Grass = Runtime::FTerrainGrassSystem;
    Runtime::TerrainComponent terrain;
    const std::array<const Runtime::TerrainComponent*, 1> terrains{&terrain};
    const std::array<std::pair<uint8_t, uint8_t>, 4> exclusions = {
        std::pair{uint8_t(Runtime::ETerrainBiome::Grass), terrain.kFlagRoad},
        {uint8_t(Runtime::ETerrainBiome::Grass), terrain.kFlagWater},
        {uint8_t(Runtime::ETerrainBiome::Grass), terrain.kFlagPad},
        {uint8_t(Runtime::ETerrainBiome::Sand), uint8_t(0)}};
    for (const auto& [biome, flags] : exclusions)
    {
        auto grid = GrassTestGrid();
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 4; ++x)
            {
                grid->biome[y * 8 + x] = biome;
                grid->flags[y * 8 + x] = flags;
            }
        terrain.SetGridData(grid);
        for (uint32_t lod = 0; lod < Grass::lodCount; ++lod)
        {
            size_t originalCount = 0;
            size_t featheredCount = 0;
            size_t shortenedCount = 0;
            size_t sparseBandCount = 0;
            size_t denseBandCount = 0;
            for (int tileZ = -3; tileZ < 3; ++tileZ)
            {
                const auto original = Grass::GenerateTile(terrains, 0, tileZ, 1, lod, 0);
                const auto feathered = Grass::GenerateTile(terrains, 0, tileZ, 1, lod, 4);
                const auto again = Grass::GenerateTile(terrains, 0, tileZ, 1, lod, 4);
                originalCount += original.size();
                featheredCount += feathered.size();
                REQUIRE(feathered.size() == again.size());
                for (size_t i = 0; i < feathered.size(); ++i)
                {
                    const auto& p = feathered[i];
                    const auto found = std::find_if(original.begin(), original.end(), [&](const auto& other)
                        { return other.position == p.position; });
                    REQUIRE(found != original.end());
                    CHECK(p.position == again[i].position);
                    CHECK(p.scale == again[i].scale);
                    CHECK(p.position.x > 0); // exclusion itself stays empty
                    CHECK(p.scale.x == found->scale.x);
                    CHECK(p.scale.y <= found->scale.y);
                    CHECK(p.scale.y >= found->scale.y * 0.35f);
                    if (p.edgeWeight < 0.999f)
                    {
                        ++shortenedCount;
                        CHECK(p.scale.y < found->scale.y);
                    }
                    if (p.position.x >= 5 && lod == 0) CHECK(p.scale.y == found->scale.y);
                    if (p.position.x >= 1 && p.position.x < 3) ++sparseBandCount;
                    if (p.position.x >= 5 && p.position.x < 7) ++denseBandCount;
                }
            }
            CHECK(featheredCount < originalCount);
            CHECK(shortenedCount > 0);
            if (lod == 0) CHECK(denseBandCount > sparseBandCount * 2);
        }
    }
}

TEST_CASE("Grass sweep contacts cover fast motion and reject other floors", "[Unit][TerrainGrass][Interaction]")
{
    using Grass = Runtime::FTerrainGrassSystem;
    Runtime::FGrassPlacement patch;
    Runtime::FGrassSweep moving{{-3, 0, 0}, {3, 0, 0}, {6, 0, 0}, 0.55f, 1.0f};
    const auto contact = Grass::Contact(patch, moving);
    CHECK(contact.z > 0.9f);
    CHECK(contact.x > 0.0f);
    auto point = moving;
    point.from = point.to;
    CHECK(Grass::Contact(patch, point) == glm::vec3(0)); // endpoints alone miss this grass
    moving.from.y = moving.to.y = 2.0f;
    CHECK(Grass::Contact(patch, moving) == glm::vec3(0)); // upstairs or bridge
    moving.from.y = moving.to.y = 0.0f;
    patch.lod = 1;
    CHECK(Grass::Contact(patch, moving) == glm::vec3(0)); // no whole-clump deformation at distance
    patch.lod = 0;
    point.from = point.to = {0, 0, 0};
    point.velocity = {};
    patch.position = {0, 0, 0.3f};
    const auto standing = Grass::Contact(patch, point);
    CHECK(standing.y > 0.0f);
    patch.position.z = -0.3f;
    CHECK(Grass::Contact(patch, point).y < 0.0f); // parts to both sides of the body
    point.velocity = {6, 0, 0};
    CHECK(Grass::Contact(patch, point).z > standing.z);
    point.strength = 0;
    CHECK(Grass::Contact(patch, point) == glm::vec3(0));
}

TEST_CASE("Grass response bends quickly then recovers independently of update rate", "[Unit][TerrainGrass][Interaction]")
{
    using Grass = Runtime::FTerrainGrassSystem;
    const glm::vec3 target(0.7f, -0.4f, 0.6f);
    std::array<Runtime::FGrassResponse, 2> responses;
    for (int rate : {20, 60})
    {
        auto& response = responses[rate == 20 ? 0 : 1];
        for (int step = 0; step < rate / 5; ++step)
            Grass::StepResponse(response, target, 1.0f / rate, 1.25f);
        CHECK(response.deformation.z > 0.55f);
        CHECK(response.deformation.z <= 1.0f);
        const auto paused = response.deformation;
        Grass::StepResponse(response, {}, 0, 1.25f);
        CHECK(response.deformation == paused);
        for (int step = 0; step < rate / 2; ++step)
            Grass::StepResponse(response, {}, 1.0f / rate, 1.25f);
        CHECK(response.deformation.z < 0.3f);
        CHECK(response.deformation.z >= 0.0f);
    }
    CHECK(glm::distance(responses[0].deformation, responses[1].deformation) < 0.001f);
    auto response = responses[0];
    for (int step = 0; step < 100; ++step) Grass::StepResponse(response, {}, 0.05f, 1.25f);
    CHECK(response.deformation == glm::vec3(0));
    CHECK(response.velocity == glm::vec3(0));
    Grass::StepResponse(response, target, 2.0f, 1.25f); // long frame remains finite
    CHECK(glm::distance(response.deformation, target) < 0.001f);
}

TEST_CASE("Grass affine interaction anchors blade roots and shares render motion matrices", "[Unit][TerrainGrass][Interaction][Node]")
{
    const glm::vec3 at(4, 2, -3);
    const auto rotation = glm::angleAxis(0.35f, glm::normalize(glm::vec3(1, 1, 0)));
    const glm::vec3 scale(1.2f, 0.45f, 0.8f);
    auto parent = Assets::Node::CreateNode("parent", {3, 0, 2}, glm::quat(1, 0, 0, 0), {1, 1.1f, 1}, 1);
    auto node = Assets::Node::CreateNode("grass", at, rotation, scale, 2);
    node->SetParent(parent);
    node->TickVelocity();
    const auto previous = node->WorldTransform();
    node->SetShearedTransform(at, rotation, scale, {1.7f, -0.8f});
    REQUIRE(node->TickVelocity());
    Assets::NodeProxy proxy{};
    node->GetNodeProxy(proxy);
    CHECK(proxy.worldTS == node->WorldTransform());
    CHECK(std::abs(glm::determinant(proxy.worldTS)) > 0.1f);
    for (uint32_t pose = 0; pose < Runtime::FTerrainGrassSystem::poseCount; ++pose)
    {
        const auto mesh = Runtime::FTerrainGrassSystem::CreatePose(pose);
        for (const auto& vertex : mesh.CPUVertices())
        {
            const auto point = glm::vec4(vertex.Position, 1);
            const glm::vec3 now = proxy.worldTS * point;
            const glm::vec3 before = previous * point;
            if (vertex.Position.y == 0.0f) CHECK(glm::distance(now, before) < 0.00001f);
            const glm::vec3 motionPrevious = proxy.combinedPrevTS * glm::vec4(now, 1);
            CHECK(glm::distance(motionPrevious, before) < 0.00001f);
        }
    }
    node->SetTranslation({5, 2, -3}); // normal translation updates retain shear
    CHECK(glm::length(glm::vec3(node->WorldTransform()[1])) > glm::length(scale.y * glm::vec3(rotation * glm::vec3(0,1,0))));
    node->SetTransform(at, rotation, scale); // ordinary TRS explicitly clears it
    CHECK(node->WorldTransform() == previous);
}
