/**************************************************************
	created:	2010/08/30 at 20:01
	filename: 	CameraComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __CameraComponent_h__30_8_2010__20_01_05__
#define __CameraComponent_h__30_8_2010__20_01_05__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_render/RenderNode.h"
#include "engine_render/RenderCamera.h"
#include <core_util/FastDelegate.h>

namespace Orkige
{
	class CameraComponent;
	//! camera function definition
	typedef fastdelegate::FastDelegate3<CameraComponent*, Real, Real, void> CameraModeFunction;
	//! @brief component that can handle a camera attached to a GameObject
	//! @remarks Phase A1 (Docs/render-abstraction.md, WP-A1.2): drives the
	//! facade window camera (RenderSystem::getWindowCamera) on a rig of
	//! facade RenderNodes. The historical Ogre::SceneNode auto-tracking was
	//! replaced by an explicit per-update lookAt at the target node - same
	//! behavior, one facade call, portable to every backend.
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
		optr<RenderNode>		actorNode;		//!< our actor :) (the sibling TransformComponent's node, not owned)
		optr<RenderNode>		controlNode;	//!< node wich controls the whole thing
		optr<RenderNode>		sightNode;		//!< "Sight" node - The actor is supposed to be looking here
		optr<RenderNode>		cameraNode;		//!< Node for the chase camera
		optr<RenderNode>		targetNode;		//!< The camera target
		optr<RenderNode>		attachNode;		//!< the node the camera gets attached to
		optr<RenderCamera>		camera;			//!< the window camera while attached
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
		inline optr<RenderNode> const & getControlNode();
		//! @see CameraComponent::sightNode
		inline optr<RenderNode> const & getSightNode();
		//! @see CameraComponent::cameraNode
		inline optr<RenderNode> const & getCameraNode();
		//! @see CameraComponent::actorNode
		inline optr<RenderNode> const & getActorNode();
		//! @see CameraComponent::targetNode
		inline optr<RenderNode> const & getTargetNode();
		//! set CameraModeFunction
		inline void setMode(CameraModeFunction const & function);
		//! get the current Camera Position
		inline Vec3 const & getCameraPosition();
		//! get camera target position
		inline Vec3 const & getTargetPosition();
		//! instant set camera
		inline void instantSetCamera(Vec3 const & cameraPosition, Vec3 const & targetPosition);
		//! set camera with delta smoothing
		inline void setCamera(Real timeSinceLastFrame, Vec3 const & cameraPosition, Vec3 const & targetPosition, Real tightness);
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
		//! push projectionMode/orthoSize onto the window camera (if one is attached)
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
	inline optr<RenderNode> const & CameraComponent::getControlNode()
	{
		return this->controlNode;
	}
	//---------------------------------------------------------
	inline optr<RenderNode> const & CameraComponent::getSightNode()
	{
		return this->sightNode;
	}
	//---------------------------------------------------------
	inline optr<RenderNode> const & CameraComponent::getCameraNode()
	{
		return this->cameraNode;
	}
	//---------------------------------------------------------
	inline optr<RenderNode> const & CameraComponent::getActorNode()
	{
		return this->actorNode;
	}
	//---------------------------------------------------------
	inline optr<RenderNode> const & CameraComponent::getTargetNode()
	{
		return this->targetNode;
	}
	//---------------------------------------------------------
	inline void CameraComponent::setMode(CameraModeFunction const & function)
	{
		this->cameraFunction = function;
	}
	//---------------------------------------------------------
	inline Vec3 const & CameraComponent::getCameraPosition()
	{
		return this->attachNode->getPosition();
	}
	//---------------------------------------------------------
	inline Vec3 const & CameraComponent::getTargetPosition()
	{
		return this->targetNode->getPosition();
	}
	//---------------------------------------------------------
	inline void CameraComponent::instantSetCamera(Vec3 const & cameraPosition, Vec3 const & targetPosition)
	{
		this->attachNode->setPosition(cameraPosition);
		this->targetNode->setPosition(targetPosition);
	}
	//---------------------------------------------------------
	inline void CameraComponent::setCamera(Real timeSinceLastFrame, Vec3 const & cameraPosition, Vec3 const & targetPosition, Real tightness)
	{
		Vec3 displacement = (cameraPosition - this->getCameraPosition()) * tightness;
		this->attachNode->translate(displacement);

		displacement = (targetPosition - this->getTargetPosition()) * tightness;
		this->targetNode->translate(displacement);
	}
	//---------------------------------------------------------
}

#endif //__CameraComponent_h__30_8_2010__20_01_05__
