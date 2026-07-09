/********************************************************************
	created:	Tuesday 2026/07/07 at 21:00
	filename: 	RigidBodyComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gocomponent/RigidBodyComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>

#include "engine_module/EnginePrerequisites.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	RigidBodyComponent::RigidBodyComponent() : mBodyId(PhysicsWorld::INVALID_BODY_ID)
	{
		this->addDependency<TransformComponent>();
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------
	RigidBodyComponent::~RigidBodyComponent()
	{
	}
	//---------------------------------------------------------
	void RigidBodyComponent::setBodyType(PhysicsWorld::BodyType bodyType)
	{
		if (this->hasBody())
		{
			oDebugWarning(false, "RigidBodyComponent::setBodyType ignored - body already created");
			return;
		}
		this->mBodyDesc.bodyType = bodyType;
	}
	//---------------------------------------------------------
	void RigidBodyComponent::setBoxShape(Vec3 const & halfExtents)
	{
		if (this->hasBody())
		{
			oDebugWarning(false, "RigidBodyComponent::setBoxShape ignored - body already created");
			return;
		}
		this->mBodyDesc.shapeType = PhysicsWorld::ST_BOX;
		this->mBodyDesc.halfExtents = halfExtents;
	}
	//---------------------------------------------------------
	void RigidBodyComponent::setSphereShape(float radius)
	{
		if (this->hasBody())
		{
			oDebugWarning(false, "RigidBodyComponent::setSphereShape ignored - body already created");
			return;
		}
		this->mBodyDesc.shapeType = PhysicsWorld::ST_SPHERE;
		this->mBodyDesc.radius = radius;
	}
	//---------------------------------------------------------
	void RigidBodyComponent::setCapsuleShape(float halfHeight, float radius)
	{
		if (this->hasBody())
		{
			oDebugWarning(false, "RigidBodyComponent::setCapsuleShape ignored - body already created");
			return;
		}
		this->mBodyDesc.shapeType = PhysicsWorld::ST_CAPSULE;
		this->mBodyDesc.halfHeight = halfHeight;
		this->mBodyDesc.radius = radius;
	}
	//---------------------------------------------------------
	void RigidBodyComponent::setMass(float mass)
	{
		if (this->hasBody())
		{
			oDebugWarning(false, "RigidBodyComponent::setMass ignored - body already created");
			return;
		}
		this->mBodyDesc.mass = mass;
	}
	//---------------------------------------------------------
	void RigidBodyComponent::setFriction(float friction)
	{
		if (this->hasBody())
		{
			oDebugWarning(false, "RigidBodyComponent::setFriction ignored - body already created");
			return;
		}
		this->mBodyDesc.friction = friction;
	}
	//---------------------------------------------------------
	void RigidBodyComponent::setRestitution(float restitution)
	{
		if (this->hasBody())
		{
			oDebugWarning(false, "RigidBodyComponent::setRestitution ignored - body already created");
			return;
		}
		this->mBodyDesc.restitution = restitution;
	}
	//---------------------------------------------------------
	void RigidBodyComponent::setPlanarMode(bool planar)
	{
		this->mBodyDesc.planar = planar;
		if (this->hasBody())
		{
			PhysicsWorld::getSingleton().setBodyPlanarMode(this->mBodyId, planar);
		}
	}
	//---------------------------------------------------------
	void RigidBodyComponent::setLinearVelocity(Vec3 const & velocity)
	{
		if (this->hasBody())
		{
			PhysicsWorld::getSingleton().setLinearVelocity(this->mBodyId, velocity);
		}
	}
	//---------------------------------------------------------
	Vec3 RigidBodyComponent::getLinearVelocity() const
	{
		if (!this->hasBody())
		{
			return Vec3::ZERO;
		}
		return PhysicsWorld::getSingleton().getLinearVelocity(this->mBodyId);
	}
	//---------------------------------------------------------
	void RigidBodyComponent::setAngularVelocity(Vec3 const & velocity)
	{
		if (this->hasBody())
		{
			PhysicsWorld::getSingleton().setAngularVelocity(this->mBodyId, velocity);
		}
	}
	//---------------------------------------------------------
	Vec3 RigidBodyComponent::getAngularVelocity() const
	{
		if (!this->hasBody())
		{
			return Vec3::ZERO;
		}
		return PhysicsWorld::getSingleton().getAngularVelocity(this->mBodyId);
	}
	//---------------------------------------------------------
	void RigidBodyComponent::applyImpulse(Vec3 const & impulse)
	{
		if (this->hasBody())
		{
			PhysicsWorld::getSingleton().applyImpulse(this->mBodyId, impulse);
		}
	}
	//---------------------------------------------------------
	void RigidBodyComponent::applyForce(Vec3 const & force)
	{
		if (this->hasBody())
		{
			PhysicsWorld::getSingleton().applyForce(this->mBodyId, force);
		}
	}
	//---------------------------------------------------------
	void RigidBodyComponent::teleport(Vec3 const & position,
		Quat const & orientation)
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent =
			componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		// moves the node (world space) and snaps every body in the subtree -
		// including this one - to its resulting world pose, killing momentum
		transformComponent->teleport(position, orientation);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void RigidBodyComponent::onAdd()
	{
		// body creation is deferred to the first update so the owner can
		// configure shape/type and position the TransformComponent first
	}
	//---------------------------------------------------------
	void RigidBodyComponent::onRemove()
	{
		this->destroyBody();
	}
	//---------------------------------------------------------
	void RigidBodyComponent::onUpdateComponent(float deltaTime)
	{
		PhysicsWorld* physicsWorld = PhysicsWorld::getSingletonPtr();
		if (!physicsWorld || !physicsWorld->isInitialized())
		{
			return;
		}
		if (!this->hasBody())
		{
			this->createBody();
			return;
		}
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent =
			componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		switch (this->mBodyDesc.bodyType)
		{
		case PhysicsWorld::BT_DYNAMIC:
			// simulation -> scene (see syncFromSimulation). In runtimes on
			// the canonical tick order (component updates BEFORE the physics
			// step) the post-physics syncDynamicBodyPoses pass republishes
			// the fresh pose the same frame; this per-update sync stays for
			// the loops that step physics first (samples, native modules).
			this->syncFromSimulation();
			break;
		case PhysicsWorld::BT_KINEMATIC:
			// scene -> simulation (with velocities, so it pushes dynamic
			// bodies); the body target is the node's WORLD pose
			physicsWorld->moveKinematic(this->mBodyId,
				transformComponent->getWorldPosition(),
				transformComponent->getWorldOrientation(),
				PhysicsWorld::FIXED_TIMESTEP);
			break;
		case PhysicsWorld::BT_STATIC:
		default:
			break;
		}
	}
	//---------------------------------------------------------
	void RigidBodyComponent::syncFromSimulation()
	{
		if (this->mBodyDesc.bodyType != PhysicsWorld::BT_DYNAMIC ||
			!this->hasBody())
		{
			return;
		}
		PhysicsWorld* physicsWorld = PhysicsWorld::getSingletonPtr();
		if (!physicsWorld || !physicsWorld->isInitialized())
		{
			return;
		}
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent =
			componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		// simulation -> scene; bodies live in WORLD space, the
		// hierarchy-aware setters recompute the local transform
		Vec3 position;
		Quat orientation;
		if (physicsWorld->getBodyTransform(this->mBodyId, position, orientation))
		{
			transformComponent->setWorldPosition(position);
			transformComponent->setWorldOrientation(orientation);
		}
	}
	//---------------------------------------------------------
	void RigidBodyComponent::syncDynamicBodyPoses(GameObjectManager & gameObjectManager)
	{
		PhysicsWorld* physicsWorld = PhysicsWorld::getSingletonPtr();
		if (!physicsWorld || !physicsWorld->isInitialized())
		{
			return;
		}
		for (GameObjectManager::GameObjectMap::value_type const & entry :
			gameObjectManager.getGameObjects())
		{
			GameObject* gameObject = entry.second.get();
			// same gate as the component update loop: deactivated objects
			// have no body in the simulation and must not be touched
			if (!gameObject || !gameObject->isActiveInHierarchy() ||
				!gameObject->hasComponent<RigidBodyComponent>())
			{
				continue;
			}
			gameObject->getComponentPtr<RigidBodyComponent>()->syncFromSimulation();
		}
	}
	//---------------------------------------------------------
	void RigidBodyComponent::createBody()
	{
		oAssert(!this->hasBody());
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent =
			componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		// bodies are created at the WORLD pose (local == world for roots)
		this->mBodyId = PhysicsWorld::getSingleton().createBody(this->mBodyDesc,
			transformComponent->getWorldPosition(), transformComponent->getWorldOrientation());
	}
	//---------------------------------------------------------
	void RigidBodyComponent::onSetActive(bool activeInHierarchy)
	{
		if (!this->hasBody())
		{
			// no body yet: an inactive object does not tick, so the lazy
			// creation simply waits for the first update after activation
			return;
		}
		PhysicsWorld* physicsWorld = PhysicsWorld::getSingletonPtr();
		if (!physicsWorld || !physicsWorld->isInitialized())
		{
			return;
		}
		physicsWorld->setBodyEnabled(this->mBodyId, activeInHierarchy);
		if (activeInHierarchy)
		{
			// the transform may have moved while the body was out of the
			// simulation - re-enter at the node's current world pose
			GameObject* componentOwner = this->getComponentOwner();
			oAssert(componentOwner);
			optr<TransformComponent> transformComponent =
				componentOwner->getComponent<TransformComponent>().lock();
			if (transformComponent && transformComponent->getNode())
			{
				physicsWorld->setBodyTransform(this->mBodyId,
					transformComponent->getWorldPosition(),
					transformComponent->getWorldOrientation());
			}
		}
	}
	//---------------------------------------------------------
	void RigidBodyComponent::destroyBody()
	{
		if (!this->hasBody())
		{
			return;
		}
		if (PhysicsWorld::getSingletonPtr())
		{
			PhysicsWorld::getSingleton().destroyBody(this->mBodyId);
		}
		this->mBodyId = PhysicsWorld::INVALID_BODY_ID;
	}
	//---------------------------------------------------------
	void RigidBodyComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// only the creation parameters (BodyDesc) round-trip: a scene stores
		// the initial state, so runtime simulation state (velocities, applied
		// forces, sleep state) of an already-created body is NOT serialized
		if (this->hasBody() && this->mBodyDesc.bodyType == PhysicsWorld::BT_DYNAMIC)
		{
			oDebugMsg("scene",0,"RigidBodyComponent: saving live dynamic body - runtime velocities are not serialized");
		}
		int bodyType = static_cast<int>(this->mBodyDesc.bodyType);
		int shapeType = static_cast<int>(this->mBodyDesc.shapeType);
		ar << bodyType << shapeType;
		ar << this->mBodyDesc.halfExtents.x << this->mBodyDesc.halfExtents.y << this->mBodyDesc.halfExtents.z;
		ar << this->mBodyDesc.radius << this->mBodyDesc.halfHeight;
		ar << this->mBodyDesc.mass << this->mBodyDesc.friction << this->mBodyDesc.restitution;
		ar << this->mBodyDesc.planar;
	}
	//---------------------------------------------------------
	void RigidBodyComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		int bodyType = 0;
		int shapeType = 0;
		ar >> bodyType >> shapeType;
		this->mBodyDesc.bodyType = static_cast<PhysicsWorld::BodyType>(bodyType);
		this->mBodyDesc.shapeType = static_cast<PhysicsWorld::ShapeType>(shapeType);
		ar >> this->mBodyDesc.halfExtents.x >> this->mBodyDesc.halfExtents.y >> this->mBodyDesc.halfExtents.z;
		ar >> this->mBodyDesc.radius >> this->mBodyDesc.halfHeight;
		ar >> this->mBodyDesc.mass >> this->mBodyDesc.friction >> this->mBodyDesc.restitution;
		ar >> this->mBodyDesc.planar;
		if (this->hasBody())
		{
			// the body is created lazily on the first update, so a load right
			// after addComponent (the SceneSerializer path) lands here before
			// creation; recreate defensively if someone loads into a live one
			this->destroyBody();
		}
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(RigidBodyComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(setBodyType)
		OFUNC(setBoxShape)
		OFUNC(setSphereShape)
		OFUNC(setCapsuleShape)
		OFUNC(setMass)
		OFUNC(setFriction)
		OFUNC(setRestitution)
		OFUNC(setPlanarMode)
		OFUNC(getPlanarMode)
		OFUNC(setLinearVelocity)
		OFUNC(getLinearVelocity)
		OFUNC(setAngularVelocity)
		OFUNC(getAngularVelocity)
		OFUNC(applyImpulse)
		OFUNC(applyForce)
		OFUNC(teleport)
		OFUNC(hasBody)
		OFUNC(getBodyId)
	OOBJECT_END
}
