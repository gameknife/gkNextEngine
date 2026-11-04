#pragma once
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include <array>
#include <map>
#include <span>

namespace Assets
{
    class Model;
    class Node;
    class Scene;
    struct FMaterial;
}

namespace Runtime
{
    struct FTerrainGrassSettings
    {
        bool enabled = true;
        float radius = 192.0f;
        float density = 1.0f;
        float edgeWidth = 4.0f; // metres beyond the patch's exclusion clearance
        float transmission = 1.0f;
        float windStrength = 0.65f;
        float windYaw = 0.7f; // radians, +X toward -Z
        float windSpeed = 1.0f;
    };

    struct FGrassPlacement
    {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec3 scale{1.0f};
        float phase = 0.0f;
        float heading = 0.0f;
        float edgeWeight = 1.0f;
        uint8_t biome = 0;
        uint8_t lod = 0;
        uint32_t seed = 0;
    };

    // Runtime decoration over semantic terrain; no loader/module dependencies.
    // Prepare before uploading scene geometry, Bind after commit, then Tick from the host.
    // Three patch sizes, each with nine immutable wind meshes/BLAS.
    class FTerrainGrassSystem
    {
    public:
        static constexpr uint32_t poseCount = 9;
        static constexpr uint32_t lodCount = 3;
        static constexpr uint32_t maxInstances = 42000;
        static constexpr float maxRadius = 192.0f;
        static constexpr float tileSize = 8.0f;

        FTerrainGrassSettings settings;
        void Prepare(std::span<const std::shared_ptr<Assets::Node>> nodes,
                     std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
        void Bind(Assets::Scene& scene);
        // Release residency, retaining staged asset IDs: the engine calls BeforeSceneRebuild
        // before OnSceneUnloaded. Scene owns the actual nodes.
        void Clear();
        void Tick(Assets::Scene& scene, float dt, const glm::vec3& focus, float weather = 0.0f);

        uint32_t ActiveCount() const { return activeCount_; }
        uint32_t ActiveCount(uint32_t lod) const { return lod < lodCount ? activeByLod_[lod] : 0; }
        float MaxActiveDistance() const { return maxActiveDistance_; }
        uint32_t EdgeCount() const { return edgeCount_; }
        uint32_t PoolSize() const { return static_cast<uint32_t>(pool_.size()); }
        uint64_t PoseChanges() const { return poseChanges_; }
        uint64_t TileBuilds() const { return tileBuilds_; }
        uint32_t MeshCount() const { return prepared_ ? poseCount * lodCount : 0; }

        // CPU-only generation and wind selection, also used by tests/tools.
        static std::vector<FGrassPlacement> GenerateTile(std::span<const TerrainComponent* const> terrains,
                                                         int tileX, int tileZ, float density, uint32_t lod = 0,
                                                         float edgeWidth = 4.0f);
        // World-space distance to an excluded biome/flag cell, capped at maxDistance.
        static float EdgeDistance(const TerrainComponent& terrain, float worldX, float worldZ, float maxDistance);
        static float Coverage(float distance, float radius, uint32_t lod);
        static uint32_t SelectPose(float bend, uint32_t previous);
        static Assets::Model CreatePose(uint32_t pose, uint32_t lod = 0);

    private:
        struct FInstance
        {
            std::shared_ptr<Assets::Node> node;
            FGrassPlacement placement;
            uint32_t pose = 0;
        };
        void Stream(Assets::Scene& scene, const glm::vec3& focus, float radius);

        std::array<std::array<uint32_t, poseCount>, lodCount> models_{};
        std::array<std::array<uint32_t, 16>, 3> materials_{};
        std::vector<const TerrainComponent*> terrains_;
        std::map<std::array<int, 3>, std::vector<uint32_t>> tiles_; // lod, tileX, tileZ
        std::vector<FInstance> pool_;
        std::vector<uint32_t> free_;
        glm::vec3 lastFocus_{1.0e20f};
        float lastDensity_ = -1.0f;
        float lastRadius_ = -1.0f;
        float lastEdgeWidth_ = -1.0f;
        float lastTransmission_ = -1.0f;
        float windYaw_ = 0.0f;
        double time_ = 0.0;
        float updateClock_ = 0.0f;
        uint32_t activeCount_ = 0;
        std::array<uint32_t, lodCount> activeByLod_{};
        float maxActiveDistance_ = 0.0f;
        uint32_t edgeCount_ = 0;
        uint64_t poseChanges_ = 0;
        uint64_t tileBuilds_ = 0;
        bool prepared_ = false;
        bool lastEnabled_ = true;
    };
}
