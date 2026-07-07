/********************************************************************
	created:	Tuesday 2026/07/07 at 21:00
	filename: 	PhysicsWorld.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __PhysicsWorld_h__7_7_2026__21_00_00__
#define __PhysicsWorld_h__7_7_2026__21_00_00__

#include "engine_module/EnginePrerequisites.h"
#include <core_base/Interface.h>
#include <core_util/Singleton.h>

namespace Orkige
{
	//! @brief rigid body dynamics world (Jolt Physics backend)
	//! @remarks backend-agnostic facade around JPH::PhysicsSystem: all Jolt
	//! types stay inside PhysicsWorld.cpp, users talk plain Ogre math types
	//! and opaque BodyId handles. update() runs a fixed-timestep accumulator
	//! (FIXED_TIMESTEP steps, capped per call); drive it from the app loop
	//! with the frame delta.
	//! @todo Phase 2: step from the engine loop instead (FrameStartedEvent
	//! listener) once the engine owns a proper game loop again.
	class ORKIGE_ENGINE_DLL PhysicsWorld : public Singleton<PhysicsWorld>, public Interface
	{
		OOBJECT(PhysicsWorld,Interface);
		DECL_OSINGLETON(PhysicsWorld)
		//--- Types -------------------------------------------------
	public:
		//! opaque rigid body handle (stable across the body's lifetime)
		typedef Ogre::uint32 BodyId;
		//! motion type of a rigid body
		enum BodyType
		{
			BT_STATIC = 0,		//!< never moves (level geometry)
			BT_KINEMATIC = 1,	//!< moved by animation/game code, pushes dynamic bodies
			BT_DYNAMIC = 2		//!< fully simulated
		};
		//! collision shape type of a rigid body
		enum ShapeType
		{
			ST_BOX = 0,			//!< box from halfExtents
			ST_SPHERE = 1,		//!< sphere from radius
			ST_CAPSULE = 2		//!< capsule from halfHeight (cylinder part) + radius
		};
		//! everything needed to create a rigid body (plain data, no backend types)
		struct ORKIGE_ENGINE_DLL BodyDesc
		{
			BodyType		bodyType;		//!< motion type
			ShapeType		shapeType;		//!< collision shape
			Ogre::Vector3	halfExtents;	//!< ST_BOX half extents
			float			radius;			//!< ST_SPHERE / ST_CAPSULE radius
			float			halfHeight;		//!< ST_CAPSULE half height of the cylinder part
			float			mass;			//!< mass in kg for dynamic bodies (<= 0 = derived from shape density)
			float			friction;		//!< friction coefficient (usually 0..1)
			float			restitution;	//!< bounciness (0 = none, 1 = fully elastic)
			bool			planar;			//!< 2D mode: lock translation to X/Y and rotation to Z (dynamic bodies)
			BodyDesc() : bodyType(BT_DYNAMIC), shapeType(ST_BOX),
				halfExtents(0.5f, 0.5f, 0.5f), radius(0.5f), halfHeight(0.5f),
				mass(1.0f), friction(0.5f), restitution(0.0f), planar(false) {}
		};
	protected:
		//! hides all Jolt types (only defined in PhysicsWorld.cpp)
		class PhysicsWorldImpl;
	private:
		//--- Variables ---------------------------------------------
	public:
		static const BodyId INVALID_BODY_ID;		//!< returned when body creation fails / no body
		static const float FIXED_TIMESTEP;			//!< simulation step size (1/60 s)
		static const int MAX_STEPS_PER_UPDATE;		//!< step cap per update() call (avoids the spiral of death)
	protected:
		PhysicsWorldImpl*	mImpl;					//!< Jolt guts, NULL until init()
		float				mAccumulator;			//!< fixed-timestep accumulator
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor (does not touch the backend - call init())
		PhysicsWorld();
		//! destructor (implies deinit())
		virtual ~PhysicsWorld();
		//! @brief init the physics system
		//! @remarks registers the Jolt runtime (allocators, factory, types) once
		//! per process on first call. numBodyMutexes 0 = backend default.
		bool init(unsigned int maxBodies = 1024, unsigned int numBodyMutexes = 0,
			unsigned int maxBodyPairs = 1024, unsigned int maxContactConstraints = 1024);
		//! destroy all bodies and shut the physics system down
		void deinit();
		//! is the physics system initialized
		inline bool isInitialized() const;
		//! @brief advance the simulation by deltaTime (seconds)
		//! @remarks steps the world in FIXED_TIMESTEP increments, at most
		//! MAX_STEPS_PER_UPDATE per call; the remainder carries over
		void update(float deltaTime);
		//! set world gravity (default (0, -9.81, 0))
		void setGravity(Ogre::Vector3 const & gravity);
		//! get world gravity
		Ogre::Vector3 getGravity() const;
		//! @brief create a rigid body at the given pose
		//! @return body handle or INVALID_BODY_ID on failure
		BodyId createBody(BodyDesc const & desc, Ogre::Vector3 const & position,
			Ogre::Quaternion const & orientation);
		//! remove and destroy a body
		void destroyBody(BodyId bodyId);
		//! read the body pose back from the simulation
		bool getBodyTransform(BodyId bodyId, Ogre::Vector3 & position, Ogre::Quaternion & orientation) const;
		//! teleport a body (activates it)
		void setBodyTransform(BodyId bodyId, Ogre::Vector3 const & position, Ogre::Quaternion const & orientation);
		//! @brief move a kinematic body so it reaches the target pose in deltaTime seconds
		//! @remarks sets the body velocities accordingly so it pushes dynamic bodies on the way
		void moveKinematic(BodyId bodyId, Ogre::Vector3 const & targetPosition,
			Ogre::Quaternion const & targetOrientation, float deltaTime);
		//! @brief toggle 2D mode on an existing dynamic body
		//! @remarks planar = true locks translation to X/Y and rotation to Z
		//! (Jolt EAllowedDOFs::Plane2D); false restores all six degrees of freedom
		void setBodyPlanarMode(BodyId bodyId, bool planar);
		//! set linear velocity (m/s)
		void setLinearVelocity(BodyId bodyId, Ogre::Vector3 const & velocity);
		//! get linear velocity (m/s)
		Ogre::Vector3 getLinearVelocity(BodyId bodyId) const;
		//! set angular velocity (rad/s)
		void setAngularVelocity(BodyId bodyId, Ogre::Vector3 const & velocity);
		//! get angular velocity (rad/s)
		Ogre::Vector3 getAngularVelocity(BodyId bodyId) const;
		//! apply an impulse (kg*m/s) at the center of mass (activates the body)
		void applyImpulse(BodyId bodyId, Ogre::Vector3 const & impulse);
		//! apply a force (N) at the center of mass for the next simulation step
		void applyForce(BodyId bodyId, Ogre::Vector3 const & force);
		//! is the body awake in the simulation
		bool isBodyActive(BodyId bodyId) const;
		//! @brief cast a ray against all bodies (closest hit)
		//! @remarks successor of the old scene-query based CollisionTools checks
		//! @param direction does not need to be normalized
		//! @return true on hit; hitPosition/hitBodyId only written on hit
		bool castRay(Ogre::Vector3 const & origin, Ogre::Vector3 const & direction,
			float maxDistance, Ogre::Vector3 & hitPosition, BodyId & hitBodyId) const;
	protected:
	private:
	};
	//---------------------------------------------------------
	inline bool PhysicsWorld::isInitialized() const
	{
		return this->mImpl != NULL;
	}
	//---------------------------------------------------------
}

#endif //__PhysicsWorld_h__7_7_2026__21_00_00__
