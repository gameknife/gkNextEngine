#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <emscripten/emscripten.h>

#include <cstdio>
#include <memory>

namespace
{
    using namespace JPH;

    // The probe has just one object layer because every body is static.
    class BroadPhaseLayers final : public BroadPhaseLayerInterface
    {
        uint GetNumBroadPhaseLayers() const override { return 1; }
        BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer) const override { return BroadPhaseLayer(0); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "static"; }
#endif
    };

    class BroadPhaseFilter final : public ObjectVsBroadPhaseLayerFilter
    {
        bool ShouldCollide(ObjectLayer, BroadPhaseLayer) const override { return true; }
    };

    class ObjectFilter final : public ObjectLayerPairFilter
    {
        bool ShouldCollide(ObjectLayer, ObjectLayer) const override { return true; }
    };

    struct World
    {
        BroadPhaseLayers layers;
        BroadPhaseFilter broadPhaseFilter;
        ObjectFilter objectFilter;
        TempAllocatorImpl temporary{16 * 1024 * 1024};
        PhysicsSystem physics;
        Ref<CharacterVirtual> character;
    };

    std::unique_ptr<World> world;
}

extern "C"
{
    EMSCRIPTEN_KEEPALIVE int BuildCollision(const float* xyz, uint32_t vertexCount, const uint32_t* indices,
                                           uint32_t triangleCount, float x, float y, float z)
    {
        if (!JPH::Factory::sInstance)
        {
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }

        using namespace JPH;
        VertexList vertices;
        vertices.reserve(vertexCount);
        for (size_t i = 0; i < vertexCount * 3ull; i += 3)
        {
            vertices.emplace_back(xyz[i], xyz[i + 1], xyz[i + 2]);
        }

        IndexedTriangleList triangles;
        triangles.reserve(triangleCount);
        for (size_t i = 0; i < triangleCount * 3ull; i += 3)
        {
            if (indices[i] >= vertexCount || indices[i + 1] >= vertexCount || indices[i + 2] >= vertexCount)
            {
                return 0;
            }
            triangles.emplace_back(indices[i], indices[i + 1], indices[i + 2], 0);
        }
        const auto mesh = MeshShapeSettings(std::move(vertices), std::move(triangles)).Create();
        if (mesh.HasError())
        {
            std::fprintf(stderr, "%s\n", mesh.GetError().c_str());
            return 0;
        }

        auto next = std::make_unique<World>();
        next->physics.Init(64, 0, 64, 64, next->layers, next->broadPhaseFilter, next->objectFilter);
        BodyCreationSettings ground(mesh.Get(), RVec3::sZero(), Quat::sIdentity(), EMotionType::Static, 0);
        auto& bodies = next->physics.GetBodyInterface();
        if (bodies.CreateAndAddBody(ground, EActivation::DontActivate).IsInvalid())
        {
            return 0;
        }
        next->physics.OptimizeBroadPhase();

        constexpr float radius = 0.3f;
        constexpr float halfCylinder = 0.55f;
        CharacterVirtualSettings character;
        character.mShape = new RotatedTranslatedShape(Vec3(0, halfCylinder + radius, 0), Quat::sIdentity(),
                                                       new CapsuleShape(halfCylinder, radius));
        character.mSupportingVolume = Plane(Vec3::sAxisY(), -radius);
        next->character = new CharacterVirtual(&character, RVec3(x, y, z), Quat::sIdentity(), 0, &next->physics);
        world = std::move(next);
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE void ResetCapsule(float x, float y, float z)
    {
        world->character->SetPosition(JPH::RVec3(x, y, z));
        world->character->SetLinearVelocity(JPH::Vec3::sZero());
    }

    EMSCRIPTEN_KEEPALIVE void StepCapsule(float dx, float dz, float dt, float* position)
    {
        using namespace JPH;
        auto& character = *world->character;
        const Vec3 gravity = world->physics.GetGravity();
        const float verticalSpeed = character.IsSupported() ? 0.0f : character.GetLinearVelocity().GetY();
        character.SetLinearVelocity(Vec3(dx, verticalSpeed, dz) + dt * gravity);
        character.ExtendedUpdate(dt, gravity, {}, {}, {}, {}, {}, world->temporary);
        character.GetPosition().StoreFloat3(reinterpret_cast<Float3*>(position));
    }
}
