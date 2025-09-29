﻿#include "NextPhysics.h"

// The Jolt headers don't include Jolt.h. Always include Jolt.h before including any other Jolt header.
// You can use Jolt.h in your precompiled header to speed up compilation.
#include <Jolt/Jolt.h>

// Jolt includes
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/PhysicsMaterialSimple.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
// STL includes
#include <iostream>
#include <cstdarg>
#include <thread>
#include <glm/ext.hpp>

#include "Engine.hpp"

// Disable common warnings triggered by Jolt, you can use JPH_SUPPRESS_WARNING_PUSH / JPH_SUPPRESS_WARNING_POP to store and restore the warning state
JPH_SUPPRESS_WARNINGS

// All Jolt symbols are in the JPH namespace
using namespace JPH;

// If you want your code to compile using single or double precision write 0.0_r to get a Real value that compiles to double or float depending if JPH_DOUBLE_PRECISION is set or not.
using namespace JPH::literals;

// We're also using STL classes in this example
using namespace std;

// Callback for traces, connect this to your own trace function if you have one
static void TraceImpl(const char *inFMT, ...)
{
	// Format the message
	va_list list;
	va_start(list, inFMT);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), inFMT, list);
	va_end(list);

	// Print to the TTY
	cout << buffer << endl;
}

#ifdef JPH_ENABLE_ASSERTS

// Callback for asserts, connect this to your own assert handler if you have one
static bool AssertFailedImpl(const char *inExpression, const char *inMessage, const char *inFile, uint inLine)
{
	// Print to the TTY
	cout << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr? inMessage : "") << endl;

	// Breakpoint
	return true;
};

#endif // JPH_ENABLE_ASSERTS

// Layer that objects can be in, determines which other objects it can collide with
// Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
// layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics simulation
// but only if you do collision testing).

/// Class that determines if two object layers can collide
class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter
{
public:
	virtual bool					ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override
	{
		switch (inObject1)
		{
		case Layers::NON_MOVING:
			return inObject2 == Layers::MOVING; // Non moving only collides with moving
		case Layers::MOVING:
			return true; // Moving collides with everything
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};

// Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
// a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
// You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
// many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
// your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
namespace BroadPhaseLayers
{
	static constexpr BroadPhaseLayer nonMoving(0);
	static constexpr BroadPhaseLayer moving(1);
	static constexpr uint numLayers(2);
};

// BroadPhaseLayerInterface implementation
// This defines a mapping between object and broadphase layers.
class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface
{
public:
									BPLayerInterfaceImpl()
	{
		// Create a mapping table from object to broad phase layer
		mObjectToBroadPhase_[Layers::NON_MOVING] = BroadPhaseLayers::nonMoving;
		mObjectToBroadPhase_[Layers::MOVING] = BroadPhaseLayers::moving;
	}

	virtual uint					GetNumBroadPhaseLayers() const override
	{
		return BroadPhaseLayers::numLayers;
	}

	virtual BroadPhaseLayer			GetBroadPhaseLayer(ObjectLayer inLayer) const override
	{
		JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
		return mObjectToBroadPhase_[inLayer];
	}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	virtual const char *			GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override
	{
		switch ((BroadPhaseLayer::Type)inLayer)
		{
		case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:	return "NON_MOVING";
		case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:		return "MOVING";
		default:													JPH_ASSERT(false); return "INVALID";
		}
	}
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

private:
	BroadPhaseLayer					mObjectToBroadPhase_[Layers::NUM_LAYERS];
};

/// Class that determines if an object layer can collide with a broadphase layer
class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter
{
public:
	virtual bool				ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override
	{
		switch (inLayer1)
		{
		case Layers::NON_MOVING:
			return inLayer2 == BroadPhaseLayers::moving;
		case Layers::MOVING:
			return true;
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};

// An example contact listener
class MyContactListener : public ContactListener
{
public:
	// See: ContactListener
	virtual ValidateResult	OnContactValidate(const Body &inBody1, const Body &inBody2, RVec3Arg inBaseOffset, const CollideShapeResult &inCollisionResult) override
	{
		//cout << "Contact validate callback" << endl;

		// Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
		return ValidateResult::AcceptAllContactsForThisBodyPair;
	}

	virtual void			OnContactAdded(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings) override
	{
		//cout << "A contact was added" << endl;
	}

	virtual void			OnContactPersisted(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings) override
	{
		//cout << "A contact was persisted" << endl;
	}

	virtual void			OnContactRemoved(const SubShapeIDPair &inSubShapePair) override
	{
		//cout << "A contact was removed" << endl;
	}
};

// An example activation listener
class MyBodyActivationListener : public BodyActivationListener
{
public:
	virtual void		OnBodyActivated(const BodyID &inBodyID, uint64 inBodyUserData) override
	{
		//cout << "A body got activated" << endl;
	}

	virtual void		OnBodyDeactivated(const BodyID &inBodyID, uint64 inBodyUserData) override
	{
		//cout << "A body went to sleep" << endl;
	}
};

struct FNextPhysicsContext
{
	FNextPhysicsContext():
		tempAllocator(10 * 1024 * 1024),
		jobSystem(cMaxPhysicsJobs, cMaxPhysicsBarriers, thread::hardware_concurrency() - 1)
	{
		// This is the max amount of rigid bodies that you can add to the physics system. If you try to add more you'll get an error.
		// Note: This value is low because this is a simple test. For a real project use something in the order of 65536.
		const uint cMaxBodies = 65535;//1024;

		// This determines how many mutexes to allocate to protect rigid bodies from concurrent access. Set it to 0 for the default settings.
		const uint cNumBodyMutexes = 0;

		// This is the max amount of body pairs that can be queued at any time (the broad phase will detect overlapping
		// body pairs based on their bounding boxes and will insert them into a queue for the narrowphase). If you make this buffer
		// too small the queue will fill up and the broad phase jobs will start to do narrow phase work. This is slightly less efficient.
		// Note: This value is low because this is a simple test. For a real project use something in the order of 65536.
		const uint cMaxBodyPairs = 65535;//1024;

		// This is the maximum size of the contact constraint buffer. If more contacts (collisions between bodies) are detected than this
		// number then these contacts will be ignored and bodies will start interpenetrating / fall through the world.
		// Note: This value is low because this is a simple test. For a real project use something in the order of 10240.
		const uint cMaxContactConstraints = 10240;//1024;
		
		physicsSystem.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, broadPhaseLayerInterface, objectVsBroadphaseLayerFilter, objectVsObjectLayerFilter);

		// PhysicsSettings settings;
		// settings.mPointVelocitySleepThreshold = 0.01f;
		// settings.mTimeBeforeSleep = 0.1f;
		// physics_system.SetPhysicsSettings(settings);
		
		
		physicsSystem.SetBodyActivationListener(&bodyActivationListener);
		physicsSystem.SetContactListener(&contactListener);
	}

	~FNextPhysicsContext()
	{

	}
	// We need a temp allocator for temporary allocations during the physics update. We're
	// pre-allocating 10 MB to avoid having to do allocations during the physics update.
	// B.t.w. 10 MB is way too much for this example but it is a typical value you can use.
	// If you don't want to pre-allocate you can also use TempAllocatorMalloc to fall back to
	// malloc / free.
	TempAllocatorImpl tempAllocator;

	// We need a job system that will execute physics jobs on multiple threads. Typically
	// you would implement the JobSystem interface yourself and let Jolt Physics run on top
	// of your own job scheduler. JobSystemThreadPool is an example implementation.
	JobSystemThreadPool jobSystem;

	// Create mapping table from object layer to broadphase layer
	// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
	// Also have a look at BroadPhaseLayerInterfaceTable or BroadPhaseLayerInterfaceMask for a simpler interface.
	BPLayerInterfaceImpl broadPhaseLayerInterface;

	// Create class that filters object vs broadphase layers
	// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
	// Also have a look at ObjectVsBroadPhaseLayerFilterTable or ObjectVsBroadPhaseLayerFilterMask for a simpler interface.
	ObjectVsBroadPhaseLayerFilterImpl objectVsBroadphaseLayerFilter;

	// Create class that filters object vs object layers
	// Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
	// Also have a look at ObjectLayerPairFilterTable or ObjectLayerPairFilterMask for a simpler interface.
	ObjectLayerPairFilterImpl objectVsObjectLayerFilter;
	
	PhysicsSystem physicsSystem;

	// A body activation listener gets notified when bodies activate and go to sleep
	// Note that this is called from a job so whatever you do here needs to be thread safe.
	// Registering one is entirely optional.
	MyBodyActivationListener bodyActivationListener;

	// A contact listener gets notified when bodies (are about to) collide, and when they separate again.
	// Note that this is called from a job so whatever you do here needs to be thread safe.
	// Registering one is entirely optional.
	MyContactListener contactListener;
};

NextPhysics::NextPhysics()
{
    
}

NextPhysics::~NextPhysics()
{
    
}

void NextPhysics::Start()
{
	// Register allocation hook. In this example we'll just let Jolt use malloc / free but you can override these if you want (see Memory.h).
	// This needs to be done before any other Jolt function is called.
	RegisterDefaultAllocator();

	// Install trace and assert callbacks
	Trace = TraceImpl;
	JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)

	// Create a factory, this class is responsible for creating instances of classes based on their name or hash and is mainly used for deserialization of saved data.
	// It is not directly used in this example but still required.
	Factory::sInstance = new Factory();

	// Register all physics types with the factory and install their collision handlers with the CollisionDispatch class.
	// If you have your own custom shape types you probably need to register their handlers with the CollisionDispatch before calling this function.
	// If you implement your own default material (PhysicsMaterial::sDefault) make sure to initialize it before this function or else this function will create one for you.
	RegisterTypes();

	context_.reset(new FNextPhysicsContext());

	//context_->physics_system.SetGravity(Vec3(0,-9.8f,0));
	// The main way to interact with the bodies in the physics system is through the body interface. There is a locking and a non-locking
	// variant of this. We're going to use the locking version (even though we're not planning to access bodies from multiple threads)
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();

	//CreateBody(ENextBodyShape::Box, glm::vec3(0.0f, -1.0f, 0.0f));
	//CreateBody(ENextBodyShape::Sphere, glm::vec3(0.0f, 5.0f, 0.0f));

	
	// We simulate the physics world in discrete time steps. 60 Hz is a good rate to update the physics system.
	

	// Optional step: Before starting the physics simulation you can optimize the broad phase. This improves collision detection performance (it's pointless here because we only have 2 bodies).
	// You should definitely not call this every frame or when e.g. streaming in a new level section as it is an expensive operation.
	// Instead insert all new objects in batches instead of 1 at a time to keep the broad phase efficient.
	
}

void NextPhysics::Tick(double deltaSeconds)
{
	TimeElapsed += deltaSeconds;
	const float timeOffset = static_cast<float>( TimeElapsed - TimeSimulated );
	const float cDeltaTime = 1.0f / 60.0f;

	float shouldTick = timeOffset / cDeltaTime;

	if (shouldTick < 1.0f)
	{
		return;
	}
	
	// If you take larger steps than 1 / 60th of a second you need to do multiple collision steps in order to keep the simulation stable. Do 1 collision step per 1 / 60th of a second (round up).
	const int cCollisionSteps = glm::min(4, glm::max(1, static_cast<int>(glm::floor(timeOffset / cDeltaTime))));

	TimeSimulated += cDeltaTime * cCollisionSteps;

	// Update bodies
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	
	for (auto& body : bodies_)
	{
		if (body.second.motionType == EMotionType::Dynamic)
		{
			RVec3 pos = bodyInterface.GetPosition(body.first);
			RVec3 vel = bodyInterface.GetLinearVelocity(body.first);
			body.second.position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
			body.second.velocity = glm::vec3(vel.GetX(), vel.GetY(), vel.GetZ());

			if (vel.Length() > 0.001f)
			{
				NextEngine::GetInstance()->GetScene().MarkDirty(); // if changed, then dirty
			}
		}
	}

	// Step the world
	context_->physicsSystem.Update(cDeltaTime, cCollisionSteps, &context_->tempAllocator, &context_->jobSystem);
}

void NextPhysics::Stop()
{
	// OnSceneDestroyed();
	
	// Unregisters all types with the factory and cleans up the default material
	UnregisterTypes();

	// Destroy the factory
	delete Factory::sInstance;
	Factory::sInstance = nullptr;

	context_.reset();
}

JPH::BodyID NextPhysics::AddBodyInternal(FNextPhysicsBody& body, bool optimizeBroadPhase)
{
	bodies_[body.bodyID] = body;
	if (optimizeBroadPhase) context_->physicsSystem.OptimizeBroadPhase();
	return body.bodyID;
}

JPH::BodyID NextPhysics::CreateSphereBody(glm::vec3 position, float radius, JPH::EMotionType motionType)
{
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	BodyID bodyId(-1);

	// Now create a dynamic body to bounce on the floor
	// Note that this uses the shorthand version of creating and adding a body to the world
	BodyCreationSettings sphereSettings(new SphereShape(radius), RVec3(position.x, position.y, position.z), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
	sphereSettings.mFriction = 0.5f;
	sphereSettings.mInertiaMultiplier = 2.0f;
	//sphere_settings.mRestitution = 0.05f;
	sphereSettings.mMotionQuality = EMotionQuality::LinearCast;
	bodyId = bodyInterface.CreateAndAddBody(sphereSettings, EActivation::Activate);

	// Now you can interact with the dynamic body, in this case we're going to give it a velocity.
	// (note that if we had used CreateBody then we could have set the velocity straight on the body before adding it to the physics system)
	//body_interface.SetLinearVelocity(body_id, Vec3(0.0f, -5.0f, 0.0f));
	FNextPhysicsBody body { position, glm::vec3(0.0f, 0.0f, 0.0f), ENextBodyShape::Sphere, bodyId, EMotionType::Dynamic };
	return AddBodyInternal(body, true);
}

JPH::BodyID NextPhysics::CreateBoxBody(glm::vec3 position, glm::vec3 extent, JPH::EMotionType motionType)
{
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	BodyID bodyId(-1);
	
	// Next we can create a rigid body to serve as the floor, we make a large box
	// Create the settings for the collision volume (the shape).
	// Note that for simple shapes (like boxes) you can also directly construct a BoxShape.
	BoxShapeSettings floorShapeSettings(Vec3(extent.x, extent.y, extent.z));
	floorShapeSettings.SetEmbedded(); // A ref counted object on the stack (base class RefTarget) should be marked as such to prevent it from being freed when its reference count goes to 0.

	// Create the shape
	ShapeSettings::ShapeResult floorShapeResult = floorShapeSettings.Create();
	ShapeRefC floorShape = floorShapeResult.Get(); // We don't expect an error here, but you can check floor_shape_result for HasError() / GetError()

	// Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
	BodyCreationSettings floorSettings(floorShape, RVec3(position.x, position.y, position.z), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
	//floor_settings.mRestitution = 0.05f;
	floorSettings.mFriction = 0.5f;
	// Create the actual rigid body
	bodyId = bodyInterface.CreateAndAddBody(floorSettings, EActivation::DontActivate);

	FNextPhysicsBody body { position, glm::vec3(0.0f, 0.0f, 0.0f), ENextBodyShape::Box, bodyId, EMotionType::Static };
	return AddBodyInternal(body, true);
}

JPH::BodyID NextPhysics::CreateMeshBody(RefConst<MeshShapeSettings> meshShapeSettings, glm::vec3 position, glm::quat rotation, glm::vec3 scale, EMotionType motionType, ObjectLayer layer)
{
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	BodyID bodyId(-1);
	
	const float epsilon = 0.001f;
	bool isUniformScale = (glm::abs(scale.x - 1.0f) < epsilon && 
						  glm::abs(scale.y - 1.0f) < epsilon && 
						  glm::abs(scale.z - 1.0f) < epsilon);

	BodyCreationSettings bodyCreation = isUniformScale ? BodyCreationSettings(meshShapeSettings,
	Vec3(position.x, position.y, position.z),Quat(rotation.x, rotation.y, rotation.z, rotation.w),
	motionType, layer): BodyCreationSettings(new ScaledShapeSettings(meshShapeSettings, Vec3(scale.x, scale.y, scale.z)),
		Vec3(position.x, position.y, position.z), Quat(rotation.x, rotation.y, rotation.z, rotation.w),
		motionType, layer);

	//bodyCreation.mRestitution = 0.05f;
	bodyCreation.mFriction = 0.5f;
	bodyCreation.mOverrideMassProperties = EOverrideMassProperties::MassAndInertiaProvided;
	bodyCreation.mMassPropertiesOverride = MassProperties(1.0);
	
	bodyId = bodyInterface.CreateAndAddBody(bodyCreation, EActivation::Activate);

	FNextPhysicsBody body { glm::vec3(0,0,0), glm::vec3(0.0f, 0.0f, 0.0f), ENextBodyShape::Sphere, bodyId, motionType };
	return AddBodyInternal(body, false);
}

JPH::BodyID NextPhysics::CreatePlaneBody(glm::vec3 position, glm::vec3 normal, JPH::EMotionType motionType)
{
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	BodyID bodyId(-1);

	// Next we can create a rigid body to serve as the floor, we make a large box
	// Create the settings for the collision volume (the shape).
	// Note that for simple shapes (like boxes) you can also directly construct a BoxShape.
	PlaneShapeSettings planeShapeSettings(Plane::sFromPointAndNormal(Vec3(position.x, position.y, position.z), Vec3(normal.x, normal.y, normal.z)));
	planeShapeSettings.SetEmbedded(); // A ref counted object on the stack (base class RefTarget) should be marked as such to prevent it from being freed when its reference count goes to 0.

	// Create the shape
	ShapeSettings::ShapeResult floorShapeResult = planeShapeSettings.Create();
	ShapeRefC floorShape = floorShapeResult.Get(); // We don't expect an error here, but you can check floor_shape_result for HasError() / GetError()

	// Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
	BodyCreationSettings floorSettings(floorShape, RVec3(0,0,0), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
	//floor_settings.mRestitution = 0.05f;
	floorSettings.mFriction = 0.5f;
	// Create the actual rigid body
	bodyId = bodyInterface.CreateAndAddBody(floorSettings, EActivation::DontActivate);

	FNextPhysicsBody body { position, glm::vec3(0.0f, 0.0f, 0.0f), ENextBodyShape::Box, bodyId, EMotionType::Static };
	return AddBodyInternal(body, true);
}

MeshShapeSettings* NextPhysics::CreateMeshShape(Assets::Model& model)
{
	VertexList inVertices;
	IndexedTriangleList inTriangles;
	for ( auto& vertex : model.CPUVertices() )
	{
		inVertices.push_back( { vertex.Position.x, vertex.Position.y, vertex.Position.z} );
	}

	for ( int i = 0; i < model.CPUIndices().size(); i += 3 )
	{
		inTriangles.push_back( { model.CPUIndices()[i], model.CPUIndices()[i+1], model.CPUIndices()[i+2], 0 } );
	}

	PhysicsMaterialList materials;
	materials.push_back(new PhysicsMaterialSimple("Material " + ConvertToString(0), Color::sGetDistinctColor(0)));
	
	return new MeshShapeSettings(inVertices, inTriangles, materials);
}

void NextPhysics::AddForceToBody(JPH::BodyID bodyID, const glm::vec3& force)
{
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();

	bodyInterface.AddForce(bodyID, Vec3(force.x, force.y, force.z), EActivation::Activate); // Activate the body if it is sleeping
}

void NextPhysics::MoveKinematicBody(JPH::BodyID bodyID, const glm::vec3& position, const glm::quat& rotation, float deltaSeconds)
{
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	bodyInterface.MoveKinematic(bodyID, RVec3(position.x, position.y, position.z), Quat(rotation.x, rotation.y, rotation.z, rotation.w), deltaSeconds);
}

FNextPhysicsBody* NextPhysics::GetBody(JPH::BodyID bodyID)
{
	if ( bodies_.contains(bodyID) )
	{
		return &(bodies_[bodyID]);
	}
	return nullptr;
}

void NextPhysics::OnSceneStarted()
{
	TimeElapsed = 0;
	TimeSimulated = 0;
}

void NextPhysics::OnSceneDestroyed()
{
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	
	for (auto& body : bodies_)
	{
		if (body.first.IsInvalid()) continue;
		bodyInterface.RemoveBody(body.first);
		bodyInterface.DestroyBody(body.first);
	}
	
	bodies_.clear();
}
