/********************************************************************
	created:	Thursday 2010/11/18 at 19:04
	filename: 	SceneNodeGuard.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_util/SceneNodeGuard.h"

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(SceneNodeGuard, NodeUpdatedEvent);
	IMPL_OWNED_EVENTTYPE(SceneNodeGuard, NodeAttachedEvent);
	IMPL_OWNED_EVENTTYPE(SceneNodeGuard, NodeDetachedEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SceneNodeGuard::SceneNodeGuard() : sceneNode(NULL), nodeListener(NULL)
	{
	}
	//---------------------------------------------------------
	SceneNodeGuard::~SceneNodeGuard()
	{
		if(nodeListener)
		{
			delete nodeListener;
		}
	}
	//---------------------------------------------------------
	SceneNodeGuard::SceneNodeListener::SceneNodeListener(EventManager* em, Object* eventData) 
		: enableNodeUpdatedEvent(false), 
		nodeCanBeDestroyed(false), 
		nodeUpdatedEvent(SceneNodeGuard::NodeUpdatedEvent,		oBadPointer(eventData)),
		nodeAttachedEvent(SceneNodeGuard::NodeAttachedEvent,	oBadPointer(eventData)),
		nodeDetachedEvent(SceneNodeGuard::NodeDetachedEvent,	oBadPointer(eventData)),
		eventManager(em)
	{
	}
	//---------------------------------------------------------
	SceneNodeGuard::SceneNodeListener::~SceneNodeListener()
	{

	}
	//---------------------------------------------------------
	void SceneNodeGuard::SceneNodeListener::nodeUpdated(const Ogre::Node*)
	{
		OPROFILEFUNC();
		if(this->enableNodeUpdatedEvent && this->eventManager)
		{
			this->eventManager->trigger(this->nodeUpdatedEvent);
		}
	}
	//---------------------------------------------------------
	void SceneNodeGuard::SceneNodeListener::nodeDestroyed(const Ogre::Node* node)
	{
		oAssertDesc(this->nodeCanBeDestroyed, "It's not valid to destroy SceneNode: \"" << node->getName() << "\"!");
	}
	//---------------------------------------------------------
	void SceneNodeGuard::SceneNodeListener::nodeAttached(const Ogre::Node* node)
	{
		OPROFILEFUNC();
		if(this->eventManager)
		{
			this->eventManager->trigger(nodeAttachedEvent);
		}
	}
	//---------------------------------------------------------
	void SceneNodeGuard::SceneNodeListener::nodeDetached(const Ogre::Node* node)
	{
		OPROFILEFUNC();
		if(this->eventManager)
		{
			this->eventManager->trigger(nodeDetachedEvent);
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void SceneNodeGuard::initSceneNodeGuard(Ogre::SceneNode* node, EventManager* eventManager, Object* eventData)
	{
		oAssert(node);
		this->nodeListener = new SceneNodeListener(eventManager, eventData);
		node->setListener(nodeListener);
		this->sceneNode = node;
	}
	//---------------------------------------------------------
	void SceneNodeGuard::deinitSceneNodeGuard()
	{
		delete this->nodeListener;
		this->nodeListener = NULL;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}