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
#include "core_base/TypeManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	PropertySchema getComponentSchema(GameObjectComponent const & component)
	{
		PropertySchema schema;
		// the static per-type half (the instance's DYNAMIC type, so a subclass's
		// full schema comes through)
		if (PropertySchema const * staticSchema =
			TypeManager::getSingleton().getPropertySchema(
				component.getTypeInfo().getId()))
		{
			for (PropertyDesc const & desc : staticSchema->properties())
			{
				schema.add(desc);
			}
		}
		// the dynamic per-instance half (empty for a fully-static component) -
		// appended AFTER the static ones so a script's exports render below the
		// component's own fields, and REPLACE a static of the same name
		PropertySchema const dynamicSchema = component.getInstancePropertySchema();
		for (PropertyDesc const & desc : dynamicSchema.properties())
		{
			schema.add(desc);
		}
		return schema;
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GameObjectComponent::GameObjectComponent() : wantsUpdates(false)
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
	bool GameObjectComponent::createBeforeLoad()
	{
		return false;
	}
	//---------------------------------------------------------
	void GameObjectComponent::setWantsUpdates(bool wantsUpdates)
	{
		this->wantsUpdates = wantsUpdates;
		if(this->getComponentOwner())
		{
			if(this->wantsUpdates)
			{
				this->getComponentOwner()->enableUpdates(this->getTypeInfo());
			}
			else
			{
				this->getComponentOwner()->disableUpdates(this->getTypeInfo());
			}
		}
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
