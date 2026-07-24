/**************************************************************
	created:	2026/07/24 at 12:45
	filename: 	EditorTabMenu.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorTabMenu_h__24_7_2026__12_45_00__
#define __EditorTabMenu_h__24_7_2026__12_45_00__

#include <imgui.h>

namespace OrkigeEditor
{
	//! @brief the shared right-click menu on a docked PANEL's tab: Close (the
	//! panel hides; View > Panels reopens it). Call immediately after
	//! ImGui::Begin - the popup binds to the window's tab item when docked.
	//! Document windows carry the richer set (close others/right/all) through
	//! the same deferred-action machinery in their own draw code; this is the
	//! one-entry panel flavor every tabbable panel shares.
	inline void editorPanelTabMenu(bool* visible)
	{
		if (visible != nullptr && ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Close"))
			{
				*visible = false;
			}
			ImGui::EndPopup();
		}
	}
}

#endif //__EditorTabMenu_h__24_7_2026__12_45_00__
