/**************************************************************
	created:	2010/08/30 at 20:36
	filename: 	CameraComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_gocomponent/CameraComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include <core_game/GameObject.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_graphic/Engine.h"
#include "engine_gocomponent/CameraDefaultModes.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	CameraComponent::CameraComponent()
	{
		this->actorNode = NULL;
		this->controlNode = NULL;
		this->sightNode = NULL;
		this->cameraNode = NULL;
		this->targetNode = NULL;
		this->addDependency<TransformComponent>();
	}
	//---------------------------------------------------------
	CameraComponent::~CameraComponent()
	{
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool CameraComponent::onFrameStarted(Event const & event)
	{
		optr<FrameEventData> data = event.getDataPtr<FrameEventData>();
		this->cameraFunction(this,data->timeSinceLastFrame,1.0f);
		return false;
	}
	//---------------------------------------------------------
	void CameraComponent::onAdd()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		Ogre::SceneNode const * sceneNode = transformComponent->getSceneNode();
		oAssert(sceneNode);

		this->actorNode = sceneNode;
		oAssert(this->actorNode);

		String const & componentOwnerObjectId = componentOwner->getObjectID();
		oAssert(!componentOwnerObjectId.empty());

		this->controlNode = transformComponent->createChildSceneNode (componentOwnerObjectId + "_control", Ogre::Vector3 (0, 1, 0)); //probably somewhe in the head
		this->sightNode = this->controlNode->createChildSceneNode (componentOwnerObjectId + "_sight", Ogre::Vector3 (0, 0, 1));
		this->cameraNode = this->controlNode->createChildSceneNode (componentOwnerObjectId + "_camera", Ogre::Vector3 (0, 0, -85));
		this->attachNode = this->actorNode->getCreator()->getRootSceneNode()->createChildSceneNode(componentOwnerObjectId + "_attach");
		this->targetNode = this->actorNode->getCreator()->getRootSceneNode()->createChildSceneNode(componentOwnerObjectId + "_target");

		this->attachNode->setAutoTracking (true, this->targetNode); // The camera will always look at the camera target
		this->attachNode->setFixedYawAxis (true); // Needed because of auto tracking

		this->camera = Engine::getSingleton().getCamera();

		this->camera->detachFromParent();
		this->attachNode->attachObject(this->camera);
		this->setMode(CameraDefaultModes::ThirdPersonChaseCamera);
		this->registerEvent(Engine::FrameStartedEvent,&CameraComponent::onFrameStarted,this);
	}
	//---------------------------------------------------------
	void CameraComponent::onRemove()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		this->unregisterEvent(Engine::FrameStartedEvent);
		this->controlNode->removeAndDestroyAllChildren();
		transformComponent->removeChild(this->controlNode->getName());
		this->actorNode->getCreator()->destroySceneNode(this->controlNode->getName());
		this->attachNode->detachAllObjects();
		this->actorNode->getCreator()->destroySceneNode(this->attachNode->getName());
		this->actorNode->getCreator()->destroySceneNode(this->targetNode->getName());
		this->actorNode = NULL;
		this->controlNode = NULL;
		this->sightNode = NULL;
		this->cameraNode = NULL;
		this->targetNode = NULL;
		this->camera = NULL;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(CameraComponent)
		GAMEOBJECTCOMPONENT()
	OOBJECT_END
}
