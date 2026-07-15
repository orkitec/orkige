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
#include "engine_gocomponent/ScriptComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include <core_script/ScriptEventBus.h>
#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>
#include <core_game/SceneSerializer.h>
#include <core_util/StringUtil.h>

#include "engine_module/EnginePrerequisites.h"

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(RigidBodyComponent, ContactBeganEvent);
	IMPL_OWNED_EVENTTYPE(RigidBodyComponent, ContactEndedEvent);
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
	void RigidBodyComponent::setLayer(String const & layer)
	{
		if (this->hasBody())
		{
			oDebugWarning(false, "RigidBodyComponent::setLayer ignored - body already created");
			return;
		}
		this->mBodyDesc.layer = layer;
	}
	//---------------------------------------------------------
	void RigidBodyComponent::setIsSensor(bool isSensor)
	{
		if (this->hasBody())
		{
			// mIsSensor is a creation-time flag in Jolt (no live toggle in v1)
			oDebugWarning(false, "RigidBodyComponent::setIsSensor ignored - body already created");
			return;
		}
		this->mBodyDesc.isSensor = isSensor;
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
	GameObject* RigidBodyComponent::bodyOwner(PhysicsWorld & physicsWorld,
		PhysicsWorld::BodyId bodyId)
	{
		// getBodyUserData returns 0 for a destroyed body (its tag was erased in
		// destroyBody), so a stale id never dereferences a dead object. The tag
		// is a RigidBodyComponent*; a non-zero tag means the body still lives,
		// hence the component (which owns the body) still lives too.
		const PhysicsWorld::BodyUserData tag =
			physicsWorld.getBodyUserData(bodyId);
		if (tag == 0)
		{
			return NULL;
		}
		return reinterpret_cast<RigidBodyComponent*>(tag)->getGameObject();
	}
	//---------------------------------------------------------
	void RigidBodyComponent::dispatchContacts(GameObjectManager & gameObjectManager)
	{
		PhysicsWorld* physicsWorld = PhysicsWorld::getSingletonPtr();
		if (!physicsWorld || !physicsWorld->isInitialized())
		{
			return;
		}
		// resolve a body's user tag to its owning GameObject (@see bodyOwner)
		auto ownerOf = [physicsWorld](PhysicsWorld::BodyId bodyId) -> GameObject*
		{
			return RigidBodyComponent::bodyOwner(*physicsWorld, bodyId);
		};
		(void)gameObjectManager;	// resolution goes through the body tag, not a scan
		for (PhysicsWorld::ContactEvent const & contact :
			physicsWorld->getFrameContacts())
		{
			GameObject* objectA = ownerOf(contact.bodyA);
			GameObject* objectB = ownerOf(contact.bodyB);
			// v1: both sides must resolve to a live object - so a script hook
			// never receives a nil `other` and a dead object is never delivered
			// to (the stale-id contract). One-sided delivery is future work.
			if (!objectA || !objectB)
			{
				continue;
			}
			RigidBodyComponent::deliverContact(objectA, objectB, contact.began);
			RigidBodyComponent::deliverContact(objectB, objectA, contact.began);
			// (3) MIRROR onto the message bus (additive; the bespoke per-object
			// onContactBegin/onContactEnd hooks above are untouched): ONE
			// physics.contactBegin / physics.contactEnd per contact pair, ids in
			// the payload. This drain runs AFTER the script phase in the tick
			// order, so the bus event is delivered at the NEXT frame's drain -
			// the bespoke hook is same-frame, the bus mirror is one frame later.
			ScriptEventPayload payload;
			payload.setString("a", objectA->getObjectID());
			payload.setString("b", objectB->getObjectID());
			ScriptEventBus::getSingleton().emit(
				contact.began ? "physics.contactBegin" : "physics.contactEnd",
				payload);
		}
	}
	//---------------------------------------------------------
	void RigidBodyComponent::deliverContact(GameObject* self, GameObject* other,
		bool began)
	{
		oAssert(self && other);
		// (1) the C++ event (AnimationComponent triggerEvent pattern): native
		// components and the debug protocol observe contacts without scripting.
		// The data carries the OTHER object's id.
		optr<StringUtil::StringObject> data =
			onew(new StringUtil::StringObject(other->getObjectID()));
		self->triggerEvent(Event(began ?
			RigidBodyComponent::ContactBeganEvent :
			RigidBodyComponent::ContactEndedEvent, data));
		// (2) the optional Lua hook onContactBegin/onContactEnd(self, other) -
		// gated inside dispatchContact (no-op without a healthy loaded script).
		// A script mutating the world here goes through the GameObjectManager
		// delete queue, so it is safe even mid-dispatch. EVERY script on the
		// object hears the contact (an object may carry several behavior scripts).
		for (ScriptComponent* script : ScriptComponent::collectFrom(*self))
		{
			script->dispatchContact(other, began);
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
		// bodies are created at the WORLD pose (local == world for roots). The
		// body is tagged with THIS component pointer so the contact drain can map
		// a colliding body back to its owning GameObject; the tag is cleared in
		// destroyBody, so a stale (destroyed-body) contact resolves to no owner.
		this->mBodyId = PhysicsWorld::getSingleton().createBody(this->mBodyDesc,
			transformComponent->getWorldPosition(), transformComponent->getWorldOrientation(),
			reinterpret_cast<PhysicsWorld::BodyUserData>(this));
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
		// reflection-driven NAMED serialization: every BodyDesc
		// creation parameter is written by name off the declared schema. The old
		// positional trailing-field version guard (layer / isSensor read-the-last-
		// element hacks) is GONE - a missing named field simply keeps its default.
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void RigidBodyComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		if (this->hasBody())
		{
			// the body is created lazily on the first update, so a load right
			// after addComponent (the SceneSerializer path) lands here before
			// creation; recreate defensively (and so the desc setters apply)
			// if someone loads into a live one
			this->destroyBody();
		}
		SceneSerializer::loadComponentProperties(ar, *this);
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
		OFUNC(setLayer)
		OFUNC(getLayer)
		OFUNC(setIsSensor)
		OFUNC(isSensor)
		OFUNC(setLinearVelocity)
		OFUNC(getLinearVelocity)
		OFUNC(setAngularVelocity)
		OFUNC(getAngularVelocity)
		OFUNC(applyImpulse)
		OFUNC(applyForce)
		OFUNC(teleport)
		OFUNC(hasBody)
		OFUNC(getBodyId)
		// neutral enum value<->label tables so the reflected
		// bodyType/shapeType enums resolve labels in every scripting config
		OENUM_REGISTER_START("PhysicsBodyType", PhysicsWorld::BodyType)
			OENUM_REGISTER_VALUE(BT_STATIC)
			OENUM_REGISTER_VALUE(BT_KINEMATIC)
			OENUM_REGISTER_VALUE(BT_DYNAMIC)
		OENUM_REGISTER_END
		OENUM_REGISTER_START("PhysicsShapeType", PhysicsWorld::ShapeType)
			OENUM_REGISTER_VALUE(ST_BOX)
			OENUM_REGISTER_VALUE(ST_SPHERE)
			OENUM_REGISTER_VALUE(ST_CAPSULE)
		OENUM_REGISTER_END
		// reflected BodyDesc schema: the full creation-parameter set
		OPROPERTY_ENUM("bodyType", "PhysicsBodyType", getBodyType, setBodyType, Orkige::PROP_NONE)
		OPROPERTY_ENUM("shapeType", "PhysicsShapeType", getShapeType, setShapeType, Orkige::PROP_NONE)
		OPROPERTY("halfExtents", Orkige::PropertyKind::Vec3, getHalfExtents, setHalfExtents, Orkige::PROP_NONE)
		OPROPERTY("radius", Orkige::PropertyKind::Float, getRadius, setRadiusValue, Orkige::PROP_NONE)
		OPROPERTY("halfHeight", Orkige::PropertyKind::Float, getHalfHeight, setHalfHeightValue, Orkige::PROP_NONE)
		OPROPERTY("mass", Orkige::PropertyKind::Float, getMass, setMass, Orkige::PROP_NONE)
		OPROPERTY("friction", Orkige::PropertyKind::Float, getFriction, setFriction, Orkige::PROP_NONE)
		OPROPERTY("restitution", Orkige::PropertyKind::Float, getRestitution, setRestitution, Orkige::PROP_NONE)
		OPROPERTY("planar", Orkige::PropertyKind::Bool, getPlanarMode, setPlanarMode, Orkige::PROP_NONE)
		OPROPERTY("layer", Orkige::PropertyKind::String, getLayer, setLayer, Orkige::PROP_NONE)
		OPROPERTY("isSensor", Orkige::PropertyKind::Bool, isSensor, setIsSensor, Orkige::PROP_NONE)
		// runtime telemetry: reflected but TRANSIENT (never serialized - live
		// physics state). Velocity is read/write (the accessors no-op / return
		// zero without a created body, so a generic set is always safe); has_body
		// is read-only. These keep the debug protocol's rigid-body readout (and
		// its only pre-reflection writable fields) flowing off the ONE registry.
		OPROPERTY("linear_velocity", Orkige::PropertyKind::Vec3, getLinearVelocity, setLinearVelocity, Orkige::PROP_TRANSIENT)
		OPROPERTY("angular_velocity", Orkige::PropertyKind::Vec3, getAngularVelocity, setAngularVelocity, Orkige::PROP_TRANSIENT)
		OPROPERTY_RO("has_body", Orkige::PropertyKind::Bool, hasBody, Orkige::PROP_TRANSIENT)

		// self.rigidbody / world.getRigidBody(id) hand Lua a WEAK handle: locks
		// per call, raises an honest error naming the owner once the body's
		// object is gone (never a raw pointer). @see TransformComponent.
		OWEAKHANDLE_BEGIN(Orkige::RigidBodyComponent, "RigidBodyComponentHandle", "component handle", "component")
			OWEAKHANDLE_BASEMETHOD(setBodyType)
			OWEAKHANDLE_BASEMETHOD(setBoxShape)
			OWEAKHANDLE_BASEMETHOD(setSphereShape)
			OWEAKHANDLE_BASEMETHOD(setCapsuleShape)
			OWEAKHANDLE_BASEMETHOD(setMass)
			OWEAKHANDLE_BASEMETHOD(setFriction)
			OWEAKHANDLE_BASEMETHOD(setRestitution)
			OWEAKHANDLE_BASEMETHOD(setPlanarMode)
			OWEAKHANDLE_BASEMETHOD(getPlanarMode)
			OWEAKHANDLE_BASEMETHOD(setLayer)
			OWEAKHANDLE_BASEMETHOD(getLayer)
			OWEAKHANDLE_BASEMETHOD(setIsSensor)
			OWEAKHANDLE_BASEMETHOD(isSensor)
			OWEAKHANDLE_BASEMETHOD(setLinearVelocity)
			OWEAKHANDLE_BASEMETHOD(getLinearVelocity)
			OWEAKHANDLE_BASEMETHOD(setAngularVelocity)
			OWEAKHANDLE_BASEMETHOD(getAngularVelocity)
			OWEAKHANDLE_BASEMETHOD(applyImpulse)
			OWEAKHANDLE_BASEMETHOD(applyForce)
			OWEAKHANDLE_BASEMETHOD(teleport)
			OWEAKHANDLE_BASEMETHOD(hasBody)
			OWEAKHANDLE_BASEMETHOD(getBodyId)
		OWEAKHANDLE_END
	OOBJECT_END
}
