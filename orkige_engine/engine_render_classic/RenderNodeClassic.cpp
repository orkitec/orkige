/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	RenderNodeClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderNodeClassic.cpp
//! @brief classic-OGRE implementation of the RenderNode facade
//! @remarks the handle OWNS its Ogre::SceneNode (RAII successor of the
//! NodeUtil destroy dances); parent/child navigation is mirrored on the
//! facade side, the registry in ClassicBackend backs the reverse lookups

#include "engine_render_classic/ClassicBackend.h"

#include <algorithm>

namespace Orkige
{
	namespace
	{
		//! facade -> backend transform space
		Ogre::Node::TransformSpace toOgreSpace(RenderNode::TransformSpace space)
		{
			switch(space)
			{
			case RenderNode::TS_LOCAL:	return Ogre::Node::TS_LOCAL;
			case RenderNode::TS_PARENT:	return Ogre::Node::TS_PARENT;
			case RenderNode::TS_WORLD:	return Ogre::Node::TS_WORLD;
			}
			return Ogre::Node::TS_PARENT;
		}
	}
	//---------------------------------------------------------
	optr<RenderNode> RenderBackend::wrapNode(Ogre::SceneNode* node, bool owned,
		optr<RenderNode> const & parent)
	{
		oAssert(node);
		optr<RenderNode> handle(new RenderNode());
		handle->mImpl->node = node;
		handle->mImpl->creator = node->getCreator();
		handle->mImpl->owned = owned;
		handle->mImpl->parent = parent;
		if(parent)
		{
			parent->mImpl->children.push_back(handle);
		}
		RenderBackend::registerNode(node, handle);
		return handle;
	}
	//---------------------------------------------------------
	RenderNode::RenderNode()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	RenderNode::~RenderNode()
	{
		if(this->mImpl->node)
		{
			RenderBackend::unregisterNode(this->mImpl->node);
			// drop this handle from the parent's facade child list
			if(optr<RenderNode> parent = this->mImpl->parent.lock())
			{
				std::erase_if(parent->mImpl->children,
					[this](woptr<RenderNode> const & child)
					{
						optr<RenderNode> locked = child.lock();
						return !locked || locked.get() == this;
					});
			}
			if(this->mImpl->owned)
			{
				// contract: content handles and child handles are gone by
				// now (they hold owning optrs on this node); detach
				// defensively and destroy the backend node
				this->mImpl->node->detachAllObjects();
				if(this->mImpl->node->getParentSceneNode())
				{
					this->mImpl->node->getParentSceneNode()->removeChild(
						this->mImpl->node);
				}
				this->mImpl->creator->destroySceneNode(this->mImpl->node);
			}
		}
		delete this->mImpl;
	}
	//---------------------------------------------------------
	Vec3 const & RenderNode::getPosition() const
	{
		return this->mImpl->node->getPosition();
	}
	//---------------------------------------------------------
	void RenderNode::setPosition(Vec3 const & position)
	{
		this->mImpl->node->setPosition(position);
	}
	//---------------------------------------------------------
	Quat const & RenderNode::getOrientation() const
	{
		return this->mImpl->node->getOrientation();
	}
	//---------------------------------------------------------
	void RenderNode::setOrientation(Quat const & orientation)
	{
		this->mImpl->node->setOrientation(orientation);
	}
	//---------------------------------------------------------
	Vec3 const & RenderNode::getScale() const
	{
		return this->mImpl->node->getScale();
	}
	//---------------------------------------------------------
	void RenderNode::setScale(Vec3 const & scale)
	{
		this->mImpl->node->setScale(scale);
	}
	//---------------------------------------------------------
	Vec3 RenderNode::getWorldPosition() const
	{
		// classic caches derived transforms and only marks the CHANGED
		// node dirty - a child's cache goes stale until the per-frame
		// scene-graph traversal. Force the parent-chain refresh so the
		// facade getter is correct at any time, not just after a frame
		// (the parent chain itself resolves lazily inside _updateFromParent).
		this->mImpl->node->_update(false, true);
		return this->mImpl->node->_getDerivedPosition();
	}
	//---------------------------------------------------------
	Quat RenderNode::getWorldOrientation() const
	{
		this->mImpl->node->_update(false, true);	// see getWorldPosition
		return this->mImpl->node->_getDerivedOrientation();
	}
	//---------------------------------------------------------
	AABB RenderNode::getWorldBounds() const
	{
		// world AABBs are only refreshed by the render traversal; merge
		// them explicitly so the value is valid between frames too
		this->mImpl->node->_update(true, true);
		return this->mImpl->node->_getWorldAABB();
	}
	//---------------------------------------------------------
	void RenderNode::translate(Vec3 const & delta, TransformSpace relativeTo)
	{
		this->mImpl->node->translate(delta, toOgreSpace(relativeTo));
	}
	//---------------------------------------------------------
	void RenderNode::yaw(Radian const & angle, TransformSpace relativeTo)
	{
		this->mImpl->node->yaw(angle, toOgreSpace(relativeTo));
	}
	//---------------------------------------------------------
	void RenderNode::pitch(Radian const & angle, TransformSpace relativeTo)
	{
		this->mImpl->node->pitch(angle, toOgreSpace(relativeTo));
	}
	//---------------------------------------------------------
	void RenderNode::roll(Radian const & angle, TransformSpace relativeTo)
	{
		this->mImpl->node->roll(angle, toOgreSpace(relativeTo));
	}
	//---------------------------------------------------------
	void RenderNode::lookAt(Vec3 const & targetPoint, TransformSpace relativeTo,
		Vec3 const & localDirection)
	{
		this->mImpl->node->lookAt(targetPoint, toOgreSpace(relativeTo),
			localDirection);
	}
	//---------------------------------------------------------
	void RenderNode::setDirection(Vec3 const & direction,
		TransformSpace relativeTo, Vec3 const & localDirection)
	{
		this->mImpl->node->setDirection(direction, toOgreSpace(relativeTo),
			localDirection);
	}
	//---------------------------------------------------------
	void RenderNode::setFixedYawAxis(bool useFixed, Vec3 const & fixedAxis)
	{
		this->mImpl->node->setFixedYawAxis(useFixed, fixedAxis);
	}
	//---------------------------------------------------------
	optr<RenderNode> RenderNode::createChild(String const & name)
	{
		Ogre::SceneNode* child = name.empty()
			? this->mImpl->node->createChildSceneNode()
			: this->mImpl->node->createChildSceneNode(name);
		// own handle via the registry (facade classes are not
		// enable_shared_from_this by design - handles stay plain)
		return RenderBackend::wrapNode(child, true,
			RenderBackend::findNode(this->mImpl->node));
	}
	//---------------------------------------------------------
	void RenderNode::setParent(optr<RenderNode> const & parent)
	{
		oAssert(parent);
		oAssert(parent.get() != this);
		Ogre::SceneNode* backendParent = parent->mImpl->node;
		if(this->mImpl->node->getParentSceneNode() == backendParent)
		{
			return;
		}
		// backend re-parent (world transform follows the new parent - the
		// LOCAL transform is kept, matching Ogre::Node::addChild semantics)
		if(this->mImpl->node->getParentSceneNode())
		{
			this->mImpl->node->getParentSceneNode()->removeChild(
				this->mImpl->node);
		}
		backendParent->addChild(this->mImpl->node);
		// facade graph bookkeeping
		optr<RenderNode> self = RenderBackend::findNode(this->mImpl->node);
		if(optr<RenderNode> previousParent = this->mImpl->parent.lock())
		{
			std::erase_if(previousParent->mImpl->children,
				[this](woptr<RenderNode> const & child)
				{
					optr<RenderNode> locked = child.lock();
					return !locked || locked.get() == this;
				});
		}
		this->mImpl->parent = parent;
		parent->mImpl->children.push_back(self);
	}
	//---------------------------------------------------------
	optr<RenderNode> RenderNode::getParent() const
	{
		optr<RenderNode> parent = this->mImpl->parent.lock();
		// contract: NULL when attached to the world root
		if(parent && !parent->mImpl->owned)
		{
			return optr<RenderNode>();
		}
		return parent;
	}
	//---------------------------------------------------------
	size_t RenderNode::numChildren() const
	{
		std::erase_if(this->mImpl->children,
			[](woptr<RenderNode> const & child) { return child.expired(); });
		return this->mImpl->children.size();
	}
	//---------------------------------------------------------
	optr<RenderNode> RenderNode::getChild(size_t index) const
	{
		std::erase_if(this->mImpl->children,
			[](woptr<RenderNode> const & child) { return child.expired(); });
		if(index >= this->mImpl->children.size())
		{
			return optr<RenderNode>();
		}
		return this->mImpl->children[index].lock();
	}
	//---------------------------------------------------------
	void RenderNode::setVisible(bool visible, bool cascade)
	{
		this->mImpl->node->setVisible(visible, cascade);
	}
	//---------------------------------------------------------
	void RenderNode::setUserPointer(void * owner)
	{
		this->mImpl->userPointer = owner;
	}
	//---------------------------------------------------------
	void * RenderNode::getUserPointer() const
	{
		return this->mImpl->userPointer;
	}
	//---------------------------------------------------------
	void * RenderNode::findUserPointerUpwards() const
	{
		return RenderBackend::findUserPointerUpwards(this->mImpl->node);
	}
}
