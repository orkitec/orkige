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
	TypeInfo GameObjectComponent::getComponentKey() const
	{
		// discover the container key by finding THIS instance in the owner's
		// component map: a name-aliased kind (a script component) is stored under
		// its script name, not its C++ class, and that key is the identity every
		// consumer uses. No per-component storage keeps the class ABI-stable.
		GameObject* owner = const_cast<GameObjectComponent*>(this)->getComponentOwner();
		if (owner)
		{
			for (auto const & entry : owner->getComponents())
			{
				if (entry.second.get() == this)
				{
					return TypeInfo(entry.first);
				}
			}
		}
		// not (yet) owned: the C++ TypeInfo IS the key (they coincide for every
		// plain component). TypeInfo's copy ctor is explicit - direct-initialise.
		return TypeInfo(this->getTypeInfo());
	}
	//---------------------------------------------------------
	void GameObjectComponent::setWantsUpdates(bool wantsUpdates)
	{
		this->wantsUpdates = wantsUpdates;
		if(this->getComponentOwner())
		{
			// key off the container KIND, so a name-aliased script component
			// (stored under its script name, not "ScriptComponent") registers
			const TypeInfo key = this->getComponentKey();
			if(this->wantsUpdates)
			{
				this->getComponentOwner()->enableUpdates(key);
			}
			else
			{
				this->getComponentOwner()->disableUpdates(key);
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
