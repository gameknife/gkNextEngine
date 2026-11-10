#include "CombatEffects.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

namespace NextDayz
{
    namespace
    {
        constexpr float kParkedY = -1000.0f;
        constexpr std::array<size_t, 3> kPoolSizes = {64, 32, 16};   // blood, dust, spark
        constexpr size_t kTracerCount = 12;
        constexpr size_t kBloodPoolCount = 12;
        constexpr float kTracerSpeed = 340.0f;
        constexpr float kTracerLength = 1.6f;
        constexpr float kTracerWidth = 0.014f;
        constexpr float kFlashSeconds = 0.05f;
        constexpr float kPoolGrowSeconds = 2.5f;

        void AppendBox(std::vector<Assets::Vertex>& vertices, std::vector<uint32_t>& indices, const glm::vec3& min,
                       const glm::vec3& max)
        {
            Assets::Model box = Assets::FProcModel::CreateBox(min, max);
            const uint32_t offset = static_cast<uint32_t>(vertices.size());
            vertices.insert(vertices.end(), box.CPUVertices().begin(), box.CPUVertices().end());
            for (uint32_t index : box.CPUIndices())
            {
                indices.push_back(offset + index);
            }
        }

        // A star of thin blades opening forward (+Z), the classic rifle flash.
        Assets::Model CreateFlashModel()
        {
            std::vector<Assets::Vertex> vertices;
            std::vector<uint32_t> indices;
            AppendBox(vertices, indices, {-0.035f, -0.035f, 0.0f}, {0.035f, 0.035f, 0.16f});
            AppendBox(vertices, indices, {-0.11f, -0.008f, 0.01f}, {0.11f, 0.008f, 0.06f});
            AppendBox(vertices, indices, {-0.008f, -0.11f, 0.01f}, {0.008f, 0.11f, 0.06f});
            AppendBox(vertices, indices, {-0.018f, -0.018f, 0.12f}, {0.018f, 0.018f, 0.30f});
            return Assets::Model::CreateFromGeometry("nd_fx_flash", std::move(vertices), std::move(indices));
        }

        glm::quat FromTo(const glm::vec3& from, const glm::vec3& to)
        {
            const float cosine = glm::dot(from, to);
            if (cosine > 0.9999f)
            {
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
            if (cosine < -0.9999f)
            {
                return glm::angleAxis(glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
            }
            const glm::vec3 axis = glm::cross(from, to);
            return glm::normalize(glm::quat(1.0f + cosine, axis.x, axis.y, axis.z));
        }

        std::shared_ptr<Assets::Node> CreateFxNode(Assets::Scene& scene, const std::string& name, uint32_t model,
                                                   uint32_t material)
        {
            auto node = Assets::SceneBuilder::CreateRenderNode(name, glm::vec3(0.0f, kParkedY, 0.0f), glm::vec3(1.0f),
                                                               scene.GenerateInstanceId(), model, material, true,
                                                               glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
            auto physics = std::make_shared<Runtime::PhysicsComponent>();
            physics->SetMobility(Runtime::ENodeMobility::Dynamic);
            node->AddComponent(physics);
            if (auto render = node->GetComponent<Runtime::RenderComponent>())
            {
                render->SetCastShadows(false);
            }
            scene.AddNode(node);
            return node;
        }
    }

    void CombatEffects::InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials)
    {
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.5f), glm::vec3(0.5f)));
        cubeModelId_ = static_cast<uint32_t>(models.size() - 1);
        models.push_back(CreateFlashModel());
        flashModelId_ = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 0.5f));
        blobModelId_ = static_cast<uint32_t>(models.size() - 1);

        // Emission is radiance on the same scale as the sun (480 in NextDayz):
        // single digits read as dark grey.
        using Assets::SceneBuilder::AddDiffuseLightMaterial;
        using Assets::SceneBuilder::AddLambertianMaterial;
        kindMaterialIds_[static_cast<size_t>(EKind::Blood)] = AddLambertianMaterial(materials, {0.42f, 0.03f, 0.03f});
        kindMaterialIds_[static_cast<size_t>(EKind::Dust)] = AddLambertianMaterial(materials, {0.46f, 0.42f, 0.36f});
        kindMaterialIds_[static_cast<size_t>(EKind::Spark)] =
            AddDiffuseLightMaterial(materials, {1.0f, 0.72f, 0.32f}, 300.0f);
        // Kept below the tonemapper's white point so they still read orange.
        tracerMaterialId_ = AddDiffuseLightMaterial(materials, {1.0f, 0.75f, 0.35f}, 180.0f);
        flashMaterialId_ = AddDiffuseLightMaterial(materials, {1.0f, 0.55f, 0.18f}, 140.0f);
        poolMaterialId_ = AddLambertianMaterial(materials, {0.16f, 0.01f, 0.01f});
        injected_ = true;
    }

    void CombatEffects::OnSceneLoaded(Assets::Scene& scene)
    {
        if (!injected_)
        {
            return;
        }
        scene_ = &scene;
        for (size_t kind = 0; kind < particles_.size(); ++kind)
        {
            particles_[kind].assign(kPoolSizes[kind], {});
            for (size_t i = 0; i < kPoolSizes[kind]; ++i)
            {
                // Blood is round (liquid); dust and sparks are chips.
                const uint32_t model = kind == static_cast<size_t>(EKind::Blood) ? blobModelId_ : cubeModelId_;
                particles_[kind][i].node =
                    CreateFxNode(scene, fmt::format("nd_fx_{}_{}", kind, i), model, kindMaterialIds_[kind]);
            }
            nextParticle_[kind] = 0;
        }
        tracers_.assign(kTracerCount, {});
        for (size_t i = 0; i < kTracerCount; ++i)
        {
            tracers_[i].node = CreateFxNode(scene, fmt::format("nd_fx_tracer_{}", i), cubeModelId_, tracerMaterialId_);
        }
        pools_.assign(kBloodPoolCount, {});
        for (size_t i = 0; i < kBloodPoolCount; ++i)
        {
            for (size_t blob = 0; blob < kPoolBlobs; ++blob)
            {
                pools_[i].nodes[blob] =
                    CreateFxNode(scene, fmt::format("nd_fx_pool_{}_{}", i, blob), blobModelId_, poolMaterialId_);
            }
        }
        flash_.node = CreateFxNode(scene, "nd_fx_flash", flashModelId_, flashMaterialId_);
        flash_.remaining = 0.0f;
        nextTracer_ = 0;
        nextPool_ = 0;
        scene.MarkDirty();
    }

    void CombatEffects::OnSceneUnloaded()
    {
        for (auto& pool : particles_)
        {
            pool.clear();
        }
        tracers_.clear();
        pools_.clear();
        flash_ = {};
        scene_ = nullptr;
    }

    void CombatEffects::Park(Assets::Node& node)
    {
        node.SetTransform(glm::vec3(0.0f, kParkedY, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    }

    void CombatEffects::Reset()
    {
        for (auto& pool : particles_)
        {
            for (FParticle& particle : pool)
            {
                particle.active = false;
                Park(*particle.node);
            }
        }
        for (FTracer& tracer : tracers_)
        {
            tracer.active = false;
            Park(*tracer.node);
        }
        for (FPool& pool : pools_)
        {
            pool.active = false;
            for (const auto& node : pool.nodes)
            {
                Park(*node);
            }
        }
        if (flash_.node)
        {
            flash_.remaining = 0.0f;
            Park(*flash_.node);
        }
    }

    float CombatEffects::Random(float lo, float hi)
    {
        return std::uniform_real_distribution<float>(lo, hi)(rng_);
    }

    glm::vec3 CombatEffects::RandomUnit()
    {
        const float z = Random(-1.0f, 1.0f);
        const float a = Random(0.0f, glm::two_pi<float>());
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return {r * std::cos(a), z, r * std::sin(a)};
    }

    CombatEffects::FParticle* CombatEffects::Acquire(EKind kind)
    {
        std::vector<FParticle>& pool = particles_[static_cast<size_t>(kind)];
        if (pool.empty())
        {
            return nullptr;
        }
        // Round robin: under heavy fire the oldest droplet is recycled first.
        size_t& next = nextParticle_[static_cast<size_t>(kind)];
        FParticle* particle = &pool[next];
        next = (next + 1) % pool.size();
        return particle;
    }

    void CombatEffects::Emit(EKind kind, const glm::vec3& position, const glm::vec3& velocity, float size,
                             float life, float gravity, float drag)
    {
        FParticle* particle = Acquire(kind);
        if (!particle)
        {
            return;
        }
        particle->position = position;
        particle->velocity = velocity;
        particle->rotation = glm::angleAxis(Random(0.0f, glm::two_pi<float>()), RandomUnit());
        particle->spin = RandomUnit() * Random(4.0f, 14.0f);
        particle->size = size;
        particle->life = life;
        particle->age = 0.0f;
        particle->gravity = gravity;
        particle->drag = drag;
        particle->active = true;
    }

    void CombatEffects::MuzzleFlash(const glm::vec3& position, const glm::vec3& forward, float scale)
    {
        if (!flash_.node || glm::length(forward) < 1.0e-4f)
        {
            return;
        }
        const glm::vec3 z = glm::normalize(forward);
        const glm::quat aim = glm::quatLookAtRH(-z, std::abs(z.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
        const glm::quat roll = glm::angleAxis(Random(0.0f, glm::two_pi<float>()), glm::vec3(0.0f, 0.0f, 1.0f));
        const float size = scale * Random(0.8f, 1.25f);
        flash_.node->SetTransform(position, aim * roll, glm::vec3(size, size, size * Random(0.8f, 1.4f)));
        flash_.remaining = kFlashSeconds;
    }

    void CombatEffects::Tracer(const glm::vec3& from, const glm::vec3& to)
    {
        if (tracers_.empty())
        {
            return;
        }
        const glm::vec3 delta = to - from;
        const float distance = glm::length(delta);
        if (distance < 2.0f)
        {
            return;
        }
        FTracer& tracer = tracers_[nextTracer_];
        nextTracer_ = (nextTracer_ + 1) % tracers_.size();
        tracer.from = from;
        tracer.direction = delta / distance;
        tracer.distance = distance;
        // Start a little out of the barrel so the streak does not sit in the flash.
        tracer.travelled = std::min(distance, 0.6f);
        tracer.active = true;
    }

    void CombatEffects::BloodHit(const glm::vec3& point, const glm::vec3& direction, const glm::vec3& normal,
                                 EHitZone zone, bool killed)
    {
        const glm::vec3 travel = glm::length(direction) > 1.0e-4f ? glm::normalize(direction) : -normal;
        const bool head = zone == EHitZone::Head;
        // A puff at the wound first: big, brief, the thing the eye catches.
        for (int i = 0; i < (head ? 3 : 2); ++i)
        {
            const glm::vec3 velocity = (normal + RandomUnit() * 0.5f) * Random(0.3f, 0.8f);
            Emit(EKind::Blood, point + normal * 0.03f, velocity, Random(0.10f, head ? 0.18f : 0.14f),
                 Random(0.12f, 0.2f), -1.0f, 4.0f);
        }
        // Exit spray carries on along the round out of the far side; a smaller
        // splash kicks back out of the wound toward the shooter.
        const int exitCount = head ? (killed ? 16 : 11) : (killed ? 9 : 6);
        for (int i = 0; i < exitCount; ++i)
        {
            const glm::vec3 velocity = (travel + RandomUnit() * 0.45f) * Random(1.5f, head ? 4.8f : 3.6f) +
                                       glm::vec3(0.0f, Random(0.2f, 1.2f), 0.0f);
            Emit(EKind::Blood, point + travel * (head ? 0.22f : 0.32f), velocity,
                 Random(0.03f, head ? 0.065f : 0.055f), Random(0.35f, 0.7f), -9.0f, 1.2f);
        }
        const int entryCount = head ? 6 : 4;
        for (int i = 0; i < entryCount; ++i)
        {
            const glm::vec3 velocity = (normal + RandomUnit() * 0.6f) * Random(0.6f, 1.8f);
            Emit(EKind::Blood, point, velocity, Random(0.03f, 0.05f), Random(0.25f, 0.45f), -7.0f, 2.0f);
        }
    }

    void CombatEffects::WorldImpact(const glm::vec3& point, const glm::vec3& normal)
    {
        const glm::vec3 n = glm::length(normal) > 1.0e-4f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
        for (int i = 0; i < 5; ++i)
        {
            const glm::vec3 velocity = (n + RandomUnit() * 0.7f) * Random(0.5f, 1.8f);
            Emit(EKind::Dust, point + n * 0.03f, velocity, Random(0.03f, 0.07f), Random(0.4f, 0.8f), -2.5f, 2.5f);
        }
        for (int i = 0; i < 3; ++i)
        {
            const glm::vec3 velocity = (n + RandomUnit() * 0.9f) * Random(3.0f, 7.0f);
            Emit(EKind::Spark, point + n * 0.02f, velocity, Random(0.012f, 0.02f), Random(0.08f, 0.16f), -9.0f, 0.5f);
        }
    }

    void CombatEffects::BloodPool(const glm::vec3& position, const glm::vec3& groundNormal, float delaySeconds)
    {
        if (pools_.empty())
        {
            return;
        }
        FPool& pool = pools_[nextPool_];
        nextPool_ = (nextPool_ + 1) % pools_.size();
        const glm::vec3 up = glm::length(groundNormal) > 1.0e-4f ? glm::normalize(groundNormal) : glm::vec3(0, 1, 0);
        pool.position = position;
        pool.rotation = FromTo(glm::vec3(0.0f, 1.0f, 0.0f), up) *
                        glm::angleAxis(Random(0.0f, glm::two_pi<float>()), glm::vec3(0.0f, 1.0f, 0.0f));
        pool.delay = delaySeconds;
        pool.age = 0.0f;
        pool.radius = Random(0.42f, 0.6f);
        pool.offsets[0] = glm::vec3(0.0f, 1.0f, 0.0f);
        for (size_t blob = 1; blob < kPoolBlobs; ++blob)
        {
            const float angle = Random(0.0f, glm::two_pi<float>());
            pool.offsets[blob] = glm::vec3(std::cos(angle) * 0.55f, Random(0.45f, 0.7f), std::sin(angle) * 0.55f);
        }
        pool.active = true;
        for (const auto& node : pool.nodes)
        {
            Park(*node);
        }
    }

    void CombatEffects::Update(float deltaSeconds)
    {
        const float dt = std::max(deltaSeconds, 0.0f);
        for (auto& pool : particles_)
        {
            for (FParticle& particle : pool)
            {
                if (!particle.active)
                {
                    continue;
                }
                particle.age += dt;
                if (particle.age >= particle.life)
                {
                    particle.active = false;
                    Park(*particle.node);
                    continue;
                }
                particle.velocity.y += particle.gravity * dt;
                particle.velocity *= std::exp(-particle.drag * dt);
                particle.position += particle.velocity * dt;
                const float spin = glm::length(particle.spin);
                if (spin > 0.0f)
                {
                    particle.rotation = glm::normalize(glm::angleAxis(spin * dt, particle.spin / spin) * particle.rotation);
                }
                // Hold size, then shrink away over the last third.
                const float u = particle.age / particle.life;
                const float scale = particle.size * std::clamp((1.0f - u) * 3.0f, 0.0f, 1.0f);
                particle.node->SetTransform(particle.position, particle.rotation, glm::vec3(scale));
            }
        }

        for (FTracer& tracer : tracers_)
        {
            if (!tracer.active)
            {
                continue;
            }
            tracer.travelled += kTracerSpeed * dt;
            if (tracer.travelled - kTracerLength >= tracer.distance)
            {
                tracer.active = false;
                Park(*tracer.node);
                continue;
            }
            const float head = std::min(tracer.travelled, tracer.distance);
            const float tail = std::max(0.0f, tracer.travelled - kTracerLength);
            const float length = std::max(head - tail, 0.01f);
            const glm::vec3 center = tracer.from + tracer.direction * ((head + tail) * 0.5f);
            const glm::quat rotation = glm::quatLookAtRH(
                -tracer.direction, std::abs(tracer.direction.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
            tracer.node->SetTransform(center, rotation, glm::vec3(kTracerWidth, kTracerWidth, length));
        }

        if (flash_.node && flash_.remaining > 0.0f)
        {
            flash_.remaining -= dt;
            if (flash_.remaining <= 0.0f)
            {
                Park(*flash_.node);
            }
        }

        for (FPool& pool : pools_)
        {
            if (!pool.active)
            {
                continue;
            }
            pool.age += dt;
            const float grow = std::clamp((pool.age - pool.delay) / kPoolGrowSeconds, 0.0f, 1.0f);
            if (grow <= 0.0f)
            {
                continue;
            }
            // Ease out: it spreads fast, then creeps. The side blobs lag the
            // main one so the edge grows unevenly.
            const float radius = pool.radius * (1.0f - (1.0f - grow) * (1.0f - grow));
            for (size_t blob = 0; blob < kPoolBlobs; ++blob)
            {
                const glm::vec3& offset = pool.offsets[blob];
                const float lag = blob == 0 ? 1.0f : std::clamp(grow * 1.4f - 0.3f, 0.0f, 1.0f);
                const float size = radius * offset.y * lag * 2.0f;
                const glm::vec3 center = pool.position +
                                         pool.rotation * glm::vec3(offset.x * radius, 0.006f + blob * 0.002f, offset.z * radius);
                pool.nodes[blob]->SetTransform(center, pool.rotation, glm::vec3(size, 0.008f, size * 0.85f));
            }
        }
    }

    int CombatEffects::ActiveParticles() const
    {
        int count = 0;
        for (const auto& pool : particles_)
        {
            count += static_cast<int>(std::count_if(pool.begin(), pool.end(),
                                                    [](const FParticle& particle) { return particle.active; }));
        }
        return count;
    }

    int CombatEffects::ActiveTracers() const
    {
        return static_cast<int>(
            std::count_if(tracers_.begin(), tracers_.end(), [](const FTracer& tracer) { return tracer.active; }));
    }
}
