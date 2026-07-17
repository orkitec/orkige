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
#include <core_debug/DebugMacros.h>

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

		//! drop every baked entity attached at or below node out of the
		//! static bake (the classic demotion: it renders individually and
		//! follows its node again; ONE deferred region rebuild covers all)
		void demoteStaticSubtree(Ogre::SceneNode* node)
		{
			for(size_t each = 0; each < node->numAttachedObjects(); ++each)
			{
				Ogre::MovableObject* attached = node->getAttachedObject(each);
				if(attached->getMovableType() == "Entity")
				{
					RenderBackend::staticBakeUnregister(
						static_cast<Ogre::Entity*>(attached));
				}
			}
			for(size_t each = 0; each < node->numChildren(); ++each)
			{
				demoteStaticSubtree(
					static_cast<Ogre::SceneNode*>(node->getChild(each)));
			}
		}
	}
	//---------------------------------------------------------
	void RenderNode::Impl::noteStaticMutation(char const * operation)
	{
		if(!this->isStatic)
		{
			return;
		}
		if(!this->staticMoveWarned)
		{
			oDebugWarn("engine", 0, "RenderNode: " << operation
				<< " on STATIC node '" << this->node->getName()
				<< "' - static means static; its baked mesh content demotes "
				"out of the StaticGeometry regions (one rebuild) and renders "
				"individually again. Clear the static flag on objects that "
				"move.");
			this->staticMoveWarned = true;
		}
		demoteStaticSubtree(this->node);
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
		// late destruction guard: script-held handles (Lua userdata
		// keeps the optr alive until the Lua state closes) may legally
		// outlive the render system - once destroyRenderSystem ran, the
		// backend scene died with the Ogre root and only the facade memory
		// is freed here
		if(this->mImpl->node && RenderBackend::system())
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
		this->mImpl->noteStaticMutation("setPosition");
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
		this->mImpl->noteStaticMutation("setOrientation");
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
		this->mImpl->noteStaticMutation("setScale");
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
	Vec3 RenderNode::getWorldScale() const
	{
		this->mImpl->node->_update(false, true);	// see getWorldPosition
		return this->mImpl->node->_getDerivedScale();
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
		this->mImpl->noteStaticMutation("translate");
	}
	//---------------------------------------------------------
	void RenderNode::yaw(Radian const & angle, TransformSpace relativeTo)
	{
		this->mImpl->node->yaw(angle, toOgreSpace(relativeTo));
		this->mImpl->noteStaticMutation("yaw");
	}
	//---------------------------------------------------------
	void RenderNode::pitch(Radian const & angle, TransformSpace relativeTo)
	{
		this->mImpl->node->pitch(angle, toOgreSpace(relativeTo));
		this->mImpl->noteStaticMutation("pitch");
	}
	//---------------------------------------------------------
	void RenderNode::roll(Radian const & angle, TransformSpace relativeTo)
	{
		this->mImpl->node->roll(angle, toOgreSpace(relativeTo));
		this->mImpl->noteStaticMutation("roll");
	}
	//---------------------------------------------------------
	void RenderNode::lookAt(Vec3 const & targetPoint, TransformSpace relativeTo,
		Vec3 const & localDirection)
	{
		this->mImpl->node->lookAt(targetPoint, toOgreSpace(relativeTo),
			localDirection);
		this->mImpl->noteStaticMutation("lookAt");
	}
	//---------------------------------------------------------
	void RenderNode::setDirection(Vec3 const & direction,
		TransformSpace relativeTo, Vec3 const & localDirection)
	{
		this->mImpl->node->setDirection(direction, toOgreSpace(relativeTo),
			localDirection);
		this->mImpl->noteStaticMutation("setDirection");
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
		optr<RenderNode> handle = RenderBackend::wrapNode(child, true,
			RenderBackend::findNode(this->mImpl->node));
		// children inherit the mobility flag (@see RenderNode::setStatic):
		// content attached to them registers with the static bake
		handle->mImpl->isStatic = this->mImpl->isStatic;
		return handle;
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
		// re-parenting changes the world transform - a mobility-contract
		// violation on a static node (warned + demoted like a move)
		this->mImpl->noteStaticMutation("setParent");
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
		if(this->mImpl->isStatic)
		{
			// baked geometry ignores the source entity's visible flag - a
			// visibility change on a static object re-filters the regions at
			// the next flush (membership follows entity->getVisible()). No
			// warning: the inactive-at-load apply lands here legitimately.
			RenderBackend::staticBakeMarkDirty();
		}
	}
	//---------------------------------------------------------
	void RenderNode::setStatic(bool isStatic)
	{
		if(this->mImpl->isStatic == isStatic)
		{
			return;
		}
		this->mImpl->isStatic = isStatic;
		this->mImpl->staticMoveWarned = false;
		// membership sweep over the DIRECTLY attached objects: entities on a
		// newly-static node join the bake, entities on a newly-dynamic node
		// leave it (child nodes are the caller's cascade; content attached
		// later registers through MeshInstance::attachTo)
		for(size_t each = 0; each < this->mImpl->node->numAttachedObjects();
			++each)
		{
			Ogre::MovableObject* attached =
				this->mImpl->node->getAttachedObject(each);
			if(attached->getMovableType() != "Entity")
			{
				continue;	// 2D content batches through the sprite-run path
			}
			Ogre::Entity* entity = static_cast<Ogre::Entity*>(attached);
			if(isStatic)
			{
				RenderBackend::staticBakeRegister(entity, this->mImpl->node);
			}
			else
			{
				RenderBackend::staticBakeUnregister(entity);
			}
		}
	}
	//---------------------------------------------------------
	bool RenderNode::isStatic() const
	{
		return this->mImpl->isStatic;
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
