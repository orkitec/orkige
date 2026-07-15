/********************************************************************
	created:	2010/11/02
	filename: 	DragEventData.cpp
	author:		philipp.engelhard
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/DragEventData.h"
//complete type needed to expose the button member to Lua
#include "engine_gui/GuiDragDropButton.h"

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
