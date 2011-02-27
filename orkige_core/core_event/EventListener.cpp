/**************************************************************
	created:	2010/07/26 at 13:36
	filename: 	EventListener.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "core_event/EventListener.h"
#include "core_event/EventType.h"
#include "core_event/EventManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	EventListener::EventListener(EventHandlerFunction const & handler) : eventHandlerFunction(handler), priority(0)
	{
	}
	//---------------------------------------------------------
	EventListener::EventListener(EventHandlerFunction const & handler,signed short prio) : eventHandlerFunction(handler), priority(prio)
	{
	}
	//---------------------------------------------------------
	EventListener::~EventListener()
	{
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
