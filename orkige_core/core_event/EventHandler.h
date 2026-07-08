/********************************************************************
	created:	2009/08/15 at 15:26
	filename: 	EventHandler.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __EventHandler_h__15_8_2009__15_26_50__
#define __EventHandler_h__15_8_2009__15_26_50__

#include "core_event/EventManager.h"
#include <map>

namespace Orkige
{
	//! @brief helper for registering and unregistering events
	//!
	//! downside on using this is that it only allows 1 function per registered event
	class ORKIGE_CORE_DLL EventHandler
	{
		//--- Types -------------------------------------------------
	public:
	protected:
		typedef std::map<EventType, optr<EventListener> > EventListenerMap;	//!< map a EventType to a EventListener
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		EventListenerMap registeredEvents;	//!< registry of registered events
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		EventHandler();
		//! destructor
		virtual ~EventHandler();
		//! get assigned eventmanager (default: GlobalEventmanager)
		//! can be overriden to user other eventmanager
		virtual EventManager* getEventManager();
		//! register a event with given handler funtion, class  and priority
		template<class F,class T>
		inline bool registerEvent(EventType const & eventType, F const & handlerFunction, T * const handlerClass, signed short priority = 0);
		//! unregister given eventtype
		bool unregisterEvent(EventType const & eventType);
		//! unregister all registered events
		bool unRegisterAllEvents();
	protected:
		//! register a event with given handler funtionobject and priority
		bool registerEvent(EventType const & eventType, EventHandlerFunction const & handlerFunction, signed short priority);
	private:
	};
	//---------------------------------------------------------------
	template<class F,class T>
	inline bool EventHandler::registerEvent(EventType const & eventType, F const & handlerFunction, T * const handlerClass, signed short priority)
	{
		return this->registerEvent(eventType, MakeEventHandlerFunction(handlerClass, handlerFunction), priority);
	}
}

#endif //__EventHandler_h__15_8_2009__15_26_50__