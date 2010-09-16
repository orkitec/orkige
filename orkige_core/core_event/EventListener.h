/**************************************************************
	created:	2010/07/26 at 13:32
	filename: 	EventListener.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
***************************************************************/
#ifndef __EventListener_h__26_7_2010__13_32_42__
#define __EventListener_h__26_7_2010__13_32_42__

#include "core_event/Event.h"
#include "core_util/FastDelegate.h"

namespace Orkige
{

	//! definition of a event handling function
#ifdef ORKIGE_USE_BOOST_FOR_EVENTDELEGATION
	typedef boost::function1<bool,Event const>	EventHandlerFunction;
#	define MakeEventHandlerFunction(handlerClass, handlerFunction) boost::bind(handlerFunction,handlerClass,_1)
#else
	typedef fastdelegate::FastDelegate1<Event const &, bool> EventHandlerFunction;
#	define MakeEventHandlerFunction(handlerClass, handlerFunction) fastdelegate::MakeDelegate(handlerClass, handlerFunction)
#endif
	//---------------------------------------------------------
	//!	manages event handling funtions and optional priorities
	class ORKIGE_DLL EventListener
	{
		friend class EventManager;
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		const EventHandlerFunction eventHandler;	//!< assigned EventHandlerFunction
		signed short priority;						//!< priority (Default 0)
		//--- Methods -----------------------------------------
	public:
		//! constructor takes a EventHandlerFunction
		EventListener(EventHandlerFunction const & handler);
		//! constructor takes a EventHandlerFunction and a priority
		EventListener(EventHandlerFunction const & handler, signed short priority);
		//! destructor
		~EventListener();
	protected:
	private:
		//! call EventHandlerFunction
		inline const bool call(Event const & event) const;
		//! for priority sorting
		inline bool operator < (const EventListener & other) const 
		{
			return this->priority < other.priority;
		}
	};
	//---------------------------------------------------------
	static inline optr<EventListener> createEventListenerPtr(EventHandlerFunction const & handlerFunction, signed short priority = 0)
	{
		return onew(new EventListener(handlerFunction,priority));
	}
	//---------------------------------------------------------
	template<class F,class T>
	static inline optr<EventListener> createEventListenerPtr(F const & handlerFunction, T * const handlerClass, signed short priority = 0)
	{
		return createEventListenerPtr(MakeEventHandlerFunction(handlerClass, handlerFunction), priority);
	}
	//---------------------------------------------------------
	inline const bool EventListener::call(Event const & event) const
	{
		return this->eventHandler(event);
	}
	//---------------------------------------------------------
}

#endif //__EventListener_h__26_7_2010__13_32_42__
