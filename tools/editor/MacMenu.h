// MacMenu - the editor's native macOS menu bar (NSMenu).
//
// On macOS an ImGui-only menu bar feels alien: the system menu bar at the top
// of the screen stays empty. This module mirrors the editor's menu structure
// as real NSMenus so File/Edit/GameObject/View/Help live where mac users look
// for them; the in-window ImGui menu bar is skipped on APPLE (main.cpp).
//
// Layering: this header is plain C++ and deliberately UI-agnostic - it knows
// nothing about ImGui, EditorCore or SDL. main.cpp installs a callback table
// (MacMenuActions) routing every menu selection into the SAME functions the
// ImGui menus call, and refreshes enabled-states/labels/checkmarks once per
// frame via macMenuUpdate (cheap - AppKit is only touched on change).
//
// Interplay with SDL3: SDL creates a minimal main menu at video init (app
// menu with Quit Cmd+Q, Window menu, a View menu with the fullscreen toggle).
// macMenuInstall AUGMENTS that menu ([NSApp mainMenu]) instead of replacing
// it: existing top-level menus are reused by title, new ones are inserted
// before the Window menu. SDL's app-menu Quit posts SDL_EVENT_QUIT, which
// main.cpp routes through the same unsaved-changes confirm as every other
// quit path - Cmd+Q is therefore already safe and our File > Quit carries no
// competing key equivalent.
//
// Menu actions fire while SDL pumps AppKit events inside SDL_PollEvent, i.e.
// on the main thread between frames - the callbacks may safely poke editor
// state exactly like the SDL event handlers do.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Orkige
{
	//! the dockable editor panels the View menu toggles (indices shared
	//! between the menu layer and the UI shell)
	enum EditorPanelIndex
	{
		PANEL_HIERARCHY = 0,
		PANEL_INSPECTOR,
		PANEL_CONSOLE,
		PANEL_STATS,
		PANEL_SCENE,
		PANEL_COUNT
	};

	//! callback table the native menu routes into (installed from main.cpp)
	struct MacMenuActions
	{
		// File
		std::function<void()> newProject;
		std::function<void()> openProject;
		//! File > Open Recent Project entry clicked (path = the project root)
		std::function<void(std::string const& path)> openRecentProject;
		std::function<void()> closeProject;
		std::function<void()> newScene;
		std::function<void()> openScene;
		//! File > Open Recent entry clicked (path = the picked scene file)
		std::function<void(std::string const& path)> openRecentScene;
		std::function<void()> saveScene;
		std::function<void()> saveSceneAs;
		//! File > Add Scene to Level Sequence (needs an open project + saved scene)
		std::function<void()> addSceneToLevels;
		std::function<void()> importMesh;
		std::function<void()> quit;
		// Edit
		std::function<void()> undo;
		std::function<void()> redo;
		std::function<void()> duplicateSelected;
		std::function<void()> deleteSelected;
		std::function<void()> groupSelected;
		// GameObject
		std::function<void()> createCube;
		std::function<void()> createTestMesh;
		//! Create Prefab from the primary selection (needs an open project)
		std::function<void()> createPrefab;
		// Build (project export via Util/orkige_export.py); platform is
		// "macos", "ios-simulator" or "android"
		std::function<void(std::string const& platform)> exportProject;
		// Tools > an editor script tool was clicked (name = its stable name)
		std::function<void(std::string const& toolName)> runEditorScript;
		// View
		std::function<void(int panel, bool visible)> setPanelVisible;
		std::function<void()> resetLayout;
		std::function<void()> viewSettings;
		// Help
		std::function<void()> about;
	};

	//! per-frame refresh data: enabled states, dynamic labels, panel
	//! checkmarks (macMenuUpdate only touches AppKit when something changed)
	struct MacMenuStatus
	{
		bool canUndo = false;
		bool canRedo = false;
		std::string undoLabel = "Undo";		//!< e.g. "Undo Delete Cube1"
		std::string redoLabel = "Redo";
		bool hasSelection = false;			//!< gates Duplicate/Delete
		bool projectOpen = false;			//!< gates Close Project + (with a selection) Create Prefab
		bool sceneInProject = false;		//!< gates Add Scene to Level Sequence (project open + saved scene)
		bool canExport = false;				//!< gates the Build menu (project open, no export running)
		bool panelVisible[PANEL_COUNT] = { true, true, true, true, true };
		//! File > Open Recent entries, newest first (empty = placeholder item)
		std::vector<std::string> recentScenes;
		//! File > Open Recent Project entries, newest first
		std::vector<std::string> recentProjects;
		//! is a scripting backend compiled in (gates the Tools menu note)
		bool scriptingAvailable = true;
		//! Tools menu entries: {stable name, menu label} per editor script tool,
		//! sorted by label; empty shows a disabled hint
		std::vector<std::pair<std::string, std::string>> editorTools;
	};

	//! build the native menu bar (augments the menu SDL already created);
	//! call once after SDL_Init, before the frame loop
	void macMenuInstall(MacMenuActions const& actions);
	//! refresh enabled-states/labels/checkmarks; call once per frame
	void macMenuUpdate(MacMenuStatus const& status);
	//! test hook: number of top-level items in [NSApp mainMenu] (0 = none) -
	//! the editor selfcheck asserts > 4 (app/File/Edit/GameObject/View/...)
	int macMenuItemCount();
}
