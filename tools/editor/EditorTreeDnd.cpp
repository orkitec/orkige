/********************************************************************
	created:	Wednesday 2026/07/29 at 12:00
	filename: 	EditorTreeDnd.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the shared tree drag-drop drop-zone classifier (@see
				EditorTreeDnd.h).
*********************************************************************/

#include "EditorTreeDnd.h"

namespace OrkigeEditor
{
	//---------------------------------------------------------
	TreeDropZone classifyTreeDrop(float rowTop, float rowHeight, float cursorY)
	{
		if(rowHeight <= 0.0f)
		{
			return TreeDropZone::Into;
		}
		const float t = (cursorY - rowTop) / rowHeight;	// 0 at the top, 1 at the bottom
		if(t < 0.25f) { return TreeDropZone::Before; }
		if(t > 0.75f) { return TreeDropZone::After; }
		return TreeDropZone::Into;
	}
}
