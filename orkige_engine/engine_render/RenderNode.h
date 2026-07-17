/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	RenderNode.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderNode_h__8_7_2026__12_00_00__
#define __RenderNode_h__8_7_2026__12_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include <core_util/String.h>

namespace Orkige
{
	//! @brief a handle to one transform-hierarchy node of the render scene
	//! @remarks The facade successor of engine_util/SceneNodeGuard, sized by
	//! the audit of actual call sites (Docs/render-abstraction.md) - the
	//! unused 60% of the old SceneNode mirror is deliberately not carried
	//! over. Handles are optr-shared; the handle OWNS the backend node:
	//! destroying the last optr detaches the node from its parent and
	//! destroys it. A node must outlive the content (MeshInstance,
	//! SpriteQuad, ...) attached to it and its child handles - components
	//! already guarantee that order via onAdd/onRemove.
	//!
	//! Backend mapping (whole class): classic = Ogre::SceneNode;
	//! next = Ogre::SceneNode (v2 - same calls, but transforms are updated
	//! in bulk per frame, so derived getters may need updateAllTransforms);
	//! filament = utils::Entity + TransformManager (single local mat4 -
	//! the impl composes/decomposes TRS; hierarchy via setParent).
	class ORKIGE_ENGINE_DLL RenderNode
	{
		//--- Types -------------------------------------------------
	public:
		//! space a relative transform operates in
		//! map: classic/next=Ogre::Node::TransformSpace | filament=impl composes the matrix itself
		enum TransformSpace
		{
			TS_LOCAL = 0,	//!< relative to own orientation
			TS_PARENT,		//!< relative to the parent node
			TS_WORLD		//!< relative to world axes
		};
	protected:
		//! backend state - defined only inside the selected backend
		struct Impl;
		//! the selected backend's plumbing (@see RenderPrerequisites.h)
		friend struct RenderBackend;
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Impl*	mImpl;	//!< backend node guts, never NULL after creation
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor - detaches from the parent and destroys the backend node
		//! map: classic/next=SceneNode::removeAndDestroyChild via parent | filament=TransformManager::destroy+EntityManager::destroy
		~RenderNode();

		//--- local transform ---
		//! position relative to the parent
		//! map: classic/next=Ogre::Node::getPosition | filament=decompose cached local TRS
		Vec3 const & getPosition() const;
		//! map: classic/next=Ogre::Node::setPosition | filament=TransformManager::setTransform(recomposed TRS)
		void setPosition(Vec3 const & position);
		//! map: classic/next=Ogre::Node::getOrientation | filament=cached local TRS
		Quat const & getOrientation() const;
		//! map: classic/next=Ogre::Node::setOrientation | filament=TransformManager::setTransform(recomposed TRS)
		void setOrientation(Quat const & orientation);
		//! map: classic/next=Ogre::Node::getScale | filament=cached local TRS
		Vec3 const & getScale() const;
		//! map: classic/next=Ogre::Node::setScale | filament=TransformManager::setTransform(recomposed TRS)
		void setScale(Vec3 const & scale);

		//--- derived (world) transform ---
		//! map: classic=Node::_getDerivedPosition | next=same after updateAllTransforms | filament=getWorldTransform column 3
		Vec3 getWorldPosition() const;
		//! map: classic=Node::_getDerivedOrientation | next=same caveat | filament=decompose getWorldTransform
		Quat getWorldOrientation() const;
		//! @brief world-space scale (local scales composed component-wise
		//! through the ancestor chain - the backends' inherit-scale rule)
		//! map: classic=Node::_getDerivedScale | next=_getDerivedScaleUpdated | filament=decompose getWorldTransform
		Vec3 getWorldScale() const;
		//! @brief world-space bounds of everything attached below this node
		//! map: classic=SceneNode::_getWorldAABB | next=MovableObject::getWorldAabb per attached item | filament=RenderableManager::getAxisAlignedBoundingBox transformed
		AABB getWorldBounds() const;

		//--- relative transform operations ---
		//! map: classic/next=Ogre::Node::translate | filament=impl-side vector math on the TRS
		void translate(Vec3 const & delta, TransformSpace relativeTo = TS_PARENT);
		//! map: classic/next=Ogre::Node::yaw | filament=impl-side quat math
		void yaw(Radian const & angle, TransformSpace relativeTo = TS_LOCAL);
		//! map: classic/next=Ogre::Node::pitch | filament=impl-side quat math
		void pitch(Radian const & angle, TransformSpace relativeTo = TS_LOCAL);
		//! map: classic/next=Ogre::Node::roll | filament=impl-side quat math
		void roll(Radian const & angle, TransformSpace relativeTo = TS_LOCAL);
		//! @brief orient -Z (or localDirection) towards a world/parent/local point
		//! map: classic/next=SceneNode::lookAt | filament=impl-side lookAt quat
		void lookAt(Vec3 const & targetPoint, TransformSpace relativeTo,
			Vec3 const & localDirection = Vec3::NEGATIVE_UNIT_Z);
		//! map: classic/next=SceneNode::setDirection | filament=impl-side quat from direction
		void setDirection(Vec3 const & direction, TransformSpace relativeTo = TS_LOCAL,
			Vec3 const & localDirection = Vec3::NEGATIVE_UNIT_Z);
		//! @brief keep yaw rotations around a fixed axis (FPS-style cameras)
		//! map: classic/next=SceneNode::setFixedYawAxis | filament=impl flag steering the quat helpers above
		void setFixedYawAxis(bool useFixed, Vec3 const & fixedAxis = Vec3::UNIT_Y);

		//--- hierarchy ---
		//! @brief create a child node (empty name = backend-generated)
		//! map: classic/next=SceneManager/SceneNode::createChildSceneNode | filament=EntityManager::create+TransformManager::create(parent)
		optr<RenderNode> createChild(String const & name = "");
		//! @brief re-parent this node (detaches from the current parent first)
		//! map: classic/next=Node::removeChild+addChild | filament=TransformManager::setParent
		void setParent(optr<RenderNode> const & parent);
		//! @brief parent handle; NULL when attached to the world root
		//! @remarks the facade graph mirrors parent links (woptr inside), so
		//! navigation never touches backend child lists
		//! map: facade-side bookkeeping (all backends)
		optr<RenderNode> getParent() const;
		//! number of child nodes created through this handle's facade graph
		//! map: facade-side bookkeeping (all backends)
		size_t numChildren() const;
		//! child handle by index (facade graph order) or NULL
		//! map: facade-side bookkeeping (all backends)
		optr<RenderNode> getChild(size_t index) const;

		//--- visibility ---
		//! @brief show/hide all content attached at (and below) this node
		//! map: classic/next=SceneNode::setVisible(cascade) | filament=Scene::remove/addEntity per renderable
		void setVisible(bool visible, bool cascade = true);

		//--- mobility (the static flag) ---
		//! @brief declare this node's WORLD transform immutable so the backend
		//! can take its native immobile fast path. THE MOBILITY CONTRACT:
		//! "static means static" - a static node is expected never to move
		//! again. A later transform mutation on a static node stays CORRECT
		//! but is a contract violation: the backend logs one warning per node
		//! and lands the move through its costly repair path (next: an
		//! explicit static-dirty notify re-derives the frozen transforms;
		//! classic: baked mesh content DEMOTES out of its StaticGeometry
		//! region - one region rebuild - and renders individually again).
		//! Children created after the flag inherit it (createChild); child
		//! nodes existing BEFORE the flag are not touched - callers cascade
		//! explicitly (TransformComponent owns the per-object cascade).
		//! map: classic=facade bookkeeping; attached mesh content becomes
		//! StaticGeometry-bake eligible (see StaticBakeClassic.cpp) |
		//! next=SceneNode::setStatic - the node and its attached objects move
		//! into the SCENE_STATIC memory managers, which skip per-frame
		//! transform derivation and cull prep | filament=no per-object static
		//! path (transforms are only recomputed when set) - facade cache only
		void setStatic(bool isStatic);
		//! @see RenderNode::setStatic
		bool isStatic() const;

		//--- back-mapping (picking, editor hierarchy) ---
		//! @brief attach an engine-side owner to the node - ray query results
		//! and editors resolve a hit node back to its TransformComponent this
		//! way (successor of the Ogre::Any/UserObjectBindings trick)
		//! map: facade-side bookkeeping (all backends)
		void setUserPointer(void * owner);
		//! @see RenderNode::setUserPointer - walks no parents; use
		//! findUserPointerUpwards for the "traverseParents" behavior
		void * getUserPointer() const;
		//! first non-NULL user pointer on the path from this node to the root
		void * findUserPointerUpwards() const;
	protected:
		//! nodes are created by RenderWorld::createNode / RenderNode::createChild only
		RenderNode();
	private:
		RenderNode(RenderNode const &);				// non-copyable
		RenderNode & operator=(RenderNode const &);	// non-copyable
	};
}

#endif //__RenderNode_h__8_7_2026__12_00_00__
