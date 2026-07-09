/**************************************************************
	created:	2010/08/31 at 10:44
	filename: 	TransformComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/RigidBodyComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include <core_game/GameObjectManager.h>

#include <vector>

namespace Orkige
{
	//---------------------------------------------------------
	//! world-space scale of a facade node: local scales composed through the
	//! parent chain (component-wise, matching the backends' inherit-scale)
	static Vec3 TransformNodeWorldScale(optr<RenderNode> const & node)
	{
		Vec3 scale = node->getScale();
		for(optr<RenderNode> parent = node->getParent(); parent; parent = parent->getParent())
		{
			scale = scale * parent->getScale();
		}
		return scale;
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	TransformComponent::TransformComponent()
	{
	}
	//---------------------------------------------------------
	TransformComponent::~TransformComponent()
	{
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void TransformComponent::onAdd()
	{
		oAssert(!this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		String const & componentOwnerObjectId = componentOwner->getObjectID();
		oAssert(!componentOwnerObjectId.empty());
		RenderSystem* renderSystem = RenderSystem::get();
		oAssert(renderSystem);
		const String nodeName = componentOwnerObjectId + ".TransformComponent";
		optr<RenderNode> node = renderSystem->getWorld()->createNode(nodeName);
		oAssert(node);
		// the facade back-mapping: ray query hits and editors resolve a node
		// to its component through this (@see getComponentFromNode)
		node->setUserPointer(this);
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);
		// an owner that is already parented in the GameObject tree (runtime
		// addComponent after setParent) starts under its parent's node
		if(!componentOwner->getParentId().empty())
		{
			this->attachToNode(this->resolveParentNode());
		}
	}
	//---------------------------------------------------------
	void TransformComponent::onRemove()
	{
		oAssert(this->mNode);
		// child transforms of OTHER GameObjects survive under the world root;
		// content components (Model/Sprite/...) were removed before us by the
		// dependency order and took their child nodes with them (RAII)
		this->detachTransformComponents(this->getNode());
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	Vec3 TransformComponent::getWorldScale() const
	{
		oAssert(this->mNode);
		return TransformNodeWorldScale(this->mNode);
	}
	//---------------------------------------------------------
	void TransformComponent::setWorldPosition(Vec3 const & worldPosition)
	{
		oAssert(this->mNode);
		optr<RenderNode> parent = this->mNode->getParent();
		if(!parent)
		{
			// attached to the world root: local IS world
			this->setPosition(worldPosition);
			return;
		}
		Vec3 localPosition = parent->getWorldOrientation().Inverse() *
			(worldPosition - parent->getWorldPosition());
		localPosition = localPosition / TransformNodeWorldScale(parent);
		this->setPosition(localPosition);
	}
	//---------------------------------------------------------
	void TransformComponent::setWorldOrientation(Quat const & worldOrientation)
	{
		oAssert(this->mNode);
		optr<RenderNode> parent = this->mNode->getParent();
		if(!parent)
		{
			this->setOrientation(worldOrientation);
			return;
		}
		this->setOrientation(parent->getWorldOrientation().Inverse() * worldOrientation);
	}
	//---------------------------------------------------------
	void TransformComponent::teleport(Vec3 const & worldPosition, Quat const & worldOrientation)
	{
		this->setWorldPosition(worldPosition);
		this->setWorldOrientation(worldOrientation);
		// bodies live in world space - snap the whole subtree's collision
		// geometry to the nodes' new world poses (works while paused)
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		TransformComponent::syncSubtreeBodies(componentOwner);
	}
	//---------------------------------------------------------
	void TransformComponent::syncSubtreeBodies(GameObject * gameObject)
	{
		oAssert(gameObject);
		PhysicsWorld* physicsWorld = PhysicsWorld::getSingletonPtr();
		if(physicsWorld && physicsWorld->isInitialized() &&
			gameObject->hasComponent<RigidBodyComponent>() &&
			gameObject->hasComponent<TransformComponent>())
		{
			RigidBodyComponent* body = gameObject->getComponentPtr<RigidBodyComponent>();
			TransformComponent* transform = gameObject->getComponentPtr<TransformComponent>();
			if(body->hasBody() && transform->getNode())
			{
				physicsWorld->setBodyTransform(body->getBodyId(),
					transform->getWorldPosition(), transform->getWorldOrientation());
				if(body->getBodyType() != PhysicsWorld::BT_STATIC)
				{
					physicsWorld->setLinearVelocity(body->getBodyId(), Vec3::ZERO);
					physicsWorld->setAngularVelocity(body->getBodyId(), Vec3::ZERO);
				}
			}
		}
		GameObjectManager & manager = GameObjectManager::getSingleton();
		StringVector const children = manager.getChildren(gameObject->getObjectID());
		foreach(String const & childId, children)
		{
			if(optr<GameObject> child = manager.getGameObject(childId).lock())
			{
				TransformComponent::syncSubtreeBodies(child.get());
			}
		}
	}
	//---------------------------------------------------------
	TransformComponent* TransformComponent::getComponentFromNode(optr<RenderNode> const & node, bool traverseParents)
	{
		oAssert(node);
		void* owner = traverseParents
			? node->findUserPointerUpwards()
			: node->getUserPointer();
		// within the engine only TransformComponent tags scene nodes
		return static_cast<TransformComponent*>(owner);
	}
	//---------------------------------------------------------
	void TransformComponent::onParentChanged(GameObject * newParent, bool keepWorldTransform)
	{
		if(!this->mNode)
		{
			return;
		}
		optr<RenderNode> parentNode = this->resolveParentNode();
		oAssert(parentNode);
		if(!keepWorldTransform)
		{
			// scene loading: the serialized LOCAL transform is authoritative
			// (RenderNode::setParent keeps the local TRS on both backends)
			this->attachToNode(parentNode);
			return;
		}
		// Unity semantics: preserve the WORLD transform by recomputing the
		// local one relative to the new parent
		const Vec3 worldPosition = this->getWorldPosition();
		const Quat worldOrientation = this->getWorldOrientation();
		const Vec3 worldScale = this->getWorldScale();
		this->attachToNode(parentNode);
		const Vec3 parentWorldPosition = parentNode->getWorldPosition();
		const Quat parentWorldOrientation = parentNode->getWorldOrientation();
		const Vec3 parentWorldScale = TransformNodeWorldScale(parentNode);
		const Quat inverseParentOrientation = parentWorldOrientation.Inverse();
		Vec3 localPosition = inverseParentOrientation *
			(worldPosition - parentWorldPosition);
		localPosition = localPosition / parentWorldScale;
		this->setPosition(localPosition);
		this->setOrientation(inverseParentOrientation * worldOrientation);
		this->setScale(worldScale / parentWorldScale);
	}
	//---------------------------------------------------------
	optr<RenderNode> TransformComponent::resolveParentNode()
	{
		optr<RenderNode> rootNode = RenderSystem::get()->getWorld()->getRootNode();
		GameObject* componentOwner = this->getComponentOwner();
		if(!componentOwner)
		{
			return rootNode;
		}
		optr<GameObject> parent = componentOwner->getParent().lock();
		if(!parent)
		{
			return rootNode;
		}
		if(!parent->hasComponent<TransformComponent>())
		{
			// a transform-less parent still groups logically; the node math
			// runs through the world root (parent space == world space)
			oDebugMsg("engine",0,"TransformComponent: parent GameObject "
				<< parent->getObjectID() << " has no TransformComponent - "
				<< componentOwner->getObjectID() << " keeps world-space coordinates");
			return rootNode;
		}
		TransformComponent* parentTransform = parent->getComponentPtr<TransformComponent>();
		oAssert(parentTransform);
		optr<RenderNode> parentNode = parentTransform->getNode();
		return parentNode ? parentNode : rootNode;
	}
	//---------------------------------------------------------
	void TransformComponent::detachTransformComponents(optr<RenderNode> const & node, bool traverseChildren)
	{
		// snapshot the children - re-parenting mutates the child list
		std::vector<optr<RenderNode>> children;
		for(size_t each = 0; each < node->numChildren(); ++each)
		{
			if(optr<RenderNode> child = node->getChild(each))
			{
				children.push_back(child);
			}
		}
		optr<RenderNode> rootNode = RenderSystem::get()->getWorld()->getRootNode();
		for(optr<RenderNode> const & child : children)
		{
			TransformComponent* trans = TransformComponent::getComponentFromNode(child, false);
			if(trans)
			{
				trans->attachToNode(rootNode);
			}
			else if(traverseChildren)
			{
				this->detachTransformComponents(child, traverseChildren);
			}
		}
	}
	//---------------------------------------------------------
	void TransformComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// the node hierarchy below this transform (child nodes, attached
		// content) is owned by other components (e.g. ModelComponent)
		// which serialize their own state - only the local transform is saved
		Vec3 position = this->getPosition();
		Quat orientation = this->getOrientation();
		Vec3 scale = this->getScale();
		ar << position.x << position.y << position.z;
		ar << orientation.w << orientation.x << orientation.y << orientation.z;
		ar << scale.x << scale.y << scale.z;
	}
	//---------------------------------------------------------
	void TransformComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		Vec3 position;
		Quat orientation;
		Vec3 scale;
		ar >> position.x >> position.y >> position.z;
		ar >> orientation.w >> orientation.x >> orientation.y >> orientation.z;
		ar >> scale.x >> scale.y >> scale.z;
		this->setPosition(position);
		this->setOrientation(orientation);
		this->setScale(scale);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(TransformComponent)
		GAMEOBJECTCOMPONENT()
		OFUNCIR(getPosition)
		OFUNCIR(getScale)
		OFUNCIR(getOrientation)
		OFUNC(setPosition)
		OFUNC(setScale)
		OFUNC(setOrientation)
		OFUNC(getWorldPosition)
		OFUNC(getWorldOrientation)
		OFUNC(getWorldScale)
		OFUNC(setWorldPosition)
		OFUNC(setWorldOrientation)
		OFUNC(teleport)
		// reflected local-transform schema (task #94, P1): the same LOCAL
		// position/orientation/scale the hand-written save/load persists (that
		// stays untouched in P1 - serialization migration is P2). The Vec3/Quat
		// adapters resolve to the engine-side overloads in
		// ComponentPropertyReflect.h.
		OPROPERTY("position", Orkige::PropertyKind::Vec3, getPosition, setPosition, Orkige::PROP_NONE)
		OPROPERTY("orientation", Orkige::PropertyKind::Quat, getOrientation, setOrientation, Orkige::PROP_NONE)
		OPROPERTY("scale", Orkige::PropertyKind::Vec3, getScale, setScale, Orkige::PROP_NONE)
	OOBJECT_END
}
