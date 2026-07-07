/********************************************************************
	created:	2010/11/02
	filename: 	DragEventData.cpp
	author:		philipp.engelhard
	purpose:	
	copyright:	(c) 2010 kunst-stoff
***************************************************************/

#include "engine_fastgui/DragEventData.h"
//complete type needed to expose the button member to Lua
#include "engine_fastgui/FastGuiDragDropButton.h"

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
	OABSTRACT_IMPL(DragEventData)
		OVAR(button)
		OVAR(state)
		OVAR(position)
	OOBJECT_END
}
