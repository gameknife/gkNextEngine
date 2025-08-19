#pragma once

#include <memory>
#include <glm/glm.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/MotionType.h>

#include "Vulkan/Vulkan.hpp"

struct FNextPhysicsContext;

namespace Assets
{
    class Model;
}

namespace JPH
{
    class MeshShapeSettings;
}

enum class ENextBodyShape
{
    Box,
    Sphere,
    Capsule,
    Cylinder,
    Cone,
    ConvexHull,
    ConvexDecomposition,
    HeightField,
    Mesh,
    SoftBody
};

struct FNextPhysicsBody
{
    glm::vec3 position;
    glm::vec3 velocity;
    ENextBodyShape shape;
    JPH::BodyID bodyID;
};

class NextPhysics final
{
public:
    VULKAN_NON_COPIABLE(NextPhysics)

    NextPhysics();
    ~NextPhysics();

    void Start();
    void Tick(double DeltaSeconds);
    void Stop();
    
    JPH::BodyID CreateSphereBody(glm::vec3 position, float radius, JPH::EMotionType motionType);
    JPH::BodyID CreatePlaneBody(glm::vec3 position, glm::vec3 extent, JPH::EMotionType motionType);
    JPH::BodyID CreateMeshBody(JPH::MeshShapeSettings* meshShapeSettings, glm::vec3 position, glm::quat rotation, glm::vec3 scale);
    JPH::MeshShapeSettings* CreateMeshShape(Assets::Model& model);

    void AddForceToBody(JPH::BodyID bodyID, const glm::vec3& force);

    FNextPhysicsBody* GetBody(JPH::BodyID bodyID);

    void OnSceneStarted();
    void OnSceneDestroyed();
private:

    JPH::BodyID AddBodyInternal(FNextPhysicsBody& body);
    
    std::unique_ptr<FNextPhysicsContext> context_;
    std::unordered_map<JPH::BodyID, FNextPhysicsBody> bodies_;

    double TimeElapsed {};
    double TimeSimulated {};
};

