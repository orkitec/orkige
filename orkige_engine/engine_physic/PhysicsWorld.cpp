/********************************************************************
	created:	Tuesday 2026/07/07 at 21:00
	filename: 	PhysicsWorld.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_physic/PhysicsWorld.h"

// the entire Jolt surface stays inside this translation unit
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <thread>

namespace
{
	//--- Jolt <-> Ogre math conversions ------------------------------
	inline JPH::Vec3 toJolt(Ogre::Vector3 const & v)
	{
		return JPH::Vec3(v.x, v.y, v.z);
	}
	//---------------------------------------------------------
	inline JPH::Quat toJolt(Ogre::Quaternion const & q)
	{
		return JPH::Quat(q.x, q.y, q.z, q.w).Normalized();
	}
	//---------------------------------------------------------
	inline Ogre::Vector3 toOgre(JPH::Vec3Arg v)
	{
		return Ogre::Vector3(v.GetX(), v.GetY(), v.GetZ());
	}
	//---------------------------------------------------------
	inline Ogre::Quaternion toOgre(JPH::QuatArg q)
	{
		return Ogre::Quaternion(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
	}

	//--- standard two-layer broadphase setup (Jolt HelloWorld) -------
	namespace Layers
	{
		static constexpr JPH::ObjectLayer NON_MOVING = 0;
		static constexpr JPH::ObjectLayer MOVING = 1;
		static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
	}
	namespace BroadPhaseLayers
	{
		static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
		static constexpr JPH::BroadPhaseLayer MOVING(1);
		static constexpr JPH::uint NUM_LAYERS(2);
	}
	//---------------------------------------------------------
	//! object layer -> broadphase layer mapping
	class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
	{
	public:
		virtual JPH::uint GetNumBroadPhaseLayers() const override
		{
			return BroadPhaseLayers::NUM_LAYERS;
		}
		virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
		{
			return layer == Layers::NON_MOVING ?
				BroadPhaseLayers::NON_MOVING : BroadPhaseLayers::MOVING;
		}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		virtual const char * GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
		{
			return layer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
		}
#endif
	};
	//---------------------------------------------------------
	//! which object layers collide with which broadphase layers
	class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		virtual bool ShouldCollide(JPH::ObjectLayer layer1, JPH::BroadPhaseLayer layer2) const override
		{
			switch (layer1)
			{
			case Layers::NON_MOVING:
				return layer2 == BroadPhaseLayers::MOVING;
			case Layers::MOVING:
				return true;
			default:
				return false;
			}
		}
	};
	//---------------------------------------------------------
	//! which object layers collide with each other
	class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
	{
	public:
		virtual bool ShouldCollide(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2) const override
		{
			switch (layer1)
			{
			case Layers::NON_MOVING:
				return layer2 == Layers::MOVING;
			case Layers::MOVING:
				return true;
			default:
				return false;
			}
		}
	};

	//--- Jolt runtime hooks -------------------------------------------
	//---------------------------------------------------------
	void joltTrace(const char * fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		char buffer[1024];
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);
		oDebugMsg("physic", 0, "Jolt: " << buffer);
	}
	//---------------------------------------------------------
#ifdef JPH_ENABLE_ASSERTS
	bool joltAssertFailed(const char * expression, const char * message, const char * file, JPH::uint line)
	{
		oDebugError("physic", 0, Orkige::String("Jolt assert failed: ") + expression +
			" (" + (message ? message : "") + ")");
		return true; // breakpoint
	}
#endif
	//---------------------------------------------------------
	//! register the Jolt runtime once per process; a static build keeps it
	//! registered until exit (repeated PhysicsWorld init/deinit cycles reuse it)
	void registerJoltRuntimeOnce()
	{
		static bool registered = false;
		if (registered)
		{
			return;
		}
		registered = true;
		JPH::RegisterDefaultAllocator();
		JPH::Trace = &joltTrace;
		JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = &joltAssertFailed;)
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();
	}
}

namespace Orkige
{
	const PhysicsWorld::BodyId PhysicsWorld::INVALID_BODY_ID = JPH::BodyID::cInvalidBodyID;
	const float PhysicsWorld::FIXED_TIMESTEP = 1.0f / 60.0f;
	const int PhysicsWorld::MAX_STEPS_PER_UPDATE = 4;
	//---------------------------------------------------------
	//! all Jolt state of a PhysicsWorld
	class PhysicsWorld::PhysicsWorldImpl
	{
		//--- Variables ---------------------------------------------
	public:
		BPLayerInterfaceImpl				mBroadPhaseLayerInterface;	//!< object -> broadphase layer mapping
		ObjectVsBroadPhaseLayerFilterImpl	mObjectVsBroadPhaseFilter;	//!< object vs broadphase layer filter
		ObjectLayerPairFilterImpl			mObjectLayerPairFilter;		//!< object layer pair filter
		JPH::TempAllocatorImpl				mTempAllocator;				//!< per-update scratch memory
		JPH::JobSystemThreadPool			mJobSystem;					//!< worker threads for the simulation
		JPH::PhysicsSystem					mPhysicsSystem;				//!< the Jolt world
		//--- Methods -----------------------------------------------
	public:
		PhysicsWorldImpl() :
			mTempAllocator(10 * 1024 * 1024),
			mJobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
				std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1))
		{
		}
	};
	//---------------------------------------------------------
	IMPL_OSINGLETON(PhysicsWorld)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	PhysicsWorld::PhysicsWorld() : mImpl(NULL), mAccumulator(0.0f), mPaused(false)
	{
	}
	//---------------------------------------------------------
	PhysicsWorld::~PhysicsWorld()
	{
		this->deinit();
	}
	//---------------------------------------------------------
	bool PhysicsWorld::init(unsigned int maxBodies, unsigned int numBodyMutexes,
		unsigned int maxBodyPairs, unsigned int maxContactConstraints)
	{
		if (this->mImpl)
		{
			return true;
		}
		registerJoltRuntimeOnce();
		this->mImpl = new PhysicsWorldImpl();
		this->mImpl->mPhysicsSystem.Init(maxBodies, numBodyMutexes, maxBodyPairs,
			maxContactConstraints, this->mImpl->mBroadPhaseLayerInterface,
			this->mImpl->mObjectVsBroadPhaseFilter, this->mImpl->mObjectLayerPairFilter);
		this->mAccumulator = 0.0f;
		oDebugMsg("physic", 0, "PhysicsWorld initialized (Jolt, maxBodies=" << maxBodies << ")");
		return true;
	}
	//---------------------------------------------------------
	void PhysicsWorld::deinit()
	{
		if (!this->mImpl)
		{
			return;
		}
		delete this->mImpl;
		this->mImpl = NULL;
		this->mAccumulator = 0.0f;
	}
	//---------------------------------------------------------
	void PhysicsWorld::update(float deltaTime)
	{
		if (!this->mImpl || deltaTime <= 0.0f || this->mPaused)
		{
			return;
		}
		this->mAccumulator += deltaTime;
		int steps = 0;
		while (this->mAccumulator >= PhysicsWorld::FIXED_TIMESTEP &&
			steps < PhysicsWorld::MAX_STEPS_PER_UPDATE)
		{
			this->mImpl->mPhysicsSystem.Update(PhysicsWorld::FIXED_TIMESTEP, 1,
				&this->mImpl->mTempAllocator, &this->mImpl->mJobSystem);
			this->mAccumulator -= PhysicsWorld::FIXED_TIMESTEP;
			++steps;
		}
		// after a long stall drop the excess instead of trying to catch up
		this->mAccumulator = std::min(this->mAccumulator, PhysicsWorld::FIXED_TIMESTEP);
	}
	//---------------------------------------------------------
	void PhysicsWorld::setPaused(bool paused)
	{
		this->mPaused = paused;
		// no stale catch-up burst on resume
		this->mAccumulator = 0.0f;
	}
	//---------------------------------------------------------
	void PhysicsWorld::setGravity(Ogre::Vector3 const & gravity)
	{
		oAssert(this->mImpl);
		this->mImpl->mPhysicsSystem.SetGravity(toJolt(gravity));
	}
	//---------------------------------------------------------
	Ogre::Vector3 PhysicsWorld::getGravity() const
	{
		oAssert(this->mImpl);
		return toOgre(this->mImpl->mPhysicsSystem.GetGravity());
	}
	//---------------------------------------------------------
	PhysicsWorld::BodyId PhysicsWorld::createBody(BodyDesc const & desc,
		Ogre::Vector3 const & position, Ogre::Quaternion const & orientation)
	{
		oAssert(this->mImpl);
		JPH::ShapeRefC shape;
		switch (desc.shapeType)
		{
		case PhysicsWorld::ST_SPHERE:
			shape = new JPH::SphereShape(desc.radius);
			break;
		case PhysicsWorld::ST_CAPSULE:
			shape = new JPH::CapsuleShape(desc.halfHeight, desc.radius);
			break;
		case PhysicsWorld::ST_BOX:
		default:
			shape = new JPH::BoxShape(toJolt(desc.halfExtents));
			break;
		}
		const JPH::EMotionType motionType =
			desc.bodyType == PhysicsWorld::BT_STATIC ? JPH::EMotionType::Static :
			desc.bodyType == PhysicsWorld::BT_KINEMATIC ? JPH::EMotionType::Kinematic :
			JPH::EMotionType::Dynamic;
		const JPH::ObjectLayer layer = desc.bodyType == PhysicsWorld::BT_STATIC ?
			Layers::NON_MOVING : Layers::MOVING;
		JPH::BodyCreationSettings settings(shape, toJolt(position),
			toJolt(orientation), motionType, layer);
		settings.mFriction = desc.friction;
		settings.mRestitution = desc.restitution;
		if (desc.bodyType == PhysicsWorld::BT_DYNAMIC)
		{
			if (desc.planar)
			{
				settings.mAllowedDOFs = JPH::EAllowedDOFs::Plane2D;
			}
			if (desc.mass > 0.0f)
			{
				settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
				settings.mMassPropertiesOverride.mMass = desc.mass;
			}
		}
		JPH::BodyInterface & bodyInterface = this->mImpl->mPhysicsSystem.GetBodyInterface();
		JPH::Body * body = bodyInterface.CreateBody(settings);
		if (!body)
		{
			oDebugError("physic", 0, "PhysicsWorld::createBody failed (body pool exhausted?)");
			return PhysicsWorld::INVALID_BODY_ID;
		}
		bodyInterface.AddBody(body->GetID(), desc.bodyType == PhysicsWorld::BT_STATIC ?
			JPH::EActivation::DontActivate : JPH::EActivation::Activate);
		return body->GetID().GetIndexAndSequenceNumber();
	}
	//---------------------------------------------------------
	void PhysicsWorld::destroyBody(BodyId bodyId)
	{
		if (!this->mImpl || bodyId == PhysicsWorld::INVALID_BODY_ID)
		{
			return;
		}
		JPH::BodyInterface & bodyInterface = this->mImpl->mPhysicsSystem.GetBodyInterface();
		const JPH::BodyID id(bodyId);
		bodyInterface.RemoveBody(id);
		bodyInterface.DestroyBody(id);
	}
	//---------------------------------------------------------
	bool PhysicsWorld::getBodyTransform(BodyId bodyId, Ogre::Vector3 & position,
		Ogre::Quaternion & orientation) const
	{
		if (!this->mImpl || bodyId == PhysicsWorld::INVALID_BODY_ID)
		{
			return false;
		}
		JPH::RVec3 joltPosition;
		JPH::Quat joltRotation;
		this->mImpl->mPhysicsSystem.GetBodyInterface().GetPositionAndRotation(
			JPH::BodyID(bodyId), joltPosition, joltRotation);
		position = toOgre(joltPosition);
		orientation = toOgre(joltRotation);
		return true;
	}
	//---------------------------------------------------------
	void PhysicsWorld::setBodyTransform(BodyId bodyId, Ogre::Vector3 const & position,
		Ogre::Quaternion const & orientation)
	{
		oAssert(this->mImpl);
		JPH::BodyInterface & bodyInterface = this->mImpl->mPhysicsSystem.GetBodyInterface();
		const JPH::BodyID id(bodyId);
		// static bodies must not be activated (Jolt asserts); their broadphase
		// entry still moves, so a teleported static body collides at the new pose
		const JPH::EActivation activation =
			bodyInterface.GetMotionType(id) == JPH::EMotionType::Static ?
			JPH::EActivation::DontActivate : JPH::EActivation::Activate;
		bodyInterface.SetPositionAndRotation(id, toJolt(position),
			toJolt(orientation), activation);
	}
	//---------------------------------------------------------
	void PhysicsWorld::moveKinematic(BodyId bodyId, Ogre::Vector3 const & targetPosition,
		Ogre::Quaternion const & targetOrientation, float deltaTime)
	{
		oAssert(this->mImpl);
		this->mImpl->mPhysicsSystem.GetBodyInterface().MoveKinematic(
			JPH::BodyID(bodyId), toJolt(targetPosition), toJolt(targetOrientation),
			std::max(deltaTime, PhysicsWorld::FIXED_TIMESTEP));
	}
	//---------------------------------------------------------
	void PhysicsWorld::setBodyPlanarMode(BodyId bodyId, bool planar)
	{
		if (!this->mImpl || bodyId == PhysicsWorld::INVALID_BODY_ID)
		{
			return;
		}
		{
			JPH::BodyLockWrite lock(this->mImpl->mPhysicsSystem.GetBodyLockInterface(),
				JPH::BodyID(bodyId));
			if (!lock.Succeeded() || !lock.GetBody().IsDynamic())
			{
				return;
			}
			JPH::Body & body = lock.GetBody();
			JPH::MotionProperties * motionProperties = body.GetMotionProperties();
			// rebuild the mass properties from the shape at the current mass so
			// the DOF change keeps mass/inertia intact
			JPH::MassProperties massProperties = body.GetShape()->GetMassProperties();
			const float inverseMass = motionProperties->GetInverseMass();
			if (inverseMass > 0.0f)
			{
				massProperties.ScaleToMass(1.0f / inverseMass);
			}
			motionProperties->SetMassProperties(planar ?
				JPH::EAllowedDOFs::Plane2D : JPH::EAllowedDOFs::All, massProperties);
		}
		this->mImpl->mPhysicsSystem.GetBodyInterface().ActivateBody(JPH::BodyID(bodyId));
	}
	//---------------------------------------------------------
	void PhysicsWorld::setLinearVelocity(BodyId bodyId, Ogre::Vector3 const & velocity)
	{
		oAssert(this->mImpl);
		this->mImpl->mPhysicsSystem.GetBodyInterface().SetLinearVelocity(
			JPH::BodyID(bodyId), toJolt(velocity));
	}
	//---------------------------------------------------------
	Ogre::Vector3 PhysicsWorld::getLinearVelocity(BodyId bodyId) const
	{
		oAssert(this->mImpl);
		return toOgre(this->mImpl->mPhysicsSystem.GetBodyInterface().GetLinearVelocity(
			JPH::BodyID(bodyId)));
	}
	//---------------------------------------------------------
	void PhysicsWorld::setAngularVelocity(BodyId bodyId, Ogre::Vector3 const & velocity)
	{
		oAssert(this->mImpl);
		this->mImpl->mPhysicsSystem.GetBodyInterface().SetAngularVelocity(
			JPH::BodyID(bodyId), toJolt(velocity));
	}
	//---------------------------------------------------------
	Ogre::Vector3 PhysicsWorld::getAngularVelocity(BodyId bodyId) const
	{
		oAssert(this->mImpl);
		return toOgre(this->mImpl->mPhysicsSystem.GetBodyInterface().GetAngularVelocity(
			JPH::BodyID(bodyId)));
	}
	//---------------------------------------------------------
	void PhysicsWorld::applyImpulse(BodyId bodyId, Ogre::Vector3 const & impulse)
	{
		oAssert(this->mImpl);
		this->mImpl->mPhysicsSystem.GetBodyInterface().AddImpulse(
			JPH::BodyID(bodyId), toJolt(impulse));
	}
	//---------------------------------------------------------
	void PhysicsWorld::applyForce(BodyId bodyId, Ogre::Vector3 const & force)
	{
		oAssert(this->mImpl);
		this->mImpl->mPhysicsSystem.GetBodyInterface().AddForce(
			JPH::BodyID(bodyId), toJolt(force));
	}
	//---------------------------------------------------------
	bool PhysicsWorld::isBodyActive(BodyId bodyId) const
	{
		if (!this->mImpl || bodyId == PhysicsWorld::INVALID_BODY_ID)
		{
			return false;
		}
		return this->mImpl->mPhysicsSystem.GetBodyInterface().IsActive(JPH::BodyID(bodyId));
	}
	//---------------------------------------------------------
	bool PhysicsWorld::castRay(Ogre::Vector3 const & origin, Ogre::Vector3 const & direction,
		float maxDistance, Ogre::Vector3 & hitPosition, BodyId & hitBodyId) const
	{
		if (!this->mImpl)
		{
			return false;
		}
		const JPH::RRayCast ray{ toJolt(origin),
			toJolt(direction).NormalizedOr(JPH::Vec3::sZero()) * maxDistance };
		JPH::RayCastResult hit;
		if (!this->mImpl->mPhysicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit))
		{
			return false;
		}
		hitPosition = toOgre(ray.GetPointOnRay(hit.mFraction));
		hitBodyId = hit.mBodyID.GetIndexAndSequenceNumber();
		return true;
	}
	//---------------------------------------------------------
	PhysicsWorld::RayHit::RayHit()
		: hit(false), position(Ogre::Vector3::ZERO),
		bodyId(PhysicsWorld::INVALID_BODY_ID)
	{
	}
	//---------------------------------------------------------
	PhysicsWorld::RayHit PhysicsWorld::castRayHit(Ogre::Vector3 const & origin,
		Ogre::Vector3 const & direction, float maxDistance) const
	{
		RayHit result;
		result.hit = this->castRay(origin, direction, maxDistance,
			result.position, result.bodyId);
		return result;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(PhysicsWorld)
		OSINGLETON()
		OFUNC(update)
		OFUNC(setPaused)
		OFUNC(isPaused)
		OFUNC(setGravity)
		OFUNC(getGravity)
		OFUNC(setBodyTransform)
		OFUNC(setBodyPlanarMode)
		OFUNC(setLinearVelocity)
		OFUNC(getLinearVelocity)
		OFUNC(setAngularVelocity)
		OFUNC(getAngularVelocity)
		OFUNC(applyImpulse)
		OFUNC(applyForce)
		OFUNC(isBodyActive)
		OFUNC(castRayHit)
	OOBJECT_END
}
