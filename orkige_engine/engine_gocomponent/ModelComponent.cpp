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
		this->modelNode = NULL;
		this->modelFileName = "";
		this->addDependency<TransformComponent>();
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
		Ogre::SceneNode* parentSceneNode = transformComponent->getSceneNode();
		oAssert(parentSceneNode);

		if(this->modelNode)
		{
			this->removeModel();
		}

		if(modelFileName.empty())
			return;

		this->model = parentSceneNode->getCreator()->createEntity(componentOwner->getObjectID() + ".ModelComponent." + modelFileName, modelFileName);
		oAssert(this->model);
		this->modelNode->attachObject(model);
		componentOwner->triggerEvent(Event(ModelComponent::ModelSetEvent));
	}
	//---------------------------------------------------------
	void ModelComponent::removeModel()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		
		NodeUtil::cleanSceneNode(this->modelNode);
		this->modelNode->removeAndDestroyAllChildren();

		componentOwner->triggerEvent(Event(ModelComponent::ModelRemovedEvent));
		this->model = NULL;
		//this->modelNode = NULL;
		this->modelFileName = "";
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void ModelComponent::onAdd()
	{
		oAssert(this->modelFileName.empty());
		oAssert(!this->model);
		oAssert(!this->modelNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		Ogre::SceneNode* parentSceneNode = transformComponent->getSceneNode();
		oAssert(parentSceneNode);
		this->modelNode = parentSceneNode->createChildSceneNode(componentOwner->getObjectID() + ".ModelComponent.modelNode");
		oAssert(this->modelNode);
	}
	//---------------------------------------------------------
	void ModelComponent::onRemove()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		Ogre::SceneNode* parentSceneNode = transformComponent->getSceneNode();
		oAssert(parentSceneNode);

		if(this->modelNode)
		{
			NodeUtil::cleanSceneNode(this->modelNode);
			this->modelNode->removeAndDestroyAllChildren();
			parentSceneNode->removeAndDestroyChild(this->modelNode->getName());		
		}

		this->model = NULL;
		this->modelNode = NULL;
		this->modelFileName = "";
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
