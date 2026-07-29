/********************************************************************
	created:	Wednesday 2026/07/29 at 12:00
	filename: 	EditorTreeDnd.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorTreeDnd_h__29_7_2026__12_00_00__
#define __EditorTreeDnd_h__29_7_2026__12_00_00__

//! @file EditorTreeDnd.h
//! @brief the pure, UI-independent drop-zone classification shared by every
//! editor TREE that supports drag-drop reparent + sibling reorder (the visual
//! `.oui` editor's widget tree AND the scene GameObject Hierarchy). Given a row's
//! screen rect and the cursor Y, it decides whether a drop lands INTO the row (as
//! a child) or BETWEEN rows (as a sibling before/after it). No ImGui, no engine
//! types - just geometry, so a headless test proves the bands.

namespace OrkigeEditor
{
	//! @brief where a drop over a tree row lands.
	//! @c Into  - the row's middle band: drop as a CHILD of the row.
	//! @c Before - the row's top band: insert as the PRECEDING sibling.
	//! @c After  - the row's bottom band: insert as the FOLLOWING sibling.
	enum class TreeDropZone { Into, Before, After };

	//! @brief classify a drop over a row spanning [@p rowTop, rowTop+rowHeight) in
	//! screen Y at cursor @p cursorY. The top quarter is @c Before, the bottom
	//! quarter is @c After, the middle half is @c Into. A cursor above the row maps
	//! to @c Before and below it to @c After (the recursion clamps to the nearest
	//! band, so a drop in the gap still resolves). A non-positive @p rowHeight
	//! degenerates to @c Into. Pure.
	TreeDropZone classifyTreeDrop(float rowTop, float rowHeight, float cursorY);
}

#endif //__EditorTreeDnd_h__29_7_2026__12_00_00__
