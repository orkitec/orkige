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
	//! @remarks drives the
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
		//! @brief how the orthographic view reconciles the design rectangle with
		//! the live viewport aspect (2D aspect policy). Mirrors CameraFit::FitMode
		//! for reflection; the math itself lives in core_util/CameraFit.h.
		enum FitMode
		{
			//! honour orthoSize as the half-height; width follows the aspect
			//! (the historical default). designWidth/Height are ignored.
			FM_HEIGHT = 0,
			//! keep the full designWidth on screen; the half-height is derived
			FM_WIDTH = 1,
			//! keep the whole design rectangle visible, growing the slack axis
			FM_EXPAND = 2
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
		FitMode					fitMode;		//!< 2D aspect policy (FM_HEIGHT default)
		float					designWidth;	//!< design rect FULL width (FM_WIDTH/FM_EXPAND)
		float					designHeight;	//!< design rect FULL height (FM_EXPAND)
		float					lastAspect;		//!< last viewport aspect an ortho fit applied at
		//--- smooth follow (2D): tracks a target object's XY, composes with the
		//--- ortho fit (fit sizes the projection, follow moves the position)
		String					followTarget;	//!< GameObject id to follow ("" = off)
		float					followDamping;	//!< smoothing time constant, seconds (0 = snap)
		Vec3					followOffset;	//!< XY lead/trail added to the target (z unused in 2D)
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
		//! @brief select the 2D aspect fit policy (@see FitMode). FM_WIDTH /
		//! FM_EXPAND derive orthoSize from designWidth/Height and the live
		//! viewport aspect; FM_HEIGHT keeps orthoSize as the authored half-height.
		void setFitMode(FitMode mode);
		//! @see CameraComponent::fitMode
		inline FitMode getFitMode() const;
		//! design rectangle FULL width in world units (FM_WIDTH / FM_EXPAND)
		void setDesignWidth(float worldWidth);
		//! @see CameraComponent::designWidth
		inline float getDesignWidth() const;
		//! design rectangle FULL height in world units (FM_EXPAND)
		void setDesignHeight(float worldHeight);
		//! @see CameraComponent::designHeight
		inline float getDesignHeight() const;
		//--- SMOOTH FOLLOW (2D) -----------------------------------------
		//! @brief follow a GameObject: each update the camera eases its XY
		//! toward the target's XY (+ the follow offset) by the framerate-
		//! independent CameraFollow factor. Composes with the fit policy - fit
		//! sizes the ortho projection, follow moves the camera position, so they
		//! never fight. "" stops following (@see stopFollow).
		//! @param targetId the object to track ("" = stop)
		//! @param damping smoothing time constant in seconds (0 = instant snap)
		void follow(String const & targetId, float damping);
		//! stop following (the camera holds its current position)
		void stopFollow();
		//! @brief set the follow target id directly (the reflected setter);
		//! @see follow for the convenience form that also sets damping
		void setFollowTarget(String const & targetId);
		//! @see CameraComponent::followTarget
		inline String const & getFollowTarget() const;
		//! @brief the follow smoothing time constant in seconds (clamped >= 0);
		//! 0 snaps the camera exactly onto the target each frame
		void setFollowDamping(float damping);
		//! @see CameraComponent::followDamping
		inline float getFollowDamping() const;
		//! @brief a constant XY offset added to the target position (camera
		//! lead/trail); the z component is unused in the 2D follow
		void setFollowOffset(Vec3 const & offset);
		//! @see CameraComponent::followOffset
		inline Vec3 const & getFollowOffset() const;
	protected:
		//! @brief the 2D smooth-follow step: re-fetch the follow target by id and
		//! ease the attach node's XY toward it (+ offset), then aim -Z at it.
		//! @return true when a valid target moved the camera this frame (the
		//! caller then skips the chase rig); false when there is no resolvable
		//! target (the camera holds its position and the chase rig runs instead)
		bool updateFollow(float deltaTime);
		//! push projectionMode/orthoSize onto the window camera (if one is attached)
		void applyProjection();
		//! the live viewport aspect (width/height); 1.0 without a render system
		float currentAspect() const;
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
	inline CameraComponent::FitMode CameraComponent::getFitMode() const
	{
		return this->fitMode;
	}
	//---------------------------------------------------------
	inline float CameraComponent::getDesignWidth() const
	{
		return this->designWidth;
	}
	//---------------------------------------------------------
	inline float CameraComponent::getDesignHeight() const
	{
		return this->designHeight;
	}
	//---------------------------------------------------------
	inline String const & CameraComponent::getFollowTarget() const
	{
		return this->followTarget;
	}
	//---------------------------------------------------------
	inline float CameraComponent::getFollowDamping() const
	{
		return this->followDamping;
	}
	//---------------------------------------------------------
	inline Vec3 const & CameraComponent::getFollowOffset() const
	{
		return this->followOffset;
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
