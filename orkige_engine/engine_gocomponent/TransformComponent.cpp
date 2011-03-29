/**************************************************************
	created:	2010/08/31 at 10:44
	filename: 	TransformComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "engine_gocomponent/TransformComponent.h"
#include "engine_module/EnginePrerequisites.h"
#include "engine_graphic/Engine.h"
#include "engine_util/NodeUtil.h"

using namespace Ogre;
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
		oAssert(!this->sceneNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		String const & componentOwnerObjectId = componentOwner->getObjectID();
		oAssert(!componentOwnerObjectId.empty());
		Ogre::SceneNode* node = Engine::getSingleton().getSceneManager()->getRootSceneNode()->createChildSceneNode(componentOwnerObjectId +".TransformComponent");
		node->getUserObjectBindings().setUserAny(TransformComponent::USEROBJECT_BINDING_KEY, Ogre::Any(this));
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);
	}
	//---------------------------------------------------------
	void TransformComponent::onRemove()
	{
		oAssert(this->sceneNode);
		oAssert(this->nodeListener);
		this->nodeListener->nodeCanBeDestroyed = true;
		this->detachTransformComponents(this->getSceneNode());
		NodeUtil::wipeSceneNode(this->sceneNode);
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	TransformComponent* TransformComponent::getComponentFromNode(Ogre::Node const * node, bool traverseParents)
	{
		oAssert(node);
		TransformComponent *tc = NULL;

		Ogre::Any const & any = node->getUserObjectBindings().getUserAny(TransformComponent::USEROBJECT_BINDING_KEY);
		if(!any.isEmpty())
		{
			tc = Ogre::any_cast<TransformComponent*>(any);
		}
		
		
		if(traverseParents && (tc == NULL) && (node->getParent() != NULL))
		{
			tc = TransformComponent::getComponentFromNode(node->getParent(), traverseParents);
		}
		return tc;
	}
	//---------------------------------------------------------
	void TransformComponent::detachTransformComponents(const Ogre::Node* node, bool traverseChildren)
	{

		TransformComponent* trans = 0;
		Ogre::Node* nextNode = 0;

		for (unsigned int each = 0; each < node->numChildren(); each++)
		{

			nextNode = node->getChild(each);
			
			if (nextNode)
			{

				trans = this->getComponentFromNode(nextNode, false);
				
				if (trans)
				{
					trans->attachToNode(trans->getSceneManager()->getRootSceneNode());
				}
				else if (traverseChildren)
				{
					this->detachTransformComponents(nextNode, traverseChildren);
				}
			}

		}

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
