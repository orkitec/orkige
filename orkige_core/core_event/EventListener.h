/**************************************************************
	created:	2010/07/26 at 13:32
	filename: 	EventListener.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
***************************************************************/
#ifndef __EventListener_h__26_7_2010__13_32_42__
#define __EventListener_h__26_7_2010__13_32_42__

#include "core_event/Event.h"
#include "core_util/FastDelegate.h"
#include "core_debug/Profile.h"
#include <functional>

namespace Orkige
{

	//! definition of a event handling function
#ifdef ORKIGE_USE_BOOST_FOR_EVENTDELEGATION
	typedef std::function<bool(Event const)>	EventHandlerFunction;
#	define MakeEventHandlerFunction(handlerClass, handlerFunction) std::bind(handlerFunction,handlerClass,std::placeholders::_1)
#else
	typedef fastdelegate::FastDelegate1<Event const &, bool> EventHandlerFunction;
#	define MakeEventHandlerFunction(handlerClass, handlerFunction) fastdelegate::MakeDelegate(handlerClass, handlerFunction)
#endif
	//---------------------------------------------------------
	//!	manages event handling funtions and optional priorities
	class ORKIGE_CORE_DLL EventListener
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
		const EventHandlerFunction eventHandlerFunction;	//!< assigned EventHandlerFunction
		signed short priority;								//!< priority (Default 0)
		//--- Methods -----------------------------------------
	public:
		//! constructor takes a EventHandlerFunction
		EventListener(EventHandlerFunction const & handler);
		//! constructor takes a EventHandlerFunction and a priority
		EventListener(EventHandlerFunction const & handler, signed short priority);
		//! destructor
		~EventListener();
		//! for priority sorting
		inline bool operator < (EventListener const & other) const;
	protected:
	private:
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
	inline bool EventListener::operator < (EventListener const & other) const 
	{
		OPROFILEFUNC();
		return this->priority < other.priority;
	}
	//---------------------------------------------------------
	struct EventListenerOptrCmp
	{
		inline bool operator()( optr<EventListener> const & lhs, optr<EventListener> const & rhs)
		{
			return *lhs < *rhs;
		}
	};
}

#endif //__EventListener_h__26_7_2010__13_32_42__
