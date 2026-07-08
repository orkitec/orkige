/**************************************************************
	created:	2010/08/30 at 20:01
	filename: 	CameraComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __CameraComponent_h__30_8_2010__20_01_05__
#define __CameraComponent_h__30_8_2010__20_01_05__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include <core_util/FastDelegate.h>

namespace Orkige
{
	class CameraComponent;
	//! camera function definition
	typedef fastdelegate::FastDelegate3<CameraComponent*, Ogre::Real, Ogre::Real, void> CameraModeFunction;
	//! component that can handle a camera attached to to a GameObject
	class ORKIGE_ENGINE_DLL CameraComponent : public GameObjectComponent
	{
		OOBJECT(CameraComponent,GameObjectComponent)
		//--- Types -------------------------------------------
	public:
		//! camera projection selection (2D games use PM_ORTHOGRAPHIC)
		enum ProjectionMode
		{
			PM_PERSPECTIVE = 0,		//!< the classic 3D frustum (default)
			PM_ORTHOGRAPHIC = 1		//!< parallel projection sized by orthoSize
		};
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		Ogre::SceneNode	const *	actorNode;		//!< our actor :)
		Ogre::SceneNode*		controlNode;	//!< node wich controls the whole thing
		Ogre::SceneNode*		sightNode;		//!< "Sight" node - The actor is supposed to be looking here
		Ogre::SceneNode*		cameraNode;	//!< Node for the chase camera
		Ogre::SceneNode*		targetNode;	//!< The camera target
		Ogre::SceneNode*		attachNode;	//!< the node the camera gets attached to
		Ogre::Camera*			camera;			//!< Ogre camera
		CameraModeFunction		cameraFunction;	//!< function that handles camera control (called once per frame)
		ProjectionMode			projectionMode;	//!< perspective (default) or orthographic
		float					orthoSize;		//!< orthographic vertical HALF-extent in world units
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor
		CameraComponent();
		//! destructor
		virtual ~CameraComponent();
		//! @see CameraComponent::controlNode
		inline Ogre::SceneNode *getControlNode();
		//! @see CameraComponent::sightNode
		inline Ogre::SceneNode *getSightNode();
		//! @see CameraComponent::cameraNode
		inline Ogre::SceneNode *getCameraNode();
		//! @see CameraComponent::actorNode
		inline Ogre::SceneNode const * getActorNode();
		//! @see CameraComponent::targetNode
		inline Ogre::SceneNode *getTargetNode();
		//! set CameraModeFunction 
		inline void setMode(CameraModeFunction const & function);
		//! get the current Camera Position
		inline Ogre::Vector3 const & getCameraPosition();
		//! get camera target position
		inline Ogre::Vector3 const & getTargetPosition();
		//! instant set camera
		inline void instantSetCamera(Ogre::Vector3 const & cameraPosition, Ogre::Vector3 const & targetPosition);
		//! set camera with delta smoothing
		inline void setCamera(Ogre::Real timeSinceLastFrame, Ogre::Vector3 const & cameraPosition, Ogre::Vector3 const & targetPosition, Ogre::Real tightness);
		//! @brief select perspective or orthographic projection
		//! @remarks state is kept (and serialized) even while no camera is
		//! attached; applied to the engine camera on attach/load
		void setProjectionMode(ProjectionMode mode);
		//! @see CameraComponent::projectionMode
		inline ProjectionMode getProjectionMode() const;
		//! @brief orthographic vertical half-extent in world units (the camera
		//! sees 2*orthoSize world units of height; width follows the aspect)
		void setOrthoSize(float verticalHalfExtent);
		//! @see CameraComponent::orthoSize
		inline float getOrthoSize() const;
	protected:
		//! push projectionMode/orthoSize onto the Ogre camera (if one is attached)
		void applyProjection();
		//! overridable to update the component
		virtual void onUpdateComponent(float deltaTime);
		//! Component override gets called after the Component is attached to a GameObject
		virtual void onAdd();
		//! Component override gets called before the Component is removed from a GameObject
		virtual void onRemove();
		//--- SERIALIZATION ---
		//! @brief save projection mode + orthoSize
		//! @warning the camera MODE FUNCTION and node offsets do not round-trip
		//! yet (logs a note)
		virtual void save(optr<IArchive> const & ar);
		//! load projection mode + orthoSize (applied when a camera is attached)
		virtual void load(optr<IArchive> const & ar);
	private:
	};
	//---------------------------------------------------------
	inline CameraComponent::ProjectionMode CameraComponent::getProjectionMode() const
	{
		return this->projectionMode;
	}
	//---------------------------------------------------------
	inline float CameraComponent::getOrthoSize() const
	{
		return this->orthoSize;
	}
	//---------------------------------------------------------
	inline Ogre::SceneNode* CameraComponent::getControlNode() 
	{
		return this->controlNode;
	}
	//---------------------------------------------------------	
	inline Ogre::SceneNode* CameraComponent::getSightNode() 
	{
		return this->sightNode;
	}
	//---------------------------------------------------------	
	inline Ogre::SceneNode* CameraComponent::getCameraNode() 
	{
		return this->cameraNode;
	}
	//---------------------------------------------------------
	inline Ogre::SceneNode const * CameraComponent::getActorNode() 
	{
		return this->actorNode;
	}
	//---------------------------------------------------------
	inline Ogre::SceneNode* CameraComponent::getTargetNode() 
	{
		return this->targetNode;
	}
	//---------------------------------------------------------
	inline void CameraComponent::setMode(CameraModeFunction const & function)
	{
		this->cameraFunction = function;
	}
	//---------------------------------------------------------
	inline Ogre::Vector3 const & CameraComponent::getCameraPosition() 
	{
		return this->attachNode->getPosition();
	}
	//---------------------------------------------------------
	inline Ogre::Vector3 const & CameraComponent::getTargetPosition() 
	{
		return this->targetNode->getPosition();
	}
	//---------------------------------------------------------
	inline void CameraComponent::instantSetCamera(Ogre::Vector3 const & cameraPosition, Ogre::Vector3 const & targetPosition) 
	{
		this->attachNode->setPosition(cameraPosition);
		this->targetNode->setPosition(targetPosition);
	}
	//---------------------------------------------------------
	inline void CameraComponent::setCamera(Ogre::Real timeSinceLastFrame, Ogre::Vector3 const & cameraPosition, Ogre::Vector3 const & targetPosition, Ogre::Real tightness) 
	{
		Ogre::Vector3 displacement = (cameraPosition - this->getCameraPosition()) * tightness;
		this->attachNode->translate (displacement);

		displacement = (targetPosition - this->getTargetPosition()) * tightness;
		this->targetNode->translate(displacement);	
	}
	//---------------------------------------------------------
}

#endif //__CameraComponent_h__30_8_2010__20_01_05__
