/**************************************************************
	created:	2010/07/26 at 13:08
	filename: 	Event.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
***************************************************************/

#include "core_event/Event.h"
#include "core_event/EventType.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Event::~Event()
	{

	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------


	OOBJECT_IMPL(Event)
		OCONSTRUCTOR1(String)
		OCONSTRUCTOR2(String,optr<Object>)
		OCONSTRUCTOR1(Event&)
		OFUNC(getData)
		OFUNC(setData)
		OFUNCCR(getType)
		/*OFUNC(getTime)*/
	OOBJECT_END
}
