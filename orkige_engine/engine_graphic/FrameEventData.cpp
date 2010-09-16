/********************************************************************
	created:	Tuesday 2010/09/07 at 12:16
	filename: 	FrameEventData.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_graphic/FrameEventData.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FrameEventData)
		OVAR(timeSinceLastEvent)
		OVAR(timeSinceLastFrame)
	OOBJECT_END
}
