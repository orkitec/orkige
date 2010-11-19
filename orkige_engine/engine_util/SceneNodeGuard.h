/********************************************************************
	created:	Thursday 2010/11/18 at 19:04
	filename: 	SceneNodeGuard.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __SceneNodeGuard_h__18_11_2010__19_04_10__
#define __SceneNodeGuard_h__18_11_2010__19_04_10__

#include "engine_module/EnginePrerequisites.h"
#include <core_event/EventManager.h>

namespace Orkige
{
	//! utility wrapper around Ogre::SceneNode to prevent accidental destruction and to receive of events on Node changes 
	class SceneNodeGuard
	{
		//--- Types -------------------------------------------------
	public:
		//! @brief Note that this happens when the node's derived update happens, not every time a method altering it's state occurs. 
		//! There may be several state-changing calls but only one of these calls, when the node graph is fully updated.
		//! @see Ogre::Node::Listener::nodeUpdated
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(NodeUpdatedEvent);
		//! triggered when sceneNode is attached to another SceneNode
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(NodeAttachedEvent);
		//! triggered when sceneNode is detached from its parent SceneNode
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(NodeDetachedEvent);
	protected:
		//! listener for Ogre::Node Events 
		class SceneNodeListener : public Ogre::Node::Listener
		{
			//--- Variables ---------------------------------------
		public:
			bool enableNodeUpdatedEvent;			//!< enable triggering of the TransformUpdatedEvent
			bool nodeCanBeDestroyed;				//!< flag to mark if its valid to destroy the sceneNode
		protected:
			const Event nodeUpdatedEvent;
			const Event nodeAttachedEvent;
			const Event nodeDetachedEvent;
			EventManager* eventManager;
			Object*	eventData;
		private:
			//--- Methods -----------------------------------------
		public:
			//! constructor
			SceneNodeListener(EventManager* em, Object* eventData);
			//! destructor
			virtual ~SceneNodeListener();
			//! @copydoc Ogre::Node::Listener::nodeUpdated
			virtual void nodeUpdated(const Ogre::Node* node);
			//! @copydoc Ogre::Node::Listener::nodeDestroyed
			virtual void nodeDestroyed(const Ogre::Node* node);
			//! @copydoc Ogre::Node::Listener::nodeAttached
			virtual void nodeAttached(const Ogre::Node* node);
			//! @copydoc Ogre::Node::Listener::nodeDetached
			virtual void nodeDetached(const Ogre::Node* node);
		protected:
		private:
		};
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Ogre::SceneNode*	sceneNode;	//!< transform SceneNode
		SceneNodeListener*	nodeListener;	//!< listens on events for the transform node
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		SceneNodeGuard();
		//! destructor
		virtual ~SceneNodeGuard();
		//! get the Transform SceneNode
		inline Ogre::SceneNode const * getSceneNode() const;
		//! get position relative to the rootSceneNode
		inline Ogre::Vector3 const & getPosition() const;
		//! get position in the world
		inline Ogre::Vector3 const & getWorldPosition() const;
		//! get position relative to the rootSceneNode
		inline Ogre::Quaternion const & getOrientation() const;
		//! get the orientation independent from root
		inline Ogre::Quaternion const & getWorldOrientation() const;
		//! get scale
		inline Ogre::Vector3 const & getScale() const;
		//! set position
		inline void setPosition(Ogre::Vector3 const & position);
		//! set scale
		inline void setScale(Ogre::Vector3 const & scale);
		//! set orientation
		inline void setOrientation(Ogre::Quaternion const & orientation);
		//! @copydoc Ogre::Node::setInheritOrientation
		inline void setInheritOrientation(bool inherit);
		//! @copydoc Ogre::Node::getInheritOrientation
		inline bool getInheritOrientation() const;
		//! @copydoc Ogre::Node::setInheritScale
		inline void setInheritScale(bool inherit);
		//! @copydoc Ogre::Node::getInheritScale
		inline bool getInheritScale() const;
		//! @copydoc Ogre::Node::scale
		inline void scale(Ogre::Vector3 const & scale);
		//! @copydoc Ogre::Node::scale
		inline void scale(Ogre::Real x, Ogre::Real y, Ogre::Real z);
		//! @copydoc Ogre::Node::translate
		inline void translate(Ogre::Vector3 const & d, Ogre::Node::TransformSpace relativeTo = Ogre::Node::TS_PARENT);
		//! @copydoc Ogre::Node::translate
		inline void translate(Ogre::Real x, Ogre::Real y, Ogre::Real z, Ogre::Node::TransformSpace relativeTo = Ogre::Node::TS_PARENT);
		//! @copydoc Ogre::Node::translate
		inline void translate(Ogre::Matrix3 const & axes, Ogre::Vector3 const & move, Ogre::Node::TransformSpace relativeTo = Ogre::Node::TS_PARENT);
		//! @copydoc Ogre::Node::translate
		inline void translate(Ogre::Matrix3 const & axes, Ogre::Real x, Ogre::Real y, Ogre::Real z, Ogre::Node::TransformSpace relativeTo = Ogre::Node::TS_PARENT);
		//! @copydoc Ogre::Node::roll
		inline void roll(Ogre::Radian const & angle, Ogre::Node::TransformSpace relativeTo = Ogre::Node::TS_LOCAL);
		//! @copydoc Ogre::Node::pitch
		inline void pitch(Ogre::Radian const & angle, Ogre::Node::TransformSpace relativeTo = Ogre::Node::TS_LOCAL);
		//! @copydoc Ogre::Node::yaw
		inline void yaw(Ogre::Radian const & angle, Ogre::Node::TransformSpace relativeTo = Ogre::Node::TS_LOCAL);
		//! @copydoc Ogre::Node::rotate
		inline void rotate(Ogre::Vector3 const & axis, Ogre::Radian const & angle, Ogre::Node::TransformSpace relativeTo = Ogre::Node::TS_LOCAL);
		//! @copydoc Ogre::Node::rotate
		inline void rotate(Ogre::Quaternion const & q, Ogre::Node::TransformSpace relativeTo = Ogre::Node::TS_LOCAL);
		//! @copydoc Ogre::Node::getLocalAxes
		inline Ogre::Matrix3 getLocalAxes() const;
		//! @copydoc Ogre::Node::createChild
		inline Ogre::Node* createChild(Ogre::Vector3 const & translate = Ogre::Vector3::ZERO, Ogre::Quaternion const & rotate = Ogre::Quaternion::IDENTITY );
		//! @copydoc Ogre::Node::createChild
		inline Ogre::Node* createChild(String const & name, Ogre::Vector3 const & translate = Ogre::Vector3::ZERO, Ogre::Quaternion const & rotate = Ogre::Quaternion::IDENTITY);
		//! @copydoc Ogre::Node::addChild
		inline void addChild(Ogre::Node* child);
		//! @copydoc Ogre::Node::numChildren
		inline unsigned short numChildren(void) const;
		//! @copydoc Ogre::Node::getChild
		inline Ogre::Node* getChild(unsigned short index) const;    
		//! @copydoc Ogre::Node::getChild
		inline Ogre::Node* getChild(String const & name) const;
		//! @copydoc Ogre::Node::getChildIterator
		inline Ogre::Node::ChildNodeIterator getChildIterator(void);
		//! @copydoc Ogre::Node::getChildIterator
		inline Ogre::Node::ConstChildNodeIterator getChildIterator() const;
		//! @copydoc Ogre::Node::removeChild
		inline Ogre::Node* removeChild(unsigned short index);
		//! @copydoc Ogre::Node::removeChild
		inline Ogre::Node* removeChild(Ogre::Node* child);
		//! @copydoc Ogre::Node::removeChild
		inline Ogre::Node* removeChild(String const & name);
		//! @copydoc Ogre::Node::removeAllChildren
		inline void removeAllChildren();
		//! @copydoc Ogre::Node::getParent
		inline Ogre::Node* getParent() const;
		//! @copydoc Ogre::SceneNode::getParentSceneNode
		inline Ogre::SceneNode* getParentSceneNode(void) const;
		//! @copydoc Ogre::SceneNode::attachObject
		inline void attachObject(Ogre::MovableObject* obj);
		//! @copydoc Ogre::SceneNode::numAttachedObjects
		inline unsigned short numAttachedObjects() const;
		//! @copydoc Ogre::SceneNode::getAttachedObject
		inline Ogre::MovableObject* getAttachedObject(unsigned short index);
		//! @copydoc Ogre::SceneNode::getAttachedObject
		inline Ogre::MovableObject* getAttachedObject(String const & name);
		//! @copydoc Ogre::SceneNode::detachObject
		inline Ogre::MovableObject* detachObject(unsigned short index);
		//! @copydoc Ogre::SceneNode::detachObject
		inline void detachObject(Ogre::MovableObject* obj);
		//! @copydoc Ogre::SceneNode::detachObject
		inline Ogre::MovableObject* detachObject(String const & name);
		//! @copydoc Ogre::SceneNode::detachAllObjects
		inline void detachAllObjects();
		//! @copydoc Ogre::SceneNode::_getWorldAABB
		inline const Ogre::AxisAlignedBox& getWorldAABB() const;
		//! @copydoc Ogre::SceneNode::_updateBounds
		inline void updateBounds();
		//! @copydoc Ogre::SceneNode::getAttachedObjectIterator
		inline Ogre::SceneNode::ObjectIterator getAttachedObjectIterator();
		//! @copydoc Ogre::SceneNode::getAttachedObjectIterator
		inline Ogre::SceneNode::ConstObjectIterator getAttachedObjectIterator() const;
		//! @copydoc Ogre::SceneNode::getCreator
		inline Ogre::SceneManager* getSceneManager() const;
		//! @copydoc Ogre::SceneNode::removeAndDestroyChild
		inline void removeAndDestroyChild(String const & name);
		//! @copydoc Ogre::SceneNode::removeAndDestroyChild
		inline void removeAndDestroyChild(unsigned short index);
		//! @copydoc Ogre::SceneNode::removeAndDestroyAllChildren
		inline void removeAndDestroyAllChildren();
		//! @copydoc Ogre::SceneNode::showBoundingBox
		inline void showBoundingBox(bool bShow);
		//! @copydoc Ogre::SceneNode::hideBoundingBox
		inline void hideBoundingBox(bool bHide);
		//! @copydoc Ogre::SceneNode::getShowBoundingBox
		inline bool getShowBoundingBox() const;
		//! @copydoc Ogre::SceneNode::createChildSceneNode
		inline Ogre::SceneNode* createChildSceneNode(String const & name, Ogre::Vector3 const & translate = Ogre::Vector3::ZERO, Ogre::Quaternion const & rotate = Ogre::Quaternion::IDENTITY);
		//! @copydoc Ogre::SceneNode::findLights
		inline void findLights(Ogre::LightList& destList, Ogre::Real radius, Ogre::uint32 lightMask = 0xFFFFFFFF) const;
		//! @copydoc Ogre::SceneNode::setFixedYawAxis
		inline void setFixedYawAxis( bool useFixed, Ogre::Vector3 const & fixedAxis = Ogre::Vector3::UNIT_Y );
		//! @copydoc Ogre::SceneNode::setDirection
		inline void setDirection(Ogre::Vector3 const & vec, Ogre::Node::TransformSpace relativeTo = Ogre::Node::TS_LOCAL, Ogre::Vector3 const & localDirectionVector = Ogre::Vector3::NEGATIVE_UNIT_Z);
		//! @copydoc Ogre::SceneNode::lookAt
		inline void lookAt( Ogre::Vector3 const & targetPoint, Ogre::Node::TransformSpace relativeTo, Ogre::Vector3 const & localDirectionVector = Ogre::Vector3::NEGATIVE_UNIT_Z);
		//! @copydoc Ogre::SceneNode::setAutoTracking
		inline void setAutoTracking(bool enabled, Ogre::SceneNode const * target = 0, Ogre::Vector3 const & localDirectionVector = Ogre::Vector3::NEGATIVE_UNIT_Z, Ogre::Vector3 const & offset = Ogre::Vector3::ZERO);
		//! @copydoc Ogre::SceneNode::getAutoTrackTarget
		inline Ogre::SceneNode const * getAutoTrackTarget();
		//! @copydoc Ogre::SceneNode::getAutoTrackOffset
		inline Ogre::Vector3 const & getAutoTrackOffset();
		//! @copydoc Ogre::SceneNode::getAutoTrackLocalDirection
		inline Ogre::Vector3 const & getAutoTrackLocalDirection();
		//! @copydoc Ogre::SceneNode::setVisible
		inline void setVisible(bool visible, bool cascade = true);
		//! @copydoc Ogre::SceneNode::flipVisibility
		inline void flipVisibility(bool cascade = true);
		//! @copydoc Ogre::SceneNode::setDebugDisplayEnabled
		inline void setDebugDisplayEnabled(bool enabled, bool cascade = true);
		//! @copydoc Ogre::SceneNode::getDebugRenderable
		inline Ogre::Node::DebugRenderable* getDebugRenderable();
		//! attach to given node and tetach from current parent node
		inline void attachToNode(Ogre::Node* node);
		//! enable triggering of TransformUpdateEvent
		inline void setEnableNodeUpdateEvent(bool enable);
		//! is TransformUpdateEvent triggering enabled
		inline bool isNodeUpdateEventEnabled();
	protected:
		//! init wrapper from a SceneNode and optional EventManager and eventData
		void initSceneNodeGuard(Ogre::SceneNode* node, EventManager* eventManager = NULL, Object* eventData = NULL);
		//! deinit wrapper
		void deinitSceneNodeGuard();
	private:
	};
	//---------------------------------------------------------
	inline Ogre::SceneNode const * SceneNodeGuard::getSceneNode() const
	{
		return this->sceneNode;
	}
	//---------------------------------------------------------
	inline Ogre::Vector3 const & SceneNodeGuard::getPosition() const
	{
		return this->sceneNode->getPosition();
	}
	//---------------------------------------------------------
	inline Ogre::Vector3 const & SceneNodeGuard::getWorldPosition() const
	{
		return this->sceneNode->_getDerivedPosition();
	}
	//---------------------------------------------------------
	inline Ogre::Quaternion const & SceneNodeGuard::getOrientation() const
	{
		return this->sceneNode->getOrientation();
	}
	//---------------------------------------------------------
	inline Ogre::Quaternion const & SceneNodeGuard::getWorldOrientation() const
	{
		return this->sceneNode->_getDerivedOrientation();
	}
	//---------------------------------------------------------
	inline Ogre::Vector3 const & SceneNodeGuard::getScale() const
	{
		return this->sceneNode->getScale();
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setPosition(Ogre::Vector3 const & position)
	{
		this->sceneNode->setPosition(position);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setScale(Ogre::Vector3 const & scale)
	{
		this->sceneNode->setScale(scale);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setOrientation(Ogre::Quaternion const & orientation)
	{
		this->sceneNode->setOrientation(orientation);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setInheritOrientation(bool inherit)
	{
		this->sceneNode->setInheritOrientation(inherit);
	}
	//---------------------------------------------------------
	inline bool SceneNodeGuard::getInheritOrientation() const
	{
		return this->sceneNode->getInheritOrientation();
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setInheritScale(bool inherit)
	{
		this->sceneNode->setInheritScale(inherit);
	}
	//---------------------------------------------------------
	inline bool SceneNodeGuard::getInheritScale() const
	{
		return this->sceneNode->getInheritScale();
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::scale(Ogre::Vector3 const & scale)
	{
		this->sceneNode->scale(scale);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::scale(Ogre::Real x, Ogre::Real y, Ogre::Real z)
	{
		this->sceneNode->scale(x, y, z);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::translate(Ogre::Vector3 const & d, Ogre::Node::TransformSpace relativeTo)
	{
		this->sceneNode->translate(d, relativeTo);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::translate(Ogre::Real x, Ogre::Real y, Ogre::Real z, Ogre::Node::TransformSpace relativeTo)
	{
		this->sceneNode->translate(x, y, z, relativeTo);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::translate(Ogre::Matrix3 const & axes, Ogre::Vector3 const & move, Ogre::Node::TransformSpace relativeTo)
	{
		this->sceneNode->translate(axes, move, relativeTo);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::translate(Ogre::Matrix3 const & axes, Ogre::Real x, Ogre::Real y, Ogre::Real z, Ogre::Node::TransformSpace relativeTo)
	{
		this->sceneNode->translate(axes, x, y, z, relativeTo);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::roll(Ogre::Radian const & angle, Ogre::Node::TransformSpace relativeTo)
	{
		this->sceneNode->roll(angle, relativeTo);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::pitch(Ogre::Radian const & angle, Ogre::Node::TransformSpace relativeTo)
	{
		this->sceneNode->pitch(angle, relativeTo);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::yaw(Ogre::Radian const & angle, Ogre::Node::TransformSpace relativeTo)
	{
		this->sceneNode->yaw(angle, relativeTo);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::rotate(Ogre::Vector3 const & axis, Ogre::Radian const & angle, Ogre::Node::TransformSpace relativeTo)
	{
		this->sceneNode->rotate(axis, angle, relativeTo);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::rotate(Ogre::Quaternion const & q, Ogre::Node::TransformSpace relativeTo)
	{
		this->sceneNode->rotate(q, relativeTo);
	}
	//---------------------------------------------------------
	inline Ogre::Matrix3 SceneNodeGuard::getLocalAxes() const
	{
		return this->sceneNode->getLocalAxes();
	}
	//---------------------------------------------------------
	inline Ogre::Node* SceneNodeGuard::createChild(Ogre::Vector3 const & translate, Ogre::Quaternion const & rotate)
	{
		this->sceneNode->createChild(translate, rotate);
	}
	//---------------------------------------------------------
	inline Ogre::Node* SceneNodeGuard::createChild(String const & name, Ogre::Vector3 const & translate, Ogre::Quaternion const & rotate)
	{
		this->sceneNode->createChild(name, translate, rotate);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::addChild(Ogre::Node* child)
	{
		this->sceneNode->addChild(child);
	}
	//---------------------------------------------------------
	inline unsigned short SceneNodeGuard::numChildren() const
	{
		this->sceneNode->numChildren();
	}
	//---------------------------------------------------------
	inline Ogre::Node* SceneNodeGuard::getChild(unsigned short index) const
	{
		this->sceneNode->getChild(index);
	}
	//---------------------------------------------------------    
	inline Ogre::Node* SceneNodeGuard::getChild(String const & name) const
	{
		this->sceneNode->getChild(name);
	}
	//---------------------------------------------------------
	inline Ogre::Node::ChildNodeIterator SceneNodeGuard::getChildIterator()
	{
		this->sceneNode->getChildIterator();
	}
	//---------------------------------------------------------
	inline Ogre::Node::ConstChildNodeIterator SceneNodeGuard::getChildIterator() const
	{
		return this->getSceneNode()->getChildIterator();
	}
	//---------------------------------------------------------
	inline Ogre::Node* SceneNodeGuard::removeChild(unsigned short index)
	{
		return this->sceneNode->removeChild(index);
	}
	//---------------------------------------------------------
	inline Ogre::Node* SceneNodeGuard::removeChild(Ogre::Node* child)
	{
		return this->sceneNode->removeChild(child);
	}
	//---------------------------------------------------------
	inline Ogre::Node* SceneNodeGuard::removeChild(String const & name)
	{
		return this->sceneNode->removeChild(name);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::removeAllChildren()
	{
		this->sceneNode->removeAllChildren();
	}
	//---------------------------------------------------------
	inline Ogre::Node* SceneNodeGuard::getParent() const
	{
		return this->sceneNode->getParent();
	}
	//---------------------------------------------------------
	inline Ogre::SceneNode* SceneNodeGuard::getParentSceneNode() const
	{
		return this->sceneNode->getParentSceneNode();
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::attachObject(Ogre::MovableObject* obj)
	{
		this->sceneNode->attachObject(obj);
	}
	//---------------------------------------------------------
	inline unsigned short SceneNodeGuard::numAttachedObjects() const
	{
		return this->sceneNode->numAttachedObjects();
	}
	//---------------------------------------------------------
	inline Ogre::MovableObject* SceneNodeGuard::getAttachedObject(unsigned short index)
	{
		return this->sceneNode->getAttachedObject(index);
	}
	//---------------------------------------------------------
	inline Ogre::MovableObject* SceneNodeGuard::getAttachedObject(String const & name)
	{
		return this->sceneNode->getAttachedObject(name);
	}
	//---------------------------------------------------------
	inline Ogre::MovableObject* SceneNodeGuard::detachObject(unsigned short index)
	{
		return this->sceneNode->detachObject(index);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::detachObject(Ogre::MovableObject* obj)
	{
		this->sceneNode->detachObject(obj);
	}
	//---------------------------------------------------------
	inline Ogre::MovableObject* SceneNodeGuard::detachObject(String const & name)
	{
		return this->sceneNode->detachObject(name);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::detachAllObjects()
	{
		this->sceneNode->detachAllObjects();
	}
	//---------------------------------------------------------
	inline const Ogre::AxisAlignedBox& SceneNodeGuard::getWorldAABB() const
	{
		return this->sceneNode->_getWorldAABB();
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::updateBounds()
	{
		this->sceneNode->_updateBounds();
	}
	//---------------------------------------------------------
	inline Ogre::SceneNode::ObjectIterator SceneNodeGuard::getAttachedObjectIterator()
	{
		return this->sceneNode->getAttachedObjectIterator();
	}
	//---------------------------------------------------------
	inline Ogre::SceneNode::ConstObjectIterator SceneNodeGuard::getAttachedObjectIterator() const
	{
		return this->getSceneNode()->getAttachedObjectIterator();
	}
	//---------------------------------------------------------
	inline Ogre::SceneManager* SceneNodeGuard::getSceneManager() const
	{
		return this->sceneNode->getCreator();
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::removeAndDestroyChild(String const & name)
	{
		this->sceneNode->removeAndDestroyChild(name);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::removeAndDestroyChild(unsigned short index)
	{
		this->sceneNode->removeAndDestroyChild(index);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::removeAndDestroyAllChildren()
	{
		this->sceneNode->removeAndDestroyAllChildren();
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::showBoundingBox(bool show)
	{
		this->sceneNode->showBoundingBox(show);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::hideBoundingBox(bool hide)
	{
		this->sceneNode->hideBoundingBox(hide);
	}
	//---------------------------------------------------------
	inline bool SceneNodeGuard::getShowBoundingBox() const
	{
		return this->sceneNode->getShowBoundingBox();
	}
	//---------------------------------------------------------
	inline Ogre::SceneNode* SceneNodeGuard::createChildSceneNode(String const & name, Ogre::Vector3 const & translate, Ogre::Quaternion const & rotate)
	{
		return this->sceneNode->createChildSceneNode(name, translate, rotate);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::findLights(Ogre::LightList& destList, Ogre::Real radius, Ogre::uint32 lightMask) const
	{
		this->sceneNode->findLights(destList, radius, lightMask);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setFixedYawAxis( bool useFixed, Ogre::Vector3 const & fixedAxis)
	{
		this->sceneNode->setFixedYawAxis(useFixed, fixedAxis);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setDirection(Ogre::Vector3 const & vec, Ogre::Node::TransformSpace relativeTo, Ogre::Vector3 const & localDirectionVector)
	{
		this->sceneNode->setDirection(vec, relativeTo, localDirectionVector);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::lookAt( Ogre::Vector3 const & targetPoint, Ogre::Node::TransformSpace relativeTo, Ogre::Vector3 const & localDirectionVector)
	{
		this->sceneNode->lookAt(targetPoint, relativeTo, localDirectionVector);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setAutoTracking(bool enabled, Ogre::SceneNode const * target, Ogre::Vector3 const & localDirectionVector, Ogre::Vector3 const & offset)
	{
		this->sceneNode->setAutoTracking(enabled, const_cast<Ogre::SceneNode*>(target), localDirectionVector, offset);
	}
	//---------------------------------------------------------
	inline Ogre::SceneNode const * SceneNodeGuard::getAutoTrackTarget()
	{
		return this->sceneNode->getAutoTrackTarget();
	}
	//---------------------------------------------------------
	inline Ogre::Vector3 const & SceneNodeGuard::getAutoTrackOffset()
	{
		return this->sceneNode->getAutoTrackOffset();
	}
	//---------------------------------------------------------
	inline Ogre::Vector3 const & SceneNodeGuard::getAutoTrackLocalDirection()
	{
		return this->sceneNode->getAutoTrackLocalDirection();
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setVisible(bool visible, bool cascade)
	{
		this->sceneNode->setVisible(visible, cascade);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::flipVisibility(bool cascade)
	{
		this->sceneNode->flipVisibility(cascade);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setDebugDisplayEnabled(bool enabled, bool cascade)
	{
		this->sceneNode->setDebugDisplayEnabled(enabled, cascade);
	}
	//---------------------------------------------------------
	inline Ogre::Node::DebugRenderable* SceneNodeGuard::getDebugRenderable()
	{
		return this->sceneNode->getDebugRenderable();
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::attachToNode(Ogre::Node* node)
	{
		node->addChild(this->sceneNode);
	}
	//---------------------------------------------------------
	inline void SceneNodeGuard::setEnableNodeUpdateEvent(bool enable)
	{
		this->nodeListener->enableNodeUpdatedEvent = enable;
	}
	//---------------------------------------------------------
	inline bool SceneNodeGuard::isNodeUpdateEventEnabled()
	{
		return this->nodeListener->enableNodeUpdatedEvent;
	}
	//---------------------------------------------------------
}

#endif //__SceneNodeGuard_h__18_11_2010__19_04_10__