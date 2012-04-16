/**************************************************************
	created:	2010/08/15 at 15:27
	filename: 	GameObjectManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
***************************************************************/

#include "core_game/GameObjectManager.h"
#include "core_game/GameObject.h"
#include "core_event/GlobalEventManager.h"

namespace Orkige
{
	IMPL_OSINGLETON(GameObjectManager)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GameObjectManager::GameObjectManager() : Object(String("GameObjectManager")), numUpdatableComponents(0), currentUpdatableComponentIndex(0), enableObjectUpdates(true)
	{
	}
	//---------------------------------------------------------
	GameObjectManager::~GameObjectManager()
	{
	}
	//---------------------------------------------------------
	bool GameObjectManager::enableEvent(EventType const & eventType)
	{
		EventType::TypeId eventTypeId = eventType.getId(); 
		EventListenerMap::const_iterator it = this->globalEvents.find(eventTypeId);
		if(it == this->globalEvents.end())
		{
			EventManager* eventManager = GlobalEventManager::getSingletonPtr();
			optr<EventListener> eventListener = createEventListenerPtr(&GameObjectManager::onGlobalEvent, this);
			bool eventRegistered = eventManager->addListener(eventListener,eventType);
			if(eventRegistered)
			{
				this->globalEvents[eventTypeId] = eventListener;
				return true;
			}
			oAssert(eventRegistered);
		}
		return false;
	}
	//---------------------------------------------------------
	bool GameObjectManager::disableEvent(EventType const & eventType)
	{
		EventType::TypeId eventTypeId = eventType.getId(); 
		EventListenerMap::iterator it = this->globalEvents.find(eventTypeId);
		if(it != this->globalEvents.end())
		{
			EventManager* eventManager = GlobalEventManager::getSingletonPtr();
			optr<EventListener> eventListener = it->second;
			bool eventUnregistered = eventManager->delListener(eventListener,eventType);
			this->globalEvents.erase(it);
			oAssert(eventUnregistered);
			return true;
		}
		return false;	
	}
	//---------------------------------------------------------
	bool GameObjectManager::triggerEvent(Event const & event) const										
	{
		OPROFILE(String(__FUNCTION__) + "( " + event.getObjectID() + " )");
		bool retval = false;
		for(GameObjectMap::const_iterator it=this->objects.begin(),itend = this->objects.end(); it != itend; ++it)
		{
			//OPROFILE(it->second->getObjectID() + "->triggerEvent( " + event.getObjectID() + " )");
			if(it->second->triggerEvent(event))
				retval = true;
		}
		return retval;
	}
	//---------------------------------------------------------
	void GameObjectManager::update(float delta)
	{
		OPROFILEFUNC();

		this->processDeleteQueue();
		if(this->enableObjectUpdates)
		{
			for(this->currentUpdatableComponentIndex = 0; this->currentUpdatableComponentIndex < this->numUpdatableComponents; this->currentUpdatableComponentIndex++)
			{
				this->updatableComponents[this->currentUpdatableComponentIndex]->onUpdateComponent(delta);
			}
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void GameObjectManager::enableUpdates(GameObjectComponent * component)
	{
		oAssert(component);
		GameObjectComponentPtrVector::const_iterator it = std::find(this->updatableComponents.begin(), this->updatableComponents.end(), component);
		if(it == this->updatableComponents.end())
		{
			this->updatableComponents.push_back(component);
		}
		this->numUpdatableComponents = this->updatableComponents.size();
	}
	//---------------------------------------------------------
	void GameObjectManager::disableUpdates(GameObjectComponent * component)
	{
		oAssert(component);
		if(this->numUpdatableComponents > 0)
		{
			GameObjectComponentPtrVector::iterator it = std::find(this->updatableComponents.begin(), this->updatableComponents.end(), component);
			if(it != this->updatableComponents.end())
			{
				std::size_t eraseIndex = it - this->updatableComponents.begin();
				if(eraseIndex < this->currentUpdatableComponentIndex)
				{
					this->currentUpdatableComponentIndex--;
				}
				this->updatableComponents.erase(it);
			}
			this->numUpdatableComponents = this->updatableComponents.size();
		}
	}
	//---------------------------------------------------------
	void GameObjectManager::processDeleteQueue()
	{
		OPROFILEFUNC();
		foreach(String const & id,  this->deleteQueue)
		{
			this->delGameObject(id);
		}
		this->deleteQueue.clear();
	}
	//---------------------------------------------------------
	bool GameObjectManager::createBeforeLoad()	
	{
		return false;
	}
	//---------------------------------------------------------
	void GameObjectManager::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		ar << objects;
	}
	//---------------------------------------------------------
	void GameObjectManager::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		ar >> objects;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(GameObjectManager)
		OSINGLETON()
		OFUNC(addGameObject)
		OFUNC(delGameObject)
		OFUNC(getGameObject)
		OFUNC(objectExists)
		OFUNC(createGameObject)
		OFUNC(enableEvent)
		OFUNC(disableEvent)
		OVAR(objects)
	OOBJECT_END
}
