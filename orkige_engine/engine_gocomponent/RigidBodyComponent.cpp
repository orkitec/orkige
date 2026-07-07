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
	void RigidBodyComponent::setBoxShape(Ogre::Vector3 const & halfExtents)
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
	void RigidBodyComponent::setLinearVelocity(Ogre::Vector3 const & velocity)
	{
		if (this->hasBody())
		{
			PhysicsWorld::getSingleton().setLinearVelocity(this->mBodyId, velocity);
		}
	}
	//---------------------------------------------------------
	Ogre::Vector3 RigidBodyComponent::getLinearVelocity() const
	{
		if (!this->hasBody())
		{
			return Ogre::Vector3::ZERO;
		}
		return PhysicsWorld::getSingleton().getLinearVelocity(this->mBodyId);
	}
	//---------------------------------------------------------
	void RigidBodyComponent::setAngularVelocity(Ogre::Vector3 const & velocity)
	{
		if (this->hasBody())
		{
			PhysicsWorld::getSingleton().setAngularVelocity(this->mBodyId, velocity);
		}
	}
	//---------------------------------------------------------
	Ogre::Vector3 RigidBodyComponent::getAngularVelocity() const
	{
		if (!this->hasBody())
		{
			return Ogre::Vector3::ZERO;
		}
		return PhysicsWorld::getSingleton().getAngularVelocity(this->mBodyId);
	}
	//---------------------------------------------------------
	void RigidBodyComponent::applyImpulse(Ogre::Vector3 const & impulse)
	{
		if (this->hasBody())
		{
			PhysicsWorld::getSingleton().applyImpulse(this->mBodyId, impulse);
		}
	}
	//---------------------------------------------------------
	void RigidBodyComponent::applyForce(Ogre::Vector3 const & force)
	{
		if (this->hasBody())
		{
			PhysicsWorld::getSingleton().applyForce(this->mBodyId, force);
		}
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
			{
				// simulation -> scene
				Ogre::Vector3 position;
				Ogre::Quaternion orientation;
				if (physicsWorld->getBodyTransform(this->mBodyId, position, orientation))
				{
					transformComponent->setPosition(position);
					transformComponent->setOrientation(orientation);
				}
			}
			break;
		case PhysicsWorld::BT_KINEMATIC:
			// scene -> simulation (with velocities, so it pushes dynamic bodies)
			physicsWorld->moveKinematic(this->mBodyId,
				transformComponent->getPosition(),
				transformComponent->getOrientation(),
				PhysicsWorld::FIXED_TIMESTEP);
			break;
		case PhysicsWorld::BT_STATIC:
		default:
			break;
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
		this->mBodyId = PhysicsWorld::getSingleton().createBody(this->mBodyDesc,
			transformComponent->getPosition(), transformComponent->getOrientation());
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
		OFUNC(hasBody)
	OOBJECT_END
}
