/********************************************************************
	created:	Tuesday 2026/07/07 at 21:00
	filename: 	RigidBodyComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RigidBodyComponent_h__7_7_2026__21_00_00__
#define __RigidBodyComponent_h__7_7_2026__21_00_00__

#include <core_game/GameObjectComponent.h>
#include "engine_physic/PhysicsWorld.h"
#include "engine_render/RenderMath.h"

namespace Orkige
{
	class GameObjectManager;

	//! @brief attaches a rigid body (PhysicsWorld / Jolt) to a GameObject
	//! @remarks needs a sibling TransformComponent. The body is created lazily
	//! on the first component update (so shape/type/mass setters and the
	//! initial transform pose can be applied first) and destroyed with the
	//! component. Every update the poses are synced: simulation ->
	//! TransformComponent for dynamic bodies, TransformComponent ->
	//! simulation for kinematic bodies.
	class ORKIGE_ENGINE_DLL RigidBodyComponent : public GameObjectComponent
	{
		OOBJECT(RigidBodyComponent,GameObjectComponent)
		//--- Types -------------------------------------------------
	public:
		//! @brief fired on a GameObject when one of its bodies STARTS touching
		//! another (WP #88, begin = Jolt OnContactAdded). The event data is a
		//! StringUtil::StringObject carrying the OTHER GameObject's id. Dispatched
		//! on the MAIN thread from the contact drain (@see dispatchContacts), so
		//! native components / the debug protocol can observe contacts without
		//! scripting. @ingroup EngineEvents
		DECL_EVENTTYPE(ContactBeganEvent);
		//! @brief fired on a GameObject when one of its bodies STOPS touching
		//! another (end = Jolt OnContactRemoved). @see ContactBeganEvent
		DECL_EVENTTYPE(ContactEndedEvent);
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		PhysicsWorld::BodyDesc	mBodyDesc;	//!< creation parameters, applied when the body is created
		PhysicsWorld::BodyId	mBodyId;	//!< body handle or INVALID_BODY_ID before creation
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		RigidBodyComponent();
		//! destructor
		virtual ~RigidBodyComponent();
		//! set the motion type (before body creation; default BT_DYNAMIC)
		void setBodyType(PhysicsWorld::BodyType bodyType);
		//! get the motion type
		inline PhysicsWorld::BodyType getBodyType() const;
		//! use a box collision shape (before body creation; default 0.5^3 halfExtents box)
		void setBoxShape(Vec3 const & halfExtents);
		//! use a sphere collision shape (before body creation)
		void setSphereShape(float radius);
		//! use a capsule collision shape (before body creation)
		void setCapsuleShape(float halfHeight, float radius);
		//! set the mass in kg for dynamic bodies (before body creation; <= 0 = derived from shape)
		void setMass(float mass);
		//! set the friction coefficient (before body creation)
		void setFriction(float friction);
		//! set the restitution / bounciness (before body creation)
		void setRestitution(float restitution);
		//! @brief 2D mode: lock translation to the X/Y plane and rotation to the Z axis
		//! @remarks works before and after body creation (dynamic bodies only)
		void setPlanarMode(bool planar);
		//! is 2D mode enabled
		inline bool getPlanarMode() const;
		//! @brief set the collision-layer NAME (before body creation; the layer
		//! is resolved against the PhysicsWorld's LayerConfig at createBody)
		void setLayer(String const & layer);
		//! the collision-layer name ("Default" unless set)
		inline String const & getLayer() const;
		//! @brief make this a SENSOR / trigger volume (before body creation): it
		//! detects overlaps (firing ContactBegan/EndedEvent + the script hooks)
		//! with NO collision response. Composes with the layer - a sensor only
		//! detects bodies its layer collides with per the LayerConfig matrix. A
		//! STATIC sensor detects DYNAMIC bodies moving through it (the roller
		//! goal: a static sensor + the dynamic ball).
		void setIsSensor(bool isSensor);
		//! is this body a sensor / trigger volume (@see setIsSensor)
		inline bool isSensor() const;
		//! set linear velocity in m/s (needs the created body)
		void setLinearVelocity(Vec3 const & velocity);
		//! get linear velocity in m/s (ZERO before body creation)
		Vec3 getLinearVelocity() const;
		//! set angular velocity in rad/s (needs the created body)
		void setAngularVelocity(Vec3 const & velocity);
		//! get angular velocity in rad/s (ZERO before body creation)
		Vec3 getAngularVelocity() const;
		//! apply an impulse (kg*m/s) at the center of mass (needs the created body)
		void applyImpulse(Vec3 const & impulse);
		//! apply a force (N) at the center of mass for the next step (needs the created body)
		void applyForce(Vec3 const & force);
		//! @brief teleport the body AND the sibling TransformComponent to the
		//! WORLD-space pose, killing all momentum
		//! @remarks unlike moving the TransformComponent (which kinematic
		//! bodies follow only while the simulation steps), this also works
		//! while PhysicsWorld is PAUSED - the collision geometry moves
		//! immediately. This is the API for sliding whole tile groups in
		//! "move the world" modes and for respawns. Delegates to
		//! TransformComponent::teleport, so rigid bodies of child GameObjects
		//! are snapped along with the subtree.
		void teleport(Vec3 const & position, Quat const & orientation);
		//! has the rigid body been created in the PhysicsWorld yet
		inline bool hasBody() const;
		//! get the PhysicsWorld body handle (INVALID_BODY_ID before creation)
		inline PhysicsWorld::BodyId getBodyId() const;
		//! get the body creation parameters (what the editor inspects/edits)
		inline PhysicsWorld::BodyDesc const & getBodyDesc() const;
		//! @brief publish THIS dynamic body's simulated world pose into the
		//! sibling TransformComponent (no-op for non-dynamic bodies, missing
		//! bodies and bodies outside the simulation)
		void syncFromSimulation();
		//! @brief publish every ACTIVE dynamic body's simulated pose under the
		//! given manager into its TransformComponent - the sim->scene half of
		//! the canonical PLAYER LOOP TICK ORDER (tools/player/main.cpp):
		//! component updates run BEFORE the physics step there, so without
		//! this post-physics pass rendering and the debug stream would show a
		//! one-tick-old pose (the classic step-while-paused contract caught
		//! exactly that)
		static void syncDynamicBodyPoses(GameObjectManager & gameObjectManager);
		//! @brief MAIN-THREAD dispatch of the physics contacts drained this frame
		//! (PhysicsWorld::getFrameContacts) to game code - THE consumer side of
		//! the worker-thread->queue->main-drain pipeline. For every contact it
		//! maps each body back to its owning GameObject through the body user tag
		//! (set in createBody, re-validated here so a destroyed body never
		//! delivers), fires ContactBegan/EndedEvent on the object and calls the
		//! sibling ScriptComponent's onContactBegin/onContactEnd hook with the
		//! OTHER GameObject. Call it AFTER PhysicsWorld::update in the player loop
		//! (alongside syncDynamicBodyPoses); a no-op without an initialized world.
		//! A contact where one side no longer resolves to a live object is
		//! SKIPPED (v1: both sides must resolve, so a script never sees a nil
		//! `other`) - the stale-id tolerance the drain requires.
		static void dispatchContacts(GameObjectManager & gameObjectManager);
	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
		//! creates the body on first call, then syncs poses with the simulation
		virtual void onUpdateComponent(float deltaTime);
		//! @brief deactivated GameObjects take their body OUT of the simulation
		//! (PhysicsWorld::setBodyEnabled - no collisions, no motion, state
		//! kept); reactivation re-enters at the transform's current world pose
		virtual void onSetActive(bool activeInHierarchy);
		//! create the rigid body at the sibling TransformComponent's current pose
		void createBody();
		//! destroy the rigid body
		void destroyBody();
		//--- SERIALIZATION ---
		//! save the body creation parameters (BodyDesc) to Archive
		virtual void save(optr<IArchive> const & ar);
		//! load the body creation parameters (BodyDesc) from Archive
		virtual void load(optr<IArchive> const & ar);
	private:
		//! @brief deliver ONE contact side (self touched other): fire the C++
		//! ContactBegan/EndedEvent on self and call self's optional Lua hook with
		//! other. Both objects are guaranteed live (@see dispatchContacts).
		static void deliverContact(GameObject* self, GameObject* other, bool began);
	};
	//---------------------------------------------------------
	inline PhysicsWorld::BodyType RigidBodyComponent::getBodyType() const
	{
		return this->mBodyDesc.bodyType;
	}
	//---------------------------------------------------------
	inline bool RigidBodyComponent::getPlanarMode() const
	{
		return this->mBodyDesc.planar;
	}
	//---------------------------------------------------------
	inline String const & RigidBodyComponent::getLayer() const
	{
		return this->mBodyDesc.layer;
	}
	//---------------------------------------------------------
	inline bool RigidBodyComponent::isSensor() const
	{
		return this->mBodyDesc.isSensor;
	}
	//---------------------------------------------------------
	inline bool RigidBodyComponent::hasBody() const
	{
		return this->mBodyId != PhysicsWorld::INVALID_BODY_ID;
	}
	//---------------------------------------------------------
	inline PhysicsWorld::BodyId RigidBodyComponent::getBodyId() const
	{
		return this->mBodyId;
	}
	//---------------------------------------------------------
	inline PhysicsWorld::BodyDesc const & RigidBodyComponent::getBodyDesc() const
	{
		return this->mBodyDesc;
	}
	//---------------------------------------------------------
}

#endif //__RigidBodyComponent_h__7_7_2026__21_00_00__
