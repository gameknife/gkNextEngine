#pragma once

// ============================================================================
// CombatEffects.hpp - What a gunfight looks like: muzzle flash, tracers, blood
// sprays shaped by the round and the hit zone, dust and sparks where rounds
// hit the world, and a pool spreading under the dead.
//
// Pure presentation. Meshes are injected in BeforeSceneRebuild (the scene
// cannot grow models later); every effect is a pre-created, raycast-invisible
// node from a fixed pool, parked below the map when idle, so firing never
// allocates or rebuilds the scene.
// ============================================================================

#include <array>
#include <memory>
#include <random>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"

namespace Assets
{
    class Model;
    class Node;
    class Scene;
    struct FMaterial;
}

namespace NextDayz
{
    class CombatEffects
    {
    public:
        void InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
        void OnSceneLoaded(Assets::Scene& scene);
        void OnSceneUnloaded();
        void Reset();

        // `scale` ~1 for a rifle seen from third person; smaller in first person.
        void MuzzleFlash(const glm::vec3& position, const glm::vec3& forward, float scale);
        void Tracer(const glm::vec3& from, const glm::vec3& to);
        // `direction` is the round's travel, `normal` the hit surface.
        void BloodHit(const glm::vec3& point, const glm::vec3& direction, const glm::vec3& normal, EHitZone zone,
                      bool killed);
        void WorldImpact(const glm::vec3& point, const glm::vec3& normal);
        // Spreads over the ground (`groundNormal`) under a body after
        // `delaySeconds`, then stays.
        void BloodPool(const glm::vec3& position, const glm::vec3& groundNormal, float delaySeconds);
        void Update(float deltaSeconds);

        int ActiveParticles() const;
        int ActiveTracers() const;
        bool FlashVisible() const { return flash_.remaining > 0.0f; }

    private:
        enum class EKind : uint8_t
        {
            Blood,
            Dust,
            Spark,
            Count,
        };

        struct FParticle
        {
            std::shared_ptr<Assets::Node> node;
            glm::vec3 position{0.0f};
            glm::vec3 velocity{0.0f};
            glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec3 spin{0.0f};
            float size = 0.0f;
            float life = 0.0f;
            float age = 0.0f;
            float gravity = 0.0f;
            float drag = 0.0f;
            bool active = false;
        };

        struct FTracer
        {
            std::shared_ptr<Assets::Node> node;
            glm::vec3 from{0.0f};
            glm::vec3 direction{0.0f, 0.0f, 1.0f};
            float distance = 0.0f;
            float travelled = 0.0f;
            bool active = false;
        };

        struct FFlash
        {
            std::shared_ptr<Assets::Node> node;
            float remaining = 0.0f;
        };

        // A pool is a few overlapping flattened blobs, so it spreads irregularly.
        static constexpr size_t kPoolBlobs = 3;
        struct FPool
        {
            std::array<std::shared_ptr<Assets::Node>, kPoolBlobs> nodes;
            std::array<glm::vec3, kPoolBlobs> offsets{};   // x/z on the ground, y = radius scale
            glm::vec3 position{0.0f};
            glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
            float delay = 0.0f;
            float age = 0.0f;
            float radius = 0.0f;
            bool active = false;
        };

        FParticle* Acquire(EKind kind);
        void Emit(EKind kind, const glm::vec3& position, const glm::vec3& velocity, float size, float life,
                  float gravity, float drag);
        float Random(float lo, float hi);
        glm::vec3 RandomUnit();
        void Park(Assets::Node& node);

        // injected (valid for the next scene)
        uint32_t cubeModelId_ = 0;
        uint32_t blobModelId_ = 0;
        uint32_t flashModelId_ = 0;
        std::array<uint32_t, static_cast<size_t>(EKind::Count)> kindMaterialIds_{};
        uint32_t tracerMaterialId_ = 0;
        uint32_t flashMaterialId_ = 0;
        uint32_t poolMaterialId_ = 0;
        bool injected_ = false;

        // runtime pools
        Assets::Scene* scene_ = nullptr;
        std::array<std::vector<FParticle>, static_cast<size_t>(EKind::Count)> particles_;
        std::array<size_t, static_cast<size_t>(EKind::Count)> nextParticle_{};
        std::vector<FTracer> tracers_;
        size_t nextTracer_ = 0;
        std::vector<FPool> pools_;
        size_t nextPool_ = 0;
        FFlash flash_;
        std::mt19937 rng_{0xB100Du};
    };
}
