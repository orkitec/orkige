/********************************************************************
	created:	Tuesday 2026/08/04 at 12:00
	filename: 	EditorTestsPanel.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorTestsPanel_h__4_8_2026__12_00_00__
#define __EditorTestsPanel_h__4_8_2026__12_00_00__

struct EditorConsole;
struct EditorState;
struct ViewSettings;

//! @file EditorTestsPanel.h
//! @brief the Tests panel: the open project's own Lua suite, run and read
//! inside the editor.
//!
//! It is a VIEW and nothing more. Every decision it presents - what the suite
//! is, what a record means, where a failure points, which runs a button
//! implies - is a pure function in EditorProjectTests.h; running one is the
//! one session seam in EditorTestSession.h, which the MCP verbs drive too. So
//! a person and an agent see the same run, and this file holds no logic worth
//! testing.

namespace OrkigeEditor
{
	//! @brief draw the panel (a docked window; @p visible is ImGui's own close
	//! flag, persisted by the caller like every other panel).
	void drawTestsPanel(EditorState & state, ViewSettings & viewSettings,
		EditorConsole & console, bool * visible);
}

#endif //__EditorTestsPanel_h__4_8_2026__12_00_00__
