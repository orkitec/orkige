/**************************************************************
	created:	2026/07/24 at 12:00
	filename: 	EditorTabActions.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorTabActions_h__24_7_2026__12_00_00__
#define __EditorTabActions_h__24_7_2026__12_00_00__

#include <cstddef>
#include <vector>

namespace OrkigeEditor
{
	//! @brief the standard actions a tab's context menu offers - shared by
	//! every tabbable surface (document windows use the full set; a plain
	//! panel exposes only Close). Pure vocabulary; the ImGui-facing menu that
	//! emits these lives with the windows (drawTabContextMenu).
	enum class TabAction
	{
		None,
		Close,			//!< close this tab
		CloseOthers,	//!< close every sibling but this tab
		CloseRight,		//!< close the siblings after this tab
		CloseAll		//!< close the whole group
	};

	//! @brief which of `count` tabs does `action` on the tab at `targetIndex`
	//! close? Returns one flag per tab (true = close). Out-of-range targets
	//! and None close nothing. Pure - the single source of the close-set
	//! semantics for every tab group.
	std::vector<bool> computeTabsToClose(std::size_t count,
		std::size_t targetIndex, TabAction action);
}

#endif //__EditorTabActions_h__24_7_2026__12_00_00__
