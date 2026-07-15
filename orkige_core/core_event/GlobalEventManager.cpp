/**************************************************************
	created:	2010/07/26 at 15:27
	filename: 	GlobalEventManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec	
***************************************************************/

#include "core_event/GlobalEventManager.h"

namespace Orkige
{
	IMPL_OSINGLETON(GlobalEventManager)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GlobalEventManager::GlobalEventManager() : EventManager()
	{
	}
	//---------------------------------------------------------
	GlobalEventManager::~GlobalEventManager()
	{
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------

	OOBJECT_IMPL(GlobalEventManager)
		OCONSTRUCTOR0()
		OSINGLETON()
	OOBJECT_END
}
