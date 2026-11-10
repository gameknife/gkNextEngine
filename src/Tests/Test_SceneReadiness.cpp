#include "Engine/Common/CoreMinimal.hpp"
#include "TestCommon.hpp"

#include "Engine/Assets/GPU/Texture.hpp"

#include <chrono>
#include <thread>

TEST_CASE_METHOD(EngineTestFixture, "Scene readiness includes queued loads and texture completion",
                 "[GPU][Integration][SceneReadiness]")
{
    const auto waitUntil = [this](const auto& predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!predicate() && std::chrono::steady_clock::now() < deadline)
        {
            Simulate(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return predicate();
    };
    REQUIRE(waitUntil([this] { return engine_->IsSceneReady(); }));

    // Neither Running nor the startup root node proves that the queued scene has loaded.
    REQUIRE(engine_->GetEngineStatus() == NextRenderer::EApplicationStatus::Running);
    REQUIRE_FALSE(engine_->GetScene().Nodes().empty());
    engine_->RequestLoadScene({.filename = "Empty.proc"});
    engine_->RequestLoadScene({.filename = "Empty.proc", .append = true});
    CHECK(engine_->GetEngineStatus() == NextRenderer::EApplicationStatus::Running);
    CHECK_FALSE(engine_->IsSceneReady());
    REQUIRE(waitUntil([this] { return engine_->IsSceneReady(); }));

    std::vector<uint8_t> image;
    REQUIRE(Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(
        "assets/brand/gknext_logo_icon_125.png", image));
    REQUIRE_FALSE(image.empty());
    auto* textures = Assets::GlobalTexturePool::GetInstance();
    const uint32_t textureId = Assets::GlobalTexturePool::LoadTexture(
        "scene_readiness_test.png", "image/png", image.data(), image.size(), true);
    CHECK(textures->PendingTextureLoads() > 0);
    CHECK_FALSE(engine_->IsSceneReady());
    // Even a finished worker is insufficient until its main-thread callback has run.
    REQUIRE(waitUntil([this] { return engine_->IsSceneReady(); }));
    CHECK(textures->PendingTextureLoads() == 0);
    textures->ReleaseTexture(textureId);

    engine_->RequestLoadScene({.filename = "missing.scene_readiness_test"});
    REQUIRE(waitUntil([this] { return !engine_->GetSceneLoadError().empty(); }));
    CHECK_FALSE(engine_->IsSceneReady());
    CHECK(engine_->GetSceneLoadError().find("missing.scene_readiness_test") != std::string::npos);

    engine_->RequestLoadScene({.filename = "Empty.proc"});
    REQUIRE(waitUntil([this] { return engine_->IsSceneReady(); }));
    CHECK(engine_->GetSceneLoadError().empty());
}
