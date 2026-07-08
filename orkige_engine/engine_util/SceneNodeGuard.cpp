/********************************************************************
	created:	Thursday 2010/11/18 at 19:04
	filename: 	SceneNodeGuard.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
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
	SceneNodeGuard::SceneNodeGuard()
		: mEventManager(NULL)
		, mEventData(NULL)
	{
	}
	//---------------------------------------------------------
	SceneNodeGuard::~SceneNodeGuard()
	{
		// the handle is RAII - if a component forgot deinitSceneNodeGuard,
		// dropping mNode still detaches and destroys the backend node
	}
	//---------------------------------------------------------
	void SceneNodeGuard::attachToNode(optr<RenderNode> const & parent)
	{
		oAssert(this->mNode);
		oAssert(parent);
		if(this->mEventManager)
		{
			this->mEventManager->trigger(Event(SceneNodeGuard::NodeDetachedEvent,
				oBadPointer(this->mEventData)));
		}
		this->mNode->setParent(parent);
		if(this->mEventManager)
		{
			this->mEventManager->trigger(Event(SceneNodeGuard::NodeAttachedEvent,
				oBadPointer(this->mEventData)));
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void SceneNodeGuard::initSceneNodeGuard(optr<RenderNode> const & node, EventManager* eventManager, Object* eventData)
	{
		oAssert(node);
		this->mNode = node;
		this->mEventManager = eventManager;
		this->mEventData = eventData;
	}
	//---------------------------------------------------------
	void SceneNodeGuard::deinitSceneNodeGuard()
	{
		this->mNode.reset();
		this->mEventManager = NULL;
		this->mEventData = NULL;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
