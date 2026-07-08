/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	RenderNodeNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderNodeNext.cpp
//! @brief Ogre-Next implementation of the RenderNode facade
//! @remarks the v2 SceneNode carries the same call surface as
//! classic; differences handled here: transforms are stored SoA and
//! returned by value (cached for the facade's const-ref getters),
//! derived getters use the *Updated variants (v2 updates transforms in
//! bulk per frame), child nodes are created nameless. getWorldBounds
//! merges the world Aabbs of all attached objects in the subtree (v2
//! has no per-node world AABB like classic's _getWorldAABB - the
//! recursive merge IS the classic semantic).

#include "engine_render_next/NextBackend.h"

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreMovableObject.h>

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

		//! v2 derived transforms update in bulk per frame; the relative
		//! operations below (translate/rotate/lookAt in non-local spaces)
		//! read them IMMEDIATELY and hard-assert on a dirty cache in debug
		//! builds - force the parent-chain refresh first (the facade
		//! contract: node ops are valid at any time, same as classic)
		void forceTransformUpdate(Ogre::SceneNode* node)
		{
			node->_getDerivedPositionUpdated();
		}

		//! merge the world Aabbs of every object attached under node
		void mergeWorldBounds(Ogre::SceneNode* node, Ogre::AxisAlignedBox & into)
		{
			for(size_t each = 0; each < node->numAttachedObjects(); ++each)
			{
				const Ogre::Aabb worldAabb =
					node->getAttachedObject(each)->getWorldAabbUpdated();
				if(worldAabb.mHalfSize != Ogre::Vector3::ZERO ||
					worldAabb.mCenter != Ogre::Vector3::ZERO)
				{
					into.merge(Ogre::AxisAlignedBox(worldAabb.getMinimum(),
						worldAabb.getMaximum()));
				}
			}
			for(size_t each = 0; each < node->numChildren(); ++each)
			{
				mergeWorldBounds(
					static_cast<Ogre::SceneNode*>(node->getChild(each)), into);
			}
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
		// same late-destruction guard as classic (script-held handles may
		// outlive the render system; then only facade memory is freed)
		if(this->mImpl->node && RenderBackend::system())
		{
			RenderBackend::unregisterNode(this->mImpl->node);
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
		this->mImpl->positionCache = this->mImpl->node->getPosition();
		return this->mImpl->positionCache;
	}
	//---------------------------------------------------------
	void RenderNode::setPosition(Vec3 const & position)
	{
		this->mImpl->node->setPosition(position);
	}
	//---------------------------------------------------------
	Quat const & RenderNode::getOrientation() const
	{
		this->mImpl->orientationCache = this->mImpl->node->getOrientation();
		return this->mImpl->orientationCache;
	}
	//---------------------------------------------------------
	void RenderNode::setOrientation(Quat const & orientation)
	{
		this->mImpl->node->setOrientation(orientation);
	}
	//---------------------------------------------------------
	Vec3 const & RenderNode::getScale() const
	{
		this->mImpl->scaleCache = this->mImpl->node->getScale();
		return this->mImpl->scaleCache;
	}
	//---------------------------------------------------------
	void RenderNode::setScale(Vec3 const & scale)
	{
		this->mImpl->node->setScale(scale);
	}
	//---------------------------------------------------------
	Vec3 RenderNode::getWorldPosition() const
	{
		// v2 derived transforms update in bulk per frame - the *Updated
		// variant forces the parent-chain refresh so the getter is
		// correct at any time (facade contract, same as classic)
		return this->mImpl->node->_getDerivedPositionUpdated();
	}
	//---------------------------------------------------------
	Quat RenderNode::getWorldOrientation() const
	{
		return this->mImpl->node->_getDerivedOrientationUpdated();
	}
	//---------------------------------------------------------
	AABB RenderNode::getWorldBounds() const
	{
		AABB bounds;	// starts null; stays null for content-free subtrees
		mergeWorldBounds(this->mImpl->node, bounds);
		return bounds;
	}
	//---------------------------------------------------------
	void RenderNode::translate(Vec3 const & delta, TransformSpace relativeTo)
	{
		forceTransformUpdate(this->mImpl->node);
		this->mImpl->node->translate(delta, toOgreSpace(relativeTo));
	}
	//---------------------------------------------------------
	void RenderNode::yaw(Radian const & angle, TransformSpace relativeTo)
	{
		forceTransformUpdate(this->mImpl->node);
		this->mImpl->node->yaw(angle, toOgreSpace(relativeTo));
	}
	//---------------------------------------------------------
	void RenderNode::pitch(Radian const & angle, TransformSpace relativeTo)
	{
		forceTransformUpdate(this->mImpl->node);
		this->mImpl->node->pitch(angle, toOgreSpace(relativeTo));
	}
	//---------------------------------------------------------
	void RenderNode::roll(Radian const & angle, TransformSpace relativeTo)
	{
		forceTransformUpdate(this->mImpl->node);
		this->mImpl->node->roll(angle, toOgreSpace(relativeTo));
	}
	//---------------------------------------------------------
	void RenderNode::lookAt(Vec3 const & targetPoint, TransformSpace relativeTo,
		Vec3 const & localDirection)
	{
		forceTransformUpdate(this->mImpl->node);
		this->mImpl->node->lookAt(targetPoint, toOgreSpace(relativeTo),
			localDirection);
	}
	//---------------------------------------------------------
	void RenderNode::setDirection(Vec3 const & direction,
		TransformSpace relativeTo, Vec3 const & localDirection)
	{
		forceTransformUpdate(this->mImpl->node);
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
		Ogre::SceneNode* child =
			this->mImpl->node->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		child->setName(name.empty()
			? RenderBackend::generateName("OrkigeNode") : name);
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
		if(this->mImpl->node->getParentSceneNode())
		{
			this->mImpl->node->getParentSceneNode()->removeChild(
				this->mImpl->node);
		}
		backendParent->addChild(this->mImpl->node);
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
