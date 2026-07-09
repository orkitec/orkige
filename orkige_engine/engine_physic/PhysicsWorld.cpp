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
#include <Jolt/Physics/Collision/ContactListener.h>

#include "core_project/Project.h"
#include "core_serialization/XMLArchive.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

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

	//--- data-driven object layers ----------------------------------
	// The ObjectLayer PACKS two independent axes: the game-layer index (from
	// LayerConfig, drives the collision matrix) in the high bits and one motion
	// bit (static vs. moving, bodyType-derived) in the low bit. This keeps the
	// broadphase at the classic 2 motion buckets while giving the narrow phase
	// N arbitrary game layers - the two axes stay decoupled (a game layer may
	// carry both static and moving bodies). ObjectLayer is a 16-bit value, so
	// this leaves room for 2^15 game layers - far more than any project needs.
	namespace BroadPhaseLayers
	{
		static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
		static constexpr JPH::BroadPhaseLayer MOVING(1);
		static constexpr JPH::uint NUM_LAYERS(2);
	}
	//---------------------------------------------------------
	inline JPH::ObjectLayer makeObjectLayer(int gameLayer, bool moving)
	{
		return static_cast<JPH::ObjectLayer>(
			(static_cast<unsigned>(gameLayer) << 1) | (moving ? 1u : 0u));
	}
	inline int gameLayerOf(JPH::ObjectLayer layer)
	{
		return static_cast<int>(layer >> 1);
	}
	inline bool movingOf(JPH::ObjectLayer layer)
	{
		return (layer & 1) != 0;
	}
	//---------------------------------------------------------
	//! object layer -> broadphase (motion) layer mapping: the low bit is the
	//! static/moving flag, decoupled from the game-layer index in the high bits
	class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
	{
	public:
		virtual JPH::uint GetNumBroadPhaseLayers() const override
		{
			return BroadPhaseLayers::NUM_LAYERS;
		}
		virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
		{
			return movingOf(layer) ?
				BroadPhaseLayers::MOVING : BroadPhaseLayers::NON_MOVING;
		}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		virtual const char * GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
		{
			return layer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
		}
#endif
	};
	//---------------------------------------------------------
	//! coarse broadphase cull (unchanged semantics): a moving object may hit
	//! any bucket, a static object only the MOVING bucket - static/static pairs
	//! never simulate. The game-layer MATRIX is applied in the narrow phase.
	class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		virtual bool ShouldCollide(JPH::ObjectLayer layer1, JPH::BroadPhaseLayer layer2) const override
		{
			if (movingOf(layer1))
			{
				return true;
			}
			return layer2 == BroadPhaseLayers::MOVING;
		}
	};
	//---------------------------------------------------------
	//! which object layers collide with each other - the data-driven narrow
	//! phase: decode the game-layer index from each ObjectLayer and read the
	//! symmetric collision matrix (Jolt calls this both (A,B) and (B,A), which
	//! the enforced symmetry answers consistently)
	class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
	{
	public:
		Orkige::PhysicsWorld::LayerConfig const * mConfig = nullptr;
		virtual bool ShouldCollide(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2) const override
		{
			if (!mConfig)
			{
				return true;
			}
			return mConfig->collides(gameLayerOf(layer1), gameLayerOf(layer2));
		}
	};

	//--- contact listener (worker-thread SAFE) ----------------------
	// THE threading contract: Jolt calls OnContactAdded /
	// OnContactRemoved on WORKER threads, concurrently, DURING
	// PhysicsSystem::Update, while body locks are held. Touching GameObjects
	// or the Lua state from here is a data race / deadlock. So the callbacks do
	// the MINIMUM: push the raw {BodyID pair, began} into a MUTEX-GUARDED queue
	// and return. The main thread drains + coalesces + dispatches AFTER Update
	// returns (PhysicsWorld::update, drainContactQueue). OnContactAdded fires
	// per sub-step; the main-thread drain dedupes to once per pair per frame.
	// OnContactRemoved carries only BodyIDs (no Body refs) and can fire while a
	// body is being destroyed - the drain tolerates unresolvable ids.
	class ContactListenerImpl final : public JPH::ContactListener
	{
	public:
		struct QueuedContact
		{
			JPH::BodyID	a;
			JPH::BodyID	b;
			bool		began;
		};
		std::mutex					mMutex;		//!< guards mQueue (worker threads push, main thread drains)
		std::vector<QueuedContact>	mQueue;		//!< raw contacts, drained each update()

		virtual void OnContactAdded(const JPH::Body & inBody1,
			const JPH::Body & inBody2, const JPH::ContactManifold & /*inManifold*/,
			JPH::ContactSettings & /*ioSettings*/) override
		{
			std::lock_guard<std::mutex> lock(this->mMutex);
			this->mQueue.push_back({ inBody1.GetID(), inBody2.GetID(), true });
		}
		virtual void OnContactRemoved(
			const JPH::SubShapeIDPair & inSubShapePair) override
		{
			// only BodyIDs here - the bodies may already be mid-destruction
			std::lock_guard<std::mutex> lock(this->mMutex);
			this->mQueue.push_back({ inSubShapePair.GetBody1ID(),
				inSubShapePair.GetBody2ID(), false });
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
		PhysicsWorld::LayerConfig			mLayerConfig;				//!< the game-layer set + collision matrix (owned copy)
		BPLayerInterfaceImpl				mBroadPhaseLayerInterface;	//!< object -> broadphase layer mapping
		ObjectVsBroadPhaseLayerFilterImpl	mObjectVsBroadPhaseFilter;	//!< object vs broadphase layer filter
		ObjectLayerPairFilterImpl			mObjectLayerPairFilter;		//!< object layer pair filter (reads mLayerConfig)
		JPH::TempAllocatorImpl				mTempAllocator;				//!< per-update scratch memory
		JPH::JobSystemThreadPool			mJobSystem;					//!< worker threads for the simulation
		JPH::PhysicsSystem					mPhysicsSystem;				//!< the Jolt world
		ContactListenerImpl					mContactListener;			//!< worker-thread contact queue (@see drainContactQueue)
		//! body -> opaque user tag (the GameObject bridge stores its
		//! RigidBodyComponent here). Touched ONLY on the main thread
		//! (createBody/destroyBody/setBodyUserData/the drain) - the sole
		//! cross-thread structure is the listener's mutex-guarded queue.
		std::unordered_map<PhysicsWorld::BodyId, PhysicsWorld::BodyUserData> mBodyUserData;
		//--- Methods -----------------------------------------------
	public:
		explicit PhysicsWorldImpl(PhysicsWorld::LayerConfig const & layerConfig) :
			mLayerConfig(layerConfig),
			mTempAllocator(10 * 1024 * 1024),
			mJobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
				std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1))
		{
			// the pair filter reads the owned config copy (stable for the Jolt
			// system's lifetime - Jolt keeps the interface by reference)
			this->mObjectLayerPairFilter.mConfig = &this->mLayerConfig;
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
		this->mImpl = new PhysicsWorldImpl(this->mLayerConfig);
		this->mImpl->mPhysicsSystem.Init(maxBodies, numBodyMutexes, maxBodyPairs,
			maxContactConstraints, this->mImpl->mBroadPhaseLayerInterface,
			this->mImpl->mObjectVsBroadPhaseFilter, this->mImpl->mObjectLayerPairFilter);
		// wire the contact listener AFTER Init: sensors and contact events fire
		// through it (the queue is drained on the main thread in update())
		this->mImpl->mPhysicsSystem.SetContactListener(&this->mImpl->mContactListener);
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
		this->mFrameContacts.clear();
	}
	//---------------------------------------------------------
	void PhysicsWorld::setLayerConfig(LayerConfig const & config)
	{
		if (this->mImpl)
		{
			// the Jolt filters were built at init() from the previous config;
			// v1 has no mid-session layer changes (a re-init is heavy)
			oDebugWarning(false, "PhysicsWorld::setLayerConfig ignored - already "
				"initialized (layers must be configured before init)");
			return;
		}
		this->mLayerConfig = config;
		// a config must always answer at least the Default layer
		if (this->mLayerConfig.getLayerCount() == 0)
		{
			this->mLayerConfig.loadDefaults();
		}
		this->mLayerConfig.symmetrize();
	}
	//---------------------------------------------------------
	void PhysicsWorld::update(float deltaTime)
	{
		// last frame's contacts expire the moment a new update() begins - so a
		// PAUSED frame (early return below) honestly presents NO contacts, and a
		// consumer always sees exactly this call's contacts
		this->mFrameContacts.clear();
		if (!this->mImpl || deltaTime <= 0.0f || this->mPaused)
		{
			// PAUSED SEMANTICS (documented loudly): while paused Update() never
			// runs, so NO contact callbacks fire and NO triggers fire. A body
			// teleported into a sensor while paused generates a contact only on
			// the next UNPAUSED step (or never if teleported through and out).
			// This is correct for the roller "move the world" mode - wins happen
			// during play, not during the paused tile slide.
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
		// MAIN-THREAD drain: the worker threads finished inside Update() above,
		// so the queue is quiescent now - coalesce it into mFrameContacts
		this->drainContactQueue();
	}
	//---------------------------------------------------------
	void PhysicsWorld::drainContactQueue()
	{
		oAssert(this->mImpl);
		// take the queue under the listener's lock (workers are done, but the
		// lock is the contract) and process it lock-free afterwards
		std::vector<ContactListenerImpl::QueuedContact> queued;
		{
			std::lock_guard<std::mutex> lock(this->mImpl->mContactListener.mMutex);
			queued.swap(this->mImpl->mContactListener.mQueue);
		}
		if (queued.empty())
		{
			return;
		}
		// COALESCE per frame: OnContactAdded fires once per sub-step, so the same
		// pair can appear many times - dedupe on the unordered (lo,hi) id pair
		// plus the began/ended kind so onContactBegin fires exactly once per pair
		// per frame (and a begin+end within one frame both survive).
		std::set<std::tuple<BodyId, BodyId, bool> > seen;
		this->mFrameContacts.reserve(queued.size());
		for (ContactListenerImpl::QueuedContact const & contact : queued)
		{
			const BodyId ia = contact.a.GetIndexAndSequenceNumber();
			const BodyId ib = contact.b.GetIndexAndSequenceNumber();
			const BodyId lo = std::min(ia, ib);
			const BodyId hi = std::max(ia, ib);
			if (!seen.insert(std::make_tuple(lo, hi, contact.began)).second)
			{
				continue;	// already recorded this pair+kind this frame
			}
			ContactEvent event;
			event.bodyA = ia;
			event.bodyB = ib;
			event.began = contact.began;
			this->mFrameContacts.push_back(event);
		}
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
		Ogre::Vector3 const & position, Ogre::Quaternion const & orientation,
		BodyUserData userData)
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
		// the ObjectLayer packs the game-layer index (drives the collision
		// matrix) with the motion bit (static vs. moving, still bodyType-derived)
		const bool moving = desc.bodyType != PhysicsWorld::BT_STATIC;
		const int gameLayer = this->mImpl->mLayerConfig.layerIndex(desc.layer);
		const JPH::ObjectLayer layer = makeObjectLayer(gameLayer, moving);
		JPH::BodyCreationSettings settings(shape, toJolt(position),
			toJolt(orientation), motionType, layer);
		settings.mFriction = desc.friction;
		settings.mRestitution = desc.restitution;
		// sensor = trigger volume: overlaps are DETECTED (the contact listener
		// fires) but produce NO collision response. It still obeys the layer
		// matrix (only detects bodies its layer collides with), and a STATIC
		// sensor detects the DYNAMIC bodies moving through it (the roller goal).
		settings.mIsSensor = desc.isSensor;
		// tag the body with its owner handle so the contact drain can map back
		settings.mUserData = static_cast<JPH::uint64>(userData);
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
		const BodyId bodyId = body->GetID().GetIndexAndSequenceNumber();
		// mirror the tag in the main-thread map: OnContactRemoved carries only a
		// BodyID (no Body ref), so the drain resolves owners through this map -
		// which is cleared in destroyBody, giving the honest "no live owner"
		if (userData != 0)
		{
			this->mImpl->mBodyUserData[bodyId] = userData;
		}
		return bodyId;
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
		// a disabled body (@see setBodyEnabled) is already out of the simulation
		if (bodyInterface.IsAdded(id))
		{
			bodyInterface.RemoveBody(id);
		}
		bodyInterface.DestroyBody(id);
		// drop the owner tag: a still-queued OnContactRemoved for this body now
		// resolves to 0 in the drain, so it is never dispatched to a dead object
		this->mImpl->mBodyUserData.erase(bodyId);
	}
	//---------------------------------------------------------
	void PhysicsWorld::setBodyUserData(BodyId bodyId, BodyUserData userData)
	{
		if (!this->mImpl || bodyId == PhysicsWorld::INVALID_BODY_ID)
		{
			return;
		}
		if (userData != 0)
		{
			this->mImpl->mBodyUserData[bodyId] = userData;
		}
		else
		{
			this->mImpl->mBodyUserData.erase(bodyId);
		}
		// keep the on-body tag in sync (used for direct Body::GetUserData reads)
		this->mImpl->mPhysicsSystem.GetBodyInterface().SetUserData(
			JPH::BodyID(bodyId), static_cast<JPH::uint64>(userData));
	}
	//---------------------------------------------------------
	PhysicsWorld::BodyUserData PhysicsWorld::getBodyUserData(BodyId bodyId) const
	{
		if (!this->mImpl || bodyId == PhysicsWorld::INVALID_BODY_ID)
		{
			return 0;
		}
		std::unordered_map<BodyId, BodyUserData>::const_iterator it =
			this->mImpl->mBodyUserData.find(bodyId);
		return it != this->mImpl->mBodyUserData.end() ? it->second : 0;
	}
	//---------------------------------------------------------
	void PhysicsWorld::setBodyEnabled(BodyId bodyId, bool enabled)
	{
		if (!this->mImpl || bodyId == PhysicsWorld::INVALID_BODY_ID)
		{
			return;
		}
		JPH::BodyInterface & bodyInterface = this->mImpl->mPhysicsSystem.GetBodyInterface();
		const JPH::BodyID id(bodyId);
		const bool added = bodyInterface.IsAdded(id);
		if (enabled && !added)
		{
			// re-entry keeps the pose and velocities the body left with
			bodyInterface.AddBody(id,
				bodyInterface.GetMotionType(id) == JPH::EMotionType::Static ?
				JPH::EActivation::DontActivate : JPH::EActivation::Activate);
		}
		else if (!enabled && added)
		{
			bodyInterface.RemoveBody(id);
		}
	}
	//---------------------------------------------------------
	bool PhysicsWorld::isBodyEnabled(BodyId bodyId) const
	{
		if (!this->mImpl || bodyId == PhysicsWorld::INVALID_BODY_ID)
		{
			return false;
		}
		return this->mImpl->mPhysicsSystem.GetBodyInterface().IsAdded(
			JPH::BodyID(bodyId));
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
		// entry still moves, so a teleported static body collides at the new
		// pose. Disabled bodies (@see setBodyEnabled) are not in the broadphase
		// and must not be activated either - they re-enter at the new pose.
		const JPH::EActivation activation =
			(bodyInterface.GetMotionType(id) == JPH::EMotionType::Static ||
				!bodyInterface.IsAdded(id)) ?
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
	//--- PhysicsWorld::LayerConfig ---------------------------
	//---------------------------------------------------------
	const String PhysicsWorld::LayerConfig::LAYERS_SETTING_KEY = "physics.layers";
	const String PhysicsWorld::LayerConfig::LAYERS_FILE_EXTENSION = ".olayers";
	const String PhysicsWorld::LayerConfig::LAYERS_FILE_MAGIC = "orkige.olayers";
	const int PhysicsWorld::LayerConfig::LAYERS_FORMAT_VERSION = 1;
	const String PhysicsWorld::LayerConfig::DEFAULT_LAYER_NAME = "Default";
	//---------------------------------------------------------
	PhysicsWorld::LayerConfig::LayerConfig()
	{
		this->loadDefaults();
	}
	//---------------------------------------------------------
	void PhysicsWorld::LayerConfig::loadDefaults()
	{
		// a single "Default" layer that collides with everything - a project
		// with no .olayers asset behaves EXACTLY as before this feature landed
		this->names.clear();
		this->names.push_back(DEFAULT_LAYER_NAME);
		this->matrix.assign(1, std::vector<bool>(1, true));
	}
	//---------------------------------------------------------
	int PhysicsWorld::LayerConfig::getLayerCount() const
	{
		return static_cast<int>(this->names.size());
	}
	//---------------------------------------------------------
	int PhysicsWorld::LayerConfig::layerIndex(String const & name) const
	{
		if (!name.empty())
		{
			for (std::size_t index = 0; index < this->names.size(); ++index)
			{
				if (this->names[index] == name)
				{
					return static_cast<int>(index);
				}
			}
		}
		// migration: an empty or unknown layer lands on Default (index 0), which
		// collides with everything - old scenes behave identically
		return 0;
	}
	//---------------------------------------------------------
	String PhysicsWorld::LayerConfig::layerName(int index) const
	{
		if (index < 0 || index >= this->getLayerCount())
		{
			return String();
		}
		return this->names[static_cast<std::size_t>(index)];
	}
	//---------------------------------------------------------
	bool PhysicsWorld::LayerConfig::collides(int a, int b) const
	{
		const int count = this->getLayerCount();
		if (a < 0 || b < 0 || a >= count || b >= count)
		{
			return false;
		}
		return this->matrix[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)];
	}
	//---------------------------------------------------------
	void PhysicsWorld::LayerConfig::setCollision(int a, int b, bool value)
	{
		const int count = this->getLayerCount();
		if (a < 0 || b < 0 || a >= count || b >= count)
		{
			return;
		}
		// symmetric by construction: Jolt asks both (A,B) and (B,A)
		this->matrix[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] = value;
		this->matrix[static_cast<std::size_t>(b)][static_cast<std::size_t>(a)] = value;
	}
	//---------------------------------------------------------
	bool PhysicsWorld::LayerConfig::isSymmetric() const
	{
		const std::size_t count = this->names.size();
		if (this->matrix.size() != count)
		{
			return false;
		}
		for (std::size_t a = 0; a < count; ++a)
		{
			if (this->matrix[a].size() != count)
			{
				return false;
			}
			for (std::size_t b = a + 1; b < count; ++b)
			{
				if (this->matrix[a][b] != this->matrix[b][a])
				{
					return false;
				}
			}
		}
		return true;
	}
	//---------------------------------------------------------
	void PhysicsWorld::LayerConfig::symmetrize()
	{
		const std::size_t count = this->names.size();
		// normalise the matrix to a full NxN first (a hand-edited file may be
		// ragged); missing cells default to no-collision
		this->matrix.resize(count);
		for (std::size_t a = 0; a < count; ++a)
		{
			this->matrix[a].resize(count, false);
		}
		for (std::size_t a = 0; a < count; ++a)
		{
			for (std::size_t b = a + 1; b < count; ++b)
			{
				// a collision authored in EITHER direction stands
				const bool both = this->matrix[a][b] || this->matrix[b][a];
				this->matrix[a][b] = both;
				this->matrix[b][a] = both;
			}
		}
	}
	//---------------------------------------------------------
	bool PhysicsWorld::LayerConfig::load(String const & fileName)
	{
		optr<XMLArchive> ar = onew(new XMLArchive());
		if (!ar->startReading(fileName))
		{
			oDebugMsg("physic", 0, "PhysicsWorld::LayerConfig: could not open layer file: " << fileName);
			return false;
		}
		String magic;
		ar >> magic;
		if (magic != LAYERS_FILE_MAGIC)
		{
			oDebugMsg("physic", 0, "PhysicsWorld::LayerConfig: " << fileName
				<< " is not an orkige layer file (magic: \"" << magic << "\")");
			ar->stopReading();
			return false;
		}
		int version = 0;
		ar >> version;
		if (version > LAYERS_FORMAT_VERSION)
		{
			oDebugMsg("physic", 0, "PhysicsWorld::LayerConfig: layer file " << fileName
				<< " has unsupported version " << version << " (supported: "
				<< LAYERS_FORMAT_VERSION << ")");
			ar->stopReading();
			return false;
		}
		// build into scratch; the live config is only replaced on success
		LayerConfig loaded;
		loaded.names.clear();
		loaded.matrix.clear();
		unsigned int layerCount = 0;
		ar >> layerCount;
		for (unsigned int layerIndex = 0; layerIndex < layerCount; ++layerIndex)
		{
			String name;
			ar >> name;
			loaded.names.push_back(name);
		}
		loaded.matrix.assign(layerCount, std::vector<bool>(layerCount, false));
		for (unsigned int a = 0; a < layerCount; ++a)
		{
			for (unsigned int b = 0; b < layerCount; ++b)
			{
				bool value = false;
				ar >> value;
				loaded.matrix[a][b] = value;
			}
		}
		ar->stopReading();
		if (loaded.names.empty())
		{
			// an empty set is meaningless - keep at least Default
			loaded.loadDefaults();
		}
		// enforce symmetry: Jolt asks both directions (a hand edit may be ragged)
		loaded.symmetrize();
		*this = loaded;
		oDebugMsg("physic", 0, "PhysicsWorld::LayerConfig: loaded "
			<< this->names.size() << " layer(s) from " << fileName);
		return true;
	}
	//---------------------------------------------------------
	bool PhysicsWorld::LayerConfig::save(String const & fileName) const
	{
		optr<XMLArchive> ar = onew(new XMLArchive());
		if (!ar->startWriting(fileName))
		{
			oDebugMsg("physic", 0, "PhysicsWorld::LayerConfig: could not start writing layer file: " << fileName);
			return false;
		}
		ar << LAYERS_FILE_MAGIC;
		int version = LAYERS_FORMAT_VERSION;
		ar << version;
		unsigned int layerCount = static_cast<unsigned int>(this->names.size());
		ar << layerCount;
		for (String const & name : this->names)
		{
			String value = name;
			ar << value;
		}
		for (unsigned int a = 0; a < layerCount; ++a)
		{
			for (unsigned int b = 0; b < layerCount; ++b)
			{
				bool value = this->collides(static_cast<int>(a), static_cast<int>(b));
				ar << value;
			}
		}
		bool written = ar->stopWriting();
		if (!written)
		{
			oDebugMsg("physic", 0, "PhysicsWorld::LayerConfig: error while writing layer file: " << fileName);
		}
		return written;
	}
	//---------------------------------------------------------
	void PhysicsWorld::LayerConfig::loadForProject(Project const & project)
	{
		const String reference = project.getSetting(LAYERS_SETTING_KEY);
		if (reference.empty())
		{
			// no override authored: the built-in default (collide-with-all) stands
			this->loadDefaults();
			return;
		}
		const String path = project.resolvePath(reference);
		if (!this->load(path))
		{
			// a referenced-but-broken file must not silently drop collisions:
			// fall back to the defaults (load already logged the reason)
			oDebugMsg("physic", 0, "PhysicsWorld::LayerConfig: layer override '"
				<< reference << "' could not be loaded - keeping the built-in defaults");
			this->loadDefaults();
		}
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
