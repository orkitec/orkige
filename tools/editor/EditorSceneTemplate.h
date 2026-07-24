/**************************************************************
	created:	2026/07/24 at 12:00
	filename: 	EditorSceneTemplate.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorSceneTemplate_h__24_7_2026__12_00_00__
#define __EditorSceneTemplate_h__24_7_2026__12_00_00__

// The interactive "new scene" entry point. Distinct from the low-level
// newScene() (EditorApp.h), which stays the empty "clear the world" primitive
// the MCP verb and selfchecks reuse: newDefaultScene seeds the defaults a
// fresh scene ships with - the Main Camera and the Environment (an enabled
// DAY-preset AtmosphereComponent, so the scene starts with a living sky) -
// @see EditorDocument.cpp. The File menu, Cmd/Ctrl+N and the native menu all
// route through this.

struct EditorState;
namespace Orkige { class EditorCore; }

//! @brief user-facing File > New Scene: clear the world (via newScene) and
//! stamp the default Main Camera + Environment on top. Refuses (as newScene
//! does) while a prefab stage is open, in which case nothing is added.
void newDefaultScene(EditorState& state, Orkige::EditorCore& core);

#endif //__EditorSceneTemplate_h__24_7_2026__12_00_00__
