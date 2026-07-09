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
#include <core_util/String.h>

#include <cstdint>
#include <vector>

namespace Orkige
{
	class Project;

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
		//! @brief opaque per-body user handle stored ON the body and returned by
		//! the contact drain - wide enough to hold a pointer so the GameObject
		//! bridge tags each body with its owning RigidBodyComponent (mirrors how
		//! TransformComponent tags render nodes with a user pointer). 0 = untagged.
		typedef std::uintptr_t BodyUserData;
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
		//! @brief result of a castRayHit query - plain data, so scripting
		//! languages get the whole answer as one value (castRay's C++ out
		//! parameters do not translate to Lua)
		struct ORKIGE_ENGINE_DLL RayHit
		{
			bool			hit;			//!< did the ray hit a body
			Ogre::Vector3	position;		//!< world hit position (ZERO on miss)
			BodyId			bodyId;			//!< hit body handle (INVALID_BODY_ID on miss)
			RayHit();						// defined in the .cpp (needs INVALID_BODY_ID)
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
			String			layer;			//!< collision layer NAME (LayerConfig); "" / "Default" = the collide-with-all layer 0
			bool			isSensor;		//!< trigger volume: detects overlaps (fires contacts) with NO collision response - composes with layer (only detects bodies its layer collides with)
			BodyDesc() : bodyType(BT_DYNAMIC), shapeType(ST_BOX),
				halfExtents(0.5f, 0.5f, 0.5f), radius(0.5f), halfHeight(0.5f),
				mass(1.0f), friction(0.5f), restitution(0.0f), planar(false),
				layer("Default"), isSensor(false) {}
		};
		//! @brief a single coalesced contact between two bodies for ONE frame,
		//! drained on the MAIN thread after the step loop (@see getFrameContacts).
		//! Jolt fires the raw callbacks on worker threads mid-Update; this is the
		//! safe main-thread product. began = OnContactAdded (once per pair per
		//! frame after coalescing), !began = OnContactRemoved. Resolve a body to
		//! its user tag with getBodyUserData - a body may already be gone by the
		//! time an end event is drained, so the tag is looked up at drain time.
		struct ORKIGE_ENGINE_DLL ContactEvent
		{
			BodyId	bodyA;		//!< first body (INVALID_BODY_ID never appears here)
			BodyId	bodyB;		//!< second body
			bool	began;		//!< true = contact started, false = contact ended
		};
		//! @brief data-driven collision layers: the named game-layer set plus a
		//! SYMMETRIC NxN "do these two layers collide" matrix. Plain data (no
		//! Jolt types) - SET ON THE WORLD BEFORE init(); createBody() routes a
		//! body's BodyDesc::layer name through layerIndex() to its ObjectLayer.
		//! The broadphase motion buckets (static/moving) stay bodyType-derived
		//! and DECOUPLED from the game layer - a game layer may hold both static
		//! and moving bodies.
		//!
		//! CONFIG-ASSET CONVENTION (mirrors engine_input/InputActionMap.h - the
		//! third project-config asset alongside input.oactions and the cvars):
		//!   * an XMLArchive file (*.olayers) REFERENCED from the manifest by a
		//!     Settings key (LAYERS_SETTING_KEY "physics.layers") holding a
		//!     project-relative path; PROJECT-CONFIG, not under assets/, not
		//!     id-tracked by the AssetDatabase.
		//!   * OPTIONAL: without the key the built-in default (a single
		//!     "Default" layer that collides with everything) applies, so a
		//!     project with no asset behaves EXACTLY as before.
		//!   * export bundles it when referenced - Util/orkige_export.py stages
		//!     it via CONFIG_SETTING_KEYS.
		struct ORKIGE_ENGINE_DLL LayerConfig
		{
			StringVector					names;	//!< layer names; index 0 is always the collide-with-all "Default"
			std::vector< std::vector<bool> >	matrix;	//!< symmetric NxN: matrix[a][b] = layers a and b collide

			//--- config-asset convention constants ---
			static const String LAYERS_SETTING_KEY;		//!< manifest Settings key ("physics.layers")
			static const String LAYERS_FILE_EXTENSION;	//!< ".olayers"
			static const String LAYERS_FILE_MAGIC;		//!< "orkige.olayers"
			static const int	LAYERS_FORMAT_VERSION;	//!< version written into every saved file
			static const String DEFAULT_LAYER_NAME;		//!< "Default" (layer index 0)

			//! constructs the built-in default (single "Default", collides with all)
			LayerConfig();
			//! reset to the built-in default set
			void loadDefaults();
			//! number of named layers (always >= 1)
			int getLayerCount() const;
			//! @brief index of a named layer; the Default layer (0) for an
			//! empty or unknown name - THE migration rule: a body with no/unknown
			//! layer lands on Default (index 0), which collides with everything
			int layerIndex(String const & name) const;
			//! name of a layer index ("" out of range)
			String layerName(int index) const;
			//! do layers a and b collide (false if either index is out of range)
			bool collides(int a, int b) const;
			//! set (SYMMETRICALLY) whether layers a and b collide
			void setCollision(int a, int b, bool value);
			//! is the matrix symmetric (matrix[a][b] == matrix[b][a] everywhere)
			bool isSymmetric() const;
			//! @brief force symmetry: matrix[a][b] = matrix[b][a] = the OR of the
			//! two (a collision authored in either direction stands)
			void symmetrize();

			//--- asset IO (XMLArchive) ---
			//! @brief load an .olayers file. On any error the config is left
			//! UNCHANGED and false is returned (honest, non-destructive).
			bool load(String const & fileName);
			//! write the current config to an .olayers file (round-trip)
			bool save(String const & fileName) const;
			//! @brief the project-load entry point: if the manifest has
			//! LAYERS_SETTING_KEY, resolve it project-relative and load();
			//! otherwise loadDefaults(). A referenced-but-broken file keeps the
			//! defaults (logged), so a typo never breaks collisions.
			void loadForProject(Project const & project);
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
		bool				mPaused;				//!< update() no-ops while true
		LayerConfig			mLayerConfig;			//!< collision layers; set BEFORE init() (default = collide-with-all)
		std::vector<ContactEvent>	mFrameContacts;	//!< contacts drained during the LAST update() (cleared each call); read after update(), dispatched on the main thread
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
		//! @brief set the collision-layer configuration. MUST be called BEFORE
		//! init() - the Jolt filters are built from it at init time and there
		//! are no mid-session layer changes in v1 (a re-init is heavy). Setting
		//! it while initialized is ignored (logged).
		void setLayerConfig(LayerConfig const & config);
		//! the active collision-layer configuration
		inline LayerConfig const & getLayerConfig() const;
		//! @brief advance the simulation by deltaTime (seconds)
		//! @remarks steps the world in FIXED_TIMESTEP increments, at most
		//! MAX_STEPS_PER_UPDATE per call; the remainder carries over.
		//! A no-op while paused (@see setPaused).
		void update(float deltaTime);
		//! @brief pause/resume the simulation: update() becomes a no-op
		//! @remarks bodies keep their velocities and can still be teleported
		//! (setBodyTransform) while paused - the "move the world" mode of
		//! sliding-tile games pauses the sim and teleports whole tile groups
		void setPaused(bool paused);
		//! is the simulation paused
		inline bool isPaused() const;
		//! set world gravity (default (0, -9.81, 0))
		void setGravity(Ogre::Vector3 const & gravity);
		//! get world gravity
		Ogre::Vector3 getGravity() const;
		//! @brief create a rigid body at the given pose
		//! @param userData opaque per-body tag stored on the body (0 = untagged);
		//! the GameObject bridge passes its RigidBodyComponent here so the contact
		//! drain can map a body back to its owner (@see getBodyUserData)
		//! @return body handle or INVALID_BODY_ID on failure
		BodyId createBody(BodyDesc const & desc, Ogre::Vector3 const & position,
			Ogre::Quaternion const & orientation, BodyUserData userData = 0);
		//! remove and destroy a body (also drops its user-data tag)
		void destroyBody(BodyId bodyId);
		//! @brief set the opaque user tag of an existing body (@see createBody)
		void setBodyUserData(BodyId bodyId, BodyUserData userData);
		//! @brief read a body's opaque user tag; 0 for an untagged or
		//! already-destroyed body - the honest "no live owner" answer the
		//! contact drain relies on
		BodyUserData getBodyUserData(BodyId bodyId) const;
		//! @brief the contacts drained on the main thread during the last
		//! update() call (empty while paused / before the first step). Jolt fires
		//! contact callbacks on WORKER threads during Update; update() queues them
		//! under a mutex and drains + coalesces them here AFTER the step loop, so
		//! this is safe to walk on the main thread and dispatch to game code.
		//! Valid until the next update() call.
		inline std::vector<ContactEvent> const & getFrameContacts() const;
		//! @brief take a body out of / put it back into the simulation WITHOUT
		//! destroying it (Jolt Remove/AddBody) - the deactivated-GameObject
		//! mechanism: a disabled body neither collides nor moves, keeps its
		//! shape, velocities and pose, and can still be teleported
		//! (setBodyTransform) while disabled
		void setBodyEnabled(BodyId bodyId, bool enabled);
		//! is the body currently part of the simulation (@see setBodyEnabled)
		bool isBodyEnabled(BodyId bodyId) const;
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
		//! @brief castRay variant returning the result as one RayHit value -
		//! the form the Lua bindings expose (out parameters stay C++-only)
		RayHit castRayHit(Ogre::Vector3 const & origin,
			Ogre::Vector3 const & direction, float maxDistance) const;
	protected:
	private:
		//! @brief MAIN-THREAD drain of the worker-thread contact queue into
		//! mFrameContacts, coalescing per frame (once per pair per kind). Called
		//! from update() AFTER the step loop, when the worker threads are idle.
		void drainContactQueue();
	};
	//---------------------------------------------------------
	inline bool PhysicsWorld::isInitialized() const
	{
		return this->mImpl != NULL;
	}
	//---------------------------------------------------------
	inline PhysicsWorld::LayerConfig const & PhysicsWorld::getLayerConfig() const
	{
		return this->mLayerConfig;
	}
	//---------------------------------------------------------
	inline bool PhysicsWorld::isPaused() const
	{
		return this->mPaused;
	}
	//---------------------------------------------------------
	inline std::vector<PhysicsWorld::ContactEvent> const &
		PhysicsWorld::getFrameContacts() const
	{
		return this->mFrameContacts;
	}
	//---------------------------------------------------------
}

#endif //__PhysicsWorld_h__7_7_2026__21_00_00__
