/**************************************************************
	created:	2010/08/30 at 20:36
	filename: 	CameraComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "engine_gocomponent/CameraComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include <core_game/GameObject.h>
#include "engine_graphic/Engine.h"
#include "engine_gocomponent/CameraDefaultModes.h"

#include <algorithm>

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
		this->attachNode = NULL;
		this->camera = NULL;
		this->projectionMode = CameraComponent::PM_PERSPECTIVE;
		this->orthoSize = 5.0f;
		this->addDependency<TransformComponent>();
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------
	CameraComponent::~CameraComponent()
	{
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void CameraComponent::onUpdateComponent(float deltaTime)
	{
		this->cameraFunction(this,deltaTime,1.0f);
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
		this->applyProjection();
	}
	//---------------------------------------------------------
	void CameraComponent::onRemove()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		// hand the engine camera back the way we found it
		if(this->camera && this->projectionMode != CameraComponent::PM_PERSPECTIVE)
		{
			this->camera->setProjectionType(Ogre::PT_PERSPECTIVE);
		}
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
	void CameraComponent::setProjectionMode(ProjectionMode mode)
	{
		this->projectionMode = mode;
		this->applyProjection();
	}
	//---------------------------------------------------------
	void CameraComponent::setOrthoSize(float verticalHalfExtent)
	{
		oDebugWarning(verticalHalfExtent > 0.0f,
			"CameraComponent::setOrthoSize wants a positive half-extent");
		this->orthoSize = std::max(verticalHalfExtent, 0.001f);
		this->applyProjection();
	}
	//---------------------------------------------------------
	void CameraComponent::applyProjection()
	{
		if(!this->camera)
		{
			return;	// detached: the state applies on onAdd/load
		}
		if(this->projectionMode == CameraComponent::PM_ORTHOGRAPHIC)
		{
			this->camera->setProjectionType(Ogre::PT_ORTHOGRAPHIC);
			// setOrthoWindowHeight keeps the camera's aspect ratio - the
			// window width follows the viewport automatically
			this->camera->setOrthoWindowHeight(this->orthoSize * 2.0f);
		}
		else
		{
			this->camera->setProjectionType(Ogre::PT_PERSPECTIVE);
		}
	}
	//---------------------------------------------------------
	// @TODO(scene format v2): serialize the camera mode FUNCTION and the
	// camera/sight node offsets - projection mode and orthoSize round-trip
	void CameraComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		int mode = static_cast<int>(this->projectionMode);
		ar << mode << this->orthoSize;
		oDebugMsg("scene",0,"CameraComponent: camera mode function and node offsets are not serialized yet");
	}
	//---------------------------------------------------------
	void CameraComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		int mode = 0;
		ar >> mode >> this->orthoSize;
		this->projectionMode = static_cast<CameraComponent::ProjectionMode>(mode);
		this->applyProjection();
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(CameraComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(setProjectionMode)
		OFUNC(getProjectionMode)
		OFUNC(setOrthoSize)
		OFUNC(getOrthoSize)
		OENUM_START(ProjectionMode)
			OENUM_VALUE(PM_PERSPECTIVE)
			OENUM_VALUE(PM_ORTHOGRAPHIC)
		OENUM_END
	OOBJECT_END
}
