/********************************************************************
	created:	Monday 2026/07/13 at 12:00
	filename: 	EditorPanelRegistry.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
//! @file EditorPanelRegistry.h
//! @brief One authoritative panel list shared by the ImGui and macOS menus.
#pragma once

#include <array>

// id, human label, default visibility, ViewSettings member. The member token
// lets every editor-side visibility mapping expand this exact same list.
#define ORKIGE_EDITOR_PANEL_LIST(X) \
	X(PANEL_HIERARCHY, "Scene Hierarchy", true, showHierarchyPanel) \
	X(PANEL_INSPECTOR, "Inspector", true, showInspectorPanel) \
	X(PANEL_CONSOLE, "Console", true, showConsolePanel) \
	X(PANEL_STATS, "Stats", true, showStatsPanel) \
	X(PANEL_SCENE, "Scene", true, showScenePanel) \
	X(PANEL_ASSETS, "Assets", true, showAssetBrowserPanel) \
	X(PANEL_TILE_PALETTE, "Tile Palette", false, showTilePalettePanel) \
	X(PANEL_PREVIEW, "Preview", false, showPreviewPanel) \
	X(PANEL_UI_EDITOR, "UI Editor", false, showUiEditorPanel) \
	X(PANEL_DEBUG, "Debug", false, showDebugPanel) \
	X(PANEL_SOURCE_CONTROL, "Source Control", false, showSourceControlPanel) \
	X(PANEL_TERMINAL, "Terminal", false, showTerminalPanel)

namespace Orkige
{
	enum EditorPanelIndex
	{
#define ORKIGE_EDITOR_PANEL_ENUM(id, label, visible, member) id,
		ORKIGE_EDITOR_PANEL_LIST(ORKIGE_EDITOR_PANEL_ENUM)
#undef ORKIGE_EDITOR_PANEL_ENUM
		PANEL_COUNT
	};

	struct EditorPanelDescriptor
	{
		EditorPanelIndex index;
		const char* label;
		bool defaultVisible;
	};

	inline constexpr std::array<EditorPanelDescriptor, PANEL_COUNT>
		EDITOR_PANEL_REGISTRY = {{
#define ORKIGE_EDITOR_PANEL_DESCRIPTOR(id, label, visible, member) \
			{ id, label, visible },
			ORKIGE_EDITOR_PANEL_LIST(ORKIGE_EDITOR_PANEL_DESCRIPTOR)
#undef ORKIGE_EDITOR_PANEL_DESCRIPTOR
		}};
}
