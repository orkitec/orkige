/**************************************************************
	created:	2010/08/30 at 21:38
	filename: 	ModelComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_util/NodeUtil.h"

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(ModelComponent, ModelSetEvent);
	IMPL_OWNED_EVENTTYPE(ModelComponent, ModelRemovedEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ModelComponent::ModelComponent()
	{
		this->model = NULL;
		this->sceneNode = NULL;
		this->modelFileName = "";
		this->addDependency<TransformComponent>();
		this->eventData = onew(new StringUtil::StringObject(StringUtil::BLANK));
	}
	//---------------------------------------------------------
	ModelComponent::~ModelComponent()
	{
	}
	//---------------------------------------------------------
	void ModelComponent::loadModel(String const & modelFileName)
	{
		oAssert(!modelFileName.empty());

		this->modelFileName = modelFileName;
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		Ogre::SceneNode const * parentSceneNode = transformComponent->getSceneNode();
		oAssert(parentSceneNode);

		if(this->sceneNode)
		{
			this->removeModel();
		}

		if(modelFileName.empty())
			return;

		this->modelFileName = modelFileName;
		this->model = parentSceneNode->getCreator()->createEntity(componentOwner->getObjectID() + ".ModelComponent." + modelFileName, modelFileName);
		oAssert(this->model);
		this->sceneNode->attachObject(model);
		this->eventData->setValue(modelFileName);
		componentOwner->triggerEvent(Event(ModelComponent::ModelSetEvent, this->eventData));
	}
	//---------------------------------------------------------
	void ModelComponent::removeModel()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		
		NodeUtil::cleanSceneNode(this->sceneNode);
		this->sceneNode->removeAndDestroyAllChildren();

		this->eventData->setValue(this->modelFileName);
		componentOwner->triggerEvent(Event(ModelComponent::ModelRemovedEvent, this->eventData));
		this->model = NULL;
		//this->sceneNode = NULL;
		this->modelFileName = "";
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void ModelComponent::onAdd()
	{
		oAssert(this->modelFileName.empty());
		oAssert(!this->model);
		oAssert(!this->sceneNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		Ogre::SceneNode* node = transformComponent->createChildSceneNode(componentOwner->getObjectID() + ".ModelComponent.sceneNode");
		oAssert(node);
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);
	}
	//---------------------------------------------------------
	void ModelComponent::onRemove()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);

		this->nodeListener->nodeCanBeDestroyed = true;
		
		if(this->sceneNode)
		{
			NodeUtil::cleanSceneNode(this->sceneNode);
			this->sceneNode->removeAndDestroyAllChildren();
			transformComponent->removeAndDestroyChild(this->sceneNode->getName());		
		}

		this->model = NULL;
		this->sceneNode = NULL;
		this->modelFileName = "";
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(ModelComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(loadModel)
		OFUNCCR(getCurrentModelFileName)
		OFUNCIR(getLocalPosition)
		OFUNCIR(getLocalScale)
		OFUNCIR(getLocalOrientation)
		OFUNC(setLocalPosition)
		OFUNC(setLocalScale)
		OFUNC(setLocalOrientation)
	OOBJECT_END
}
