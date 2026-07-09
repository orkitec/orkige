/********************************************************************
	created:	Thursday 2010/11/18 at 19:04
	filename: 	SceneNodeGuard.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SceneNodeGuard_h__18_11_2010__19_04_10__
#define __SceneNodeGuard_h__18_11_2010__19_04_10__

#include "engine_module/EnginePrerequisites.h"
#include "engine_render/RenderNode.h"
#include "engine_render/RenderMath.h"
#include <core_event/EventManager.h>

namespace Orkige
{
	//! @brief the components' node-owner base: holds ONE owned facade
	//! RenderNode and forwards the transform surface components expose
	//! @remarks the facade reshape (Docs/render-abstraction.md): the
	//! historical class mirrored ~60 Ogre::SceneNode methods around a raw
	//! node pointer plus an Ogre::Node::Listener; components hand-rolled
	//! NodeUtil wipe chains in onRemove. The facade handle is RAII - the
	//! guard now only carries the ~15 methods components and their callers
	//! actually use (audit in the doc) and dropping the handle detaches and
	//! destroys the backend node. The whole class is scheduled for deletion
	//! once components hold their optr<RenderNode> directly.
	class ORKIGE_ENGINE_DLL SceneNodeGuard
	{
		//--- Types -------------------------------------------------
	public:
		//! @brief historical per-frame "node graph updated" notification
		//! @warning NOT emitted since the A1 facade reshape: the render
		//! facade has no per-node update callback (and no consumer ever
		//! registered for this event). Declared so registrations keep
		//! compiling; wire a facade node listener if a consumer appears.
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(NodeUpdatedEvent);
		//! triggered when the node is attached to another node (attachToNode)
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(NodeAttachedEvent);
		//! triggered when the node is detached from its parent (attachToNode)
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(NodeDetachedEvent);
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		optr<RenderNode>	mNode;			//!< the owned transform node (NULL while detached)
		EventManager*		mEventManager;	//!< receives the node events or NULL
		Object*				mEventData;		//!< payload of the node events (the owning component)
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		SceneNodeGuard();
		//! destructor - drops the handle (RAII destroys the backend node)
		virtual ~SceneNodeGuard();
		//! the owned facade node (NULL while the component is detached)
		inline optr<RenderNode> const & getNode() const;
		//! get position relative to the parent node
		inline Vec3 const & getPosition() const;
		//! get position in the world
		inline Vec3 getWorldPosition() const;
		//! get orientation relative to the parent node
		inline Quat const & getOrientation() const;
		//! get the orientation in the world
		inline Quat getWorldOrientation() const;
		//! get scale
		inline Vec3 const & getScale() const;
		//! set position
		inline void setPosition(Vec3 const & position);
		//! set scale
		inline void setScale(Vec3 const & scale);
		//! set orientation
		inline void setOrientation(Quat const & orientation);
		//! @see RenderNode::translate
		inline void translate(Vec3 const & delta, RenderNode::TransformSpace relativeTo = RenderNode::TS_PARENT);
		//! @see RenderNode::yaw
		inline void yaw(Radian const & angle, RenderNode::TransformSpace relativeTo = RenderNode::TS_LOCAL);
		//! @see RenderNode::pitch
		inline void pitch(Radian const & angle, RenderNode::TransformSpace relativeTo = RenderNode::TS_LOCAL);
		//! @see RenderNode::roll
		inline void roll(Radian const & angle, RenderNode::TransformSpace relativeTo = RenderNode::TS_LOCAL);
		//! @see RenderNode::lookAt
		inline void lookAt(Vec3 const & targetPoint, RenderNode::TransformSpace relativeTo, Vec3 const & localDirection = Vec3::NEGATIVE_UNIT_Z);
		//! @see RenderNode::setDirection
		inline void setDirection(Vec3 const & direction, RenderNode::TransformSpace relativeTo = RenderNode::TS_LOCAL, Vec3 const & localDirection = Vec3::NEGATIVE_UNIT_Z);
		//! @see RenderNode::setFixedYawAxis
		inline void setFixedYawAxis(bool useFixed, Vec3 const & fixedAxis = Vec3::UNIT_Y);
		//! @see RenderNode::setVisible
		inline void setVisible(bool visible, bool cascade = true);
		//! world-space bounds of everything attached at and below the node
		inline AABB getWorldAABB() const;
		//! create a child node of the owned node (empty name = generated)
		inline optr<RenderNode> createChildNode(String const & name = "");
		//! re-parent the owned node (triggers NodeDetachedEvent + NodeAttachedEvent)
		void attachToNode(optr<RenderNode> const & parent);
	protected:
		//! adopt an owned facade node and the optional event wiring
		void initSceneNodeGuard(optr<RenderNode> const & node, EventManager* eventManager = NULL, Object* eventData = NULL);
		//! drop the handle (destroys the backend node) and the event wiring
		void deinitSceneNodeGuard();
	private:
	};
	//---------------------------------------------------------
	inline optr<RenderNode> const & SceneNodeGuard::getNode() const
	{
		return this->mNode;
	}
	//---------------------------------------------------------
	inline Vec3 const & SceneNodeGuard::getPosition() const
	{
		return this->mNode->getPosition();
	}
	//---------------------------------------------------------
	inline Vec3 SceneNodeGuard::getWorldPosition() const
	{
		return this->mNode->getWorldPosition();
	}
	//---------------------------------------------------------
	inline Quat const & SceneNodeGuard::getOrientation() const
	{
		return this->mNode->getOrientation();
	}
	//---------------------------------------------------------
	inline Quat SceneNodeGuard::getWorldOrientation() const
	{
		return this->mNode->getWorldOrientation();
	}
	//---------------------------------------------------------
	inline Vec3 const & SceneNodeGuard::getScale() const
	{
		return this->mNode->getScale();
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setPosition(Vec3 const & position)
	{
		this->mNode->setPosition(position);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setScale(Vec3 const & scale)
	{
		this->mNode->setScale(scale);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setOrientation(Quat const & orientation)
	{
		this->mNode->setOrientation(orientation);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::translate(Vec3 const & delta, RenderNode::TransformSpace relativeTo)
	{
		this->mNode->translate(delta, relativeTo);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::yaw(Radian const & angle, RenderNode::TransformSpace relativeTo)
	{
		this->mNode->yaw(angle, relativeTo);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::pitch(Radian const & angle, RenderNode::TransformSpace relativeTo)
	{
		this->mNode->pitch(angle, relativeTo);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::roll(Radian const & angle, RenderNode::TransformSpace relativeTo)
	{
		this->mNode->roll(angle, relativeTo);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::lookAt(Vec3 const & targetPoint, RenderNode::TransformSpace relativeTo, Vec3 const & localDirection)
	{
		this->mNode->lookAt(targetPoint, relativeTo, localDirection);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setDirection(Vec3 const & direction, RenderNode::TransformSpace relativeTo, Vec3 const & localDirection)
	{
		this->mNode->setDirection(direction, relativeTo, localDirection);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setFixedYawAxis(bool useFixed, Vec3 const & fixedAxis)
	{
		this->mNode->setFixedYawAxis(useFixed, fixedAxis);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setVisible(bool visible, bool cascade)
	{
		this->mNode->setVisible(visible, cascade);
	}
	//---------------------------------------------------------
	inline AABB SceneNodeGuard::getWorldAABB() const
	{
		return this->mNode->getWorldBounds();
	}
	//---------------------------------------------------------
	inline optr<RenderNode> SceneNodeGuard::createChildNode(String const & name)
	{
		return this->mNode->createChild(name);
	}
	//---------------------------------------------------------
}

#endif //__SceneNodeGuard_h__18_11_2010__19_04_10__
