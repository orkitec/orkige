/**************************************************************
	created:	2010/08/31 at 10:44
	filename: 	TransformComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/TransformComponent.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"

#ifdef ORKIGE_RENDER_CLASSIC
// DUAL-TAGGING (transition only): the editor still picks with its own
// Ogre::RaySceneQuery and resolves hits through the LEGACY Ogre::Any user
// binding, so onAdd keeps setting it next to the facade user pointer.
// Engine::getSceneManager is the classic bootstrapper's node lookup.
// WP-A1.4 migrates the editor onto RenderWorld::queryRay +
// RenderNode::findUserPointerUpwards and DELETES this whole block.
#include "engine_graphic/Engine.h"
#endif //ORKIGE_RENDER_CLASSIC

#include <vector>

namespace Orkige
{
	const String TransformComponent::USEROBJECT_BINDING_KEY = "TransformComponent";
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
#ifdef ORKIGE_RENDER_CLASSIC
		// DUAL-TAGGING (see the include note above): keep the legacy
		// Ogre-side user binding alive for the editor's Ogre ray query
		// until WP-A1.4 - both tags carry the same `this`
		Engine::getSingleton().getSceneManager()->getSceneNode(nodeName)
			->getUserObjectBindings().setUserAny(
				TransformComponent::USEROBJECT_BINDING_KEY, Ogre::Any(this));
#endif //ORKIGE_RENDER_CLASSIC
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);
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
#ifdef ORKIGE_RENDER_CLASSIC
	TransformComponent* TransformComponent::getComponentFromNode(Ogre::Node const * node, bool traverseParents)
	{
		oAssert(node);
		TransformComponent *tc = NULL;

		Ogre::Any const & any = node->getUserObjectBindings().getUserAny(TransformComponent::USEROBJECT_BINDING_KEY);
		// OGRE 14: Any::isEmpty() is deprecated in favour of std::any style has_value()
		if(any.has_value())
		{
			tc = Ogre::any_cast<TransformComponent*>(any);
		}

		if(traverseParents && (tc == NULL) && (node->getParent() != NULL))
		{
			tc = TransformComponent::getComponentFromNode(node->getParent(), traverseParents);
		}
		return tc;
	}
#endif //ORKIGE_RENDER_CLASSIC
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
	OOBJECT_END
}
