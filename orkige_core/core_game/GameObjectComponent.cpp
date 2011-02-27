/**************************************************************
	created:	2010/08/15 at 15:20
	filename: 	GameObjectComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "core_game/GameObjectComponent.h"
#include "core_game/GameObject.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GameObjectComponent::GameObjectComponent()
	{
	}
	//---------------------------------------------------------
	GameObjectComponent::~GameObjectComponent()
	{
	}
	//---------------------------------------------------------
	EventManager* GameObjectComponent::getEventManager()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		EventManager* eventManager = componentOwner->getEventManager();
		oAssert(eventManager);
		return eventManager;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	IMPLEMENT_COMPONENT(GameObject)

	OOBJECT_IMPL(GameObjectComponent)
		OFUNCIR(getDependencies)	
	OOBJECT_END
}
