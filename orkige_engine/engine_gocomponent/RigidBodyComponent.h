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
