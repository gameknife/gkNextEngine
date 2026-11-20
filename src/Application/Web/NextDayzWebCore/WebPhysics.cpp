#include "Engine/Common/CoreMinimal.hpp"
#include "WebPhysics.hpp"
#include "Modules/NextPhysics/JoltCharacter.hpp"

#include <bit>
#include <stdexcept>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace NextDayz
{
    namespace
    {
        constexpr JPH::ObjectLayer rayOnly = 2;

        class FWebBroadPhase final : public JPH::BroadPhaseLayerInterface
        {
        public:
            JPH::uint GetNumBroadPhaseLayers() const override { return 3; }
            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return JPH::BroadPhaseLayer(static_cast<uint8_t>(layer));
            }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "Web map"; }
#endif
        };

        class FWebPairFilter final : public JPH::ObjectLayerPairFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
            {
                return a != rayOnly && b != rayOnly && (a == NextLayers::MOVING || b == NextLayers::MOVING);
            }
        };

        class FWebBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer other) const override
            {
                return pairs_.ShouldCollide(layer, other.GetValue());
            }

        private:
            FWebPairFilter pairs_;
        };

        class FWebRayFilter final : public JPH::BodyFilter
        {
        public:
            bool ShouldCollideLocked(const JPH::Body& body) const override { return (body.GetUserData() & 1u) != 0; }
        };

        struct FJoltRegistration
        {
            FJoltRegistration()
            {
                JPH::RegisterDefaultAllocator();
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            }
            ~FJoltRegistration()
            {
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
            }
        };

        JPH::RefConst<JPH::Shape> MakeMesh(const JPH::VertexList& vertices, const JPH::IndexedTriangleList& triangles)
        {
            JPH::MeshShapeSettings settings(vertices, triangles);
            // Degenerate decorative source meshes can be absent from Jolt
            // without preventing the valid collision geometry from loading.
            const auto result = settings.Create();
            return result.IsValid() ? result.Get() : nullptr;
        }

        struct FMesh
        {
            JPH::VertexList vertices;
            JPH::IndexedTriangleList triangles;
            JPH::RefConst<JPH::Shape> shape;
        };

        JPH::RefConst<JPH::Shape> MakeAffineMesh(const FMesh& mesh, const glm::mat4& transform)
        {
            const glm::mat3 basis(transform);
            JPH::VertexList vertices;
            vertices.reserve(mesh.vertices.size());
            for (const auto& vertex : mesh.vertices)
            {
                const auto p = basis * glm::vec3(vertex.x, vertex.y, vertex.z);
                vertices.emplace_back(p.x, p.y, p.z);
            }
            return MakeMesh(vertices, mesh.triangles);
        }
    }

    struct FWebPhysics::FImpl
    {
        FImpl(size_t meshCount, size_t instanceCount)
            : expectedMeshes(meshCount), expectedInstances(instanceCount), scratch(16 * 1024 * 1024)
        {
            meshes.reserve(meshCount);
            system.Init(static_cast<JPH::uint>(instanceCount * 2 + 32), 0, 1024, 1024, broadPhase, broadFilter, pairFilter);
        }

        void AddBody(const JPH::Shape* shape, const glm::vec3& position, const glm::quat& rotation,
                     JPH::ObjectLayer layer, JPH::EMotionType motion, uint32_t flags, uint32_t node)
        {
            if (!shape) return;
            JPH::BodyCreationSettings settings(shape, JPH::RVec3(position.x, position.y, position.z),
                JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), motion, layer);
            settings.mFriction = 0.5f;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = 1.f;
            settings.mUserData = (static_cast<uint64_t>(node + 1) << 32) | flags;
            const auto id = system.GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
            if (id.IsInvalid()) throw std::runtime_error("Cannot create map collision body");
        }

        size_t expectedMeshes;
        size_t expectedInstances;
        std::vector<FMesh> meshes;
        FWebBroadPhase broadPhase;
        FWebPairFilter pairFilter;
        FWebBroadPhaseFilter broadFilter;
        JPH::TempAllocatorImpl scratch;
        JPH::PhysicsSystem system;
        bool sealed = false;
    };

    FWebPhysics::FWebPhysics(size_t meshCount, size_t instanceCount)
    {
        if (!meshCount || !instanceCount || meshCount > 50000 || instanceCount > 100000)
            throw std::runtime_error("Invalid map collision counts");
        static const FJoltRegistration jolt;
        impl_ = std::make_unique<FImpl>(meshCount, instanceCount);
    }

    FWebPhysics::~FWebPhysics() = default;

    void FWebPhysics::AddMesh(std::span<const float> positions, std::span<const uint32_t> indices)
    {
        if (impl_->meshes.size() >= impl_->expectedMeshes || positions.empty() || indices.empty() ||
            positions.size() % 3 || indices.size() % 3)
            throw std::runtime_error("Invalid collision mesh counts");
        FMesh mesh;
        for (size_t i = 0; i < positions.size(); i += 3)
        {
            const auto v = positions.subspan(i, 3);
            if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]))
                throw std::runtime_error("Invalid map vertex");
            mesh.vertices.emplace_back(v[0], v[1], v[2]);
        }
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const auto t = indices.subspan(i, 3);
            if (std::max({t[0], t[1], t[2]}) >= mesh.vertices.size())
                throw std::runtime_error("Invalid collision mesh index");
            mesh.triangles.emplace_back(t[0], t[1], t[2]);
        }
        mesh.shape = MakeMesh(mesh.vertices, mesh.triangles);
        impl_->meshes.push_back(std::move(mesh));
    }

    void FWebPhysics::AddInstances(std::span<const uint32_t> records)
    {
        constexpr size_t recordWords = 30;
        if (impl_->sealed || impl_->meshes.size() != impl_->expectedMeshes ||
            records.size() != recordWords * impl_->expectedInstances)
            throw std::runtime_error("Invalid map collision instances");
        for (size_t i = 0; i < records.size(); i += recordWords)
        {
            const auto entry = records.subspan(i, recordWords);
            if (entry[0] >= impl_->meshes.size() || !entry[3] || entry[3] > 7)
                throw std::runtime_error("Invalid collision instance");
            const uint32_t flags = entry[3];
            const FMesh& mesh = impl_->meshes[entry[0]];
            if (!mesh.shape) continue;
            std::array<float, 26> values;
            for (size_t j = 0; j < values.size(); ++j)
            {
                values[j] = std::bit_cast<float>(entry[j + 4]);
                if (!std::isfinite(values[j])) throw std::runtime_error("Invalid collision transform");
            }
            const glm::mat4 transform = glm::make_mat4(values.data());
            const glm::vec3 translation = glm::make_vec3(values.data() + 16);
            const glm::quat rotation = glm::normalize(glm::quat(values[22], values[19], values[20], values[21]));
            const glm::vec3 scale = glm::make_vec3(values.data() + 23);
            const glm::mat3 basis(transform);
            glm::mat3 physicsBasis = glm::mat3_cast(rotation);
            for (int axis = 0; axis < 3; ++axis) physicsBasis[axis] *= scale[axis];
            const bool physical = (flags & 6u) != 0;
            const bool shared = physical && glm::length(basis[0] - physicsBasis[0]) < 1e-4f &&
                glm::length(basis[1] - physicsBasis[1]) < 1e-4f && glm::length(basis[2] - physicsBasis[2]) < 1e-4f &&
                glm::length(translation - glm::vec3(transform[3])) < 1e-4f;
            if (physical)
            {
                const auto scaled = mesh.shape->ScaleShape(JPH::Vec3(scale.x, scale.y, scale.z));
                if (!scaled.IsValid()) throw std::runtime_error("Cannot scale map collision mesh");
                const bool moving = (flags & 4u) != 0;
                impl_->AddBody(scaled.Get(), translation, rotation, moving ? NextLayers::MOVING : NextLayers::NON_MOVING,
                    moving ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static, shared ? flags : flags & ~1u, entry[1]);
            }
            if ((flags & 1u) && !shared)
            {
                const auto shape = MakeAffineMesh(mesh, transform);
                impl_->AddBody(shape, glm::vec3(transform[3]), glm::quat(1, 0, 0, 0), rayOnly, JPH::EMotionType::Static, flags, entry[1]);
            }
        }
        impl_->system.OptimizeBroadPhase();
        impl_->meshes.clear();
        impl_->sealed = true;
    }

    std::unique_ptr<INextCharacterControllerBackend> FWebPhysics::CreateCharacter(const FCharacterControllerSettings& settings)
    {
        if (!impl_->sealed) throw std::runtime_error("Map collision is still loading");
        return Modules::Physics::MakeJoltCharacter(impl_->system, impl_->scratch, settings);
    }

    std::optional<FWorldRayHit> FWebPhysics::Raycast(const glm::vec3& origin, const glm::vec3& direction, float distance) const
    {
        const glm::vec3 displacement = direction * distance;
        const JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z),
                               JPH::Vec3(displacement.x, displacement.y, displacement.z));
        JPH::RayCastResult result;
        if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, result, {}, {}, FWebRayFilter{})) return std::nullopt;
        const JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), result.mBodyID);
        if (!lock.Succeeded()) return std::nullopt;
        const auto normal = lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(result.mFraction));
        const float length = distance * result.mFraction;
        return FWorldRayHit{origin + direction * length, glm::vec3(normal.GetX(), normal.GetY(), normal.GetZ()), length, static_cast<uint32_t>(lock.GetBody().GetUserData() >> 32)};
    }
}
