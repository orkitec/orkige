/********************************************************************
	created:	2009/08/15 at 15:26
	filename: 	EventHandler.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_event/EventHandler.h"
#include "core_util/foreach.h"
#include "core_event/GlobalEventManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	EventHandler::EventHandler()
	{
	}
	//---------------------------------------------------------
	EventHandler::~EventHandler()
	{
		this->unRegisterAllEvents();
	}
	//---------------------------------------------------------------
	EventManager* EventHandler::getEventManager()
	{
		return GlobalEventManager::getSingletonPtr();
	}
	//---------------------------------------------------------
	bool EventHandler::registerEvent(EventType const & eventType, EventHandlerFunction const & handlerFunction, signed short priority)
	{
		EventManager* eventManager = this->getEventManager();
		oDebugWarning(eventManager, "You have to set a EventManager before registering Events.");

		if(eventManager == NULL)
			return false;

		EventListenerMap::const_iterator it = this->registeredEvents.find(eventType);
		if(it == this->registeredEvents.end())
		{
			optr<EventListener> eventListener = onew( new EventListener(handlerFunction,priority) );
			bool eventRegistered = eventManager->addListener(eventListener,eventType);
			if(eventRegistered)
			{
				this->registeredEvents[eventType] = eventListener;
				return true;
			}
			oAssertDesc(eventRegistered, "Could not register event: " << eventType.getName());
		}
		return false;//event already registered
	}
	//---------------------------------------------------------
	bool EventHandler::unregisterEvent(EventType const & eventType)
	{
		EventManager* eventManager = this->getEventManager();
		oDebugWarning(eventManager, "You have to set a EventManager before unregistering Events.");
		
		if(eventManager == NULL)
			return false;

		EventListenerMap::iterator it = this->registeredEvents.find(eventType);
		if(it != this->registeredEvents.end())
		{
			optr<EventListener> eventListener = it->second;
			bool eventUnregistered = eventManager->delListener(eventListener,eventType);
			this->registeredEvents.erase(it);
			oAssertDesc(eventUnregistered, "Could not unregister event: " << eventType.getName());
			return true;
		}
		return false;//event wasn't registered	
	}
	//---------------------------------------------------------
	bool EventHandler::unRegisterAllEvents()
	{
		if(this->registeredEvents.empty())
			return true;//nothing todo

		EventManager* eventManager = this->getEventManager();
		oDebugWarning(eventManager, "You have to set a EventManager before unregistering Events.");

		if(eventManager == NULL)
			return false;

		typedef std::vector<EventType const *> EventTypeVector;
		EventTypeVector tempEventTypes;
		foreach(EventListenerMap::value_type const & vt, registeredEvents)
		{
			tempEventTypes.push_back(&(vt.first));
		}

		foreach(EventType const * et, tempEventTypes)
		{
			this->unregisterEvent(*et);
		}

		return true;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}