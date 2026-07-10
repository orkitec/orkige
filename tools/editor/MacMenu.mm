// MacMenu - native macOS menu bar implementation (see header for the design
// and the SDL3 interplay notes).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "MacMenu.h"

#import <AppKit/AppKit.h>

namespace
{
	//! menu item tags -> callback dispatch (panel toggles use TAG_PANEL_BASE+i)
	enum MenuTag
	{
		TAG_NEW_SCENE = 1,
		TAG_OPEN_SCENE,
		TAG_OPEN_RECENT,		//!< path travels as the item's representedObject
		TAG_NEW_PROJECT,
		TAG_OPEN_PROJECT,
		TAG_OPEN_RECENT_PROJECT,	//!< root travels as representedObject
		TAG_CLOSE_PROJECT,
		TAG_SAVE_SCENE,
		TAG_SAVE_SCENE_AS,
		TAG_ADD_SCENE_TO_LEVELS,
		TAG_IMPORT_MESH,
		TAG_QUIT,
		TAG_UNDO,
		TAG_REDO,
		TAG_DUPLICATE,
		TAG_DELETE,
		TAG_GROUP_SELECTION,
		TAG_CREATE_CUBE,
		TAG_CREATE_TEST_MESH,
		TAG_CREATE_PREFAB,
		TAG_EXPORT_MACOS,			//!< Build > Build for macOS
		TAG_EXPORT_IOS_SIMULATOR,	//!< Build > Build for iOS Simulator
		TAG_EXPORT_ANDROID,			//!< Build > Build for Android APK
		TAG_RESET_LAYOUT,
		TAG_VIEW_SETTINGS,
		TAG_ABOUT,
		TAG_PANEL_BASE = 100
	};

	Orkige::MacMenuActions gActions;
	Orkige::MacMenuStatus gStatus;			//!< last applied (change detection)
	bool gInstalled = false;
}

//! single target object for every editor menu item; validateMenuItem drives
//! the enabled states off the last macMenuUpdate status
@interface OrkigeMenuTarget : NSObject <NSMenuItemValidation>
- (void)menuAction:(NSMenuItem*)sender;
@end

@implementation OrkigeMenuTarget
- (void)menuAction:(NSMenuItem*)sender
{
	const NSInteger tag = [sender tag];
	if (tag >= TAG_PANEL_BASE &&
		tag < TAG_PANEL_BASE + static_cast<NSInteger>(Orkige::PANEL_COUNT))
	{
		if (gActions.setPanelVisible)
		{
			// toggle: the checkmark itself is refreshed by macMenuUpdate
			const bool nowVisible = ([sender state] != NSControlStateValueOn);
			gActions.setPanelVisible(
				static_cast<int>(tag - TAG_PANEL_BASE), nowVisible);
		}
		return;
	}
	if (tag == TAG_OPEN_RECENT)
	{
		NSString* path = [sender representedObject];
		if (gActions.openRecentScene && path)
		{
			gActions.openRecentScene(std::string([path UTF8String]));
		}
		return;
	}
	if (tag == TAG_EXPORT_MACOS || tag == TAG_EXPORT_IOS_SIMULATOR ||
		tag == TAG_EXPORT_ANDROID)
	{
		if (gActions.exportProject)
		{
			gActions.exportProject(tag == TAG_EXPORT_MACOS ? "macos"
				: tag == TAG_EXPORT_IOS_SIMULATOR ? "ios-simulator"
				: "android");
		}
		return;
	}
	if (tag == TAG_OPEN_RECENT_PROJECT)
	{
		NSString* path = [sender representedObject];
		if (gActions.openRecentProject && path)
		{
			gActions.openRecentProject(std::string([path UTF8String]));
		}
		return;
	}
	std::function<void()> const* action = nullptr;
	switch (tag)
	{
	case TAG_NEW_SCENE:			action = &gActions.newScene; break;
	case TAG_OPEN_SCENE:		action = &gActions.openScene; break;
	case TAG_NEW_PROJECT:		action = &gActions.newProject; break;
	case TAG_OPEN_PROJECT:		action = &gActions.openProject; break;
	case TAG_CLOSE_PROJECT:		action = &gActions.closeProject; break;
	case TAG_SAVE_SCENE:		action = &gActions.saveScene; break;
	case TAG_SAVE_SCENE_AS:		action = &gActions.saveSceneAs; break;
	case TAG_ADD_SCENE_TO_LEVELS:	action = &gActions.addSceneToLevels; break;
	case TAG_IMPORT_MESH:		action = &gActions.importMesh; break;
	case TAG_QUIT:				action = &gActions.quit; break;
	case TAG_UNDO:				action = &gActions.undo; break;
	case TAG_REDO:				action = &gActions.redo; break;
	case TAG_DUPLICATE:			action = &gActions.duplicateSelected; break;
	case TAG_DELETE:			action = &gActions.deleteSelected; break;
	case TAG_GROUP_SELECTION:	action = &gActions.groupSelected; break;
	case TAG_CREATE_CUBE:		action = &gActions.createCube; break;
	case TAG_CREATE_TEST_MESH:	action = &gActions.createTestMesh; break;
	case TAG_CREATE_PREFAB:		action = &gActions.createPrefab; break;
	case TAG_RESET_LAYOUT:		action = &gActions.resetLayout; break;
	case TAG_VIEW_SETTINGS:		action = &gActions.viewSettings; break;
	case TAG_ABOUT:				action = &gActions.about; break;
	default: break;
	}
	if (action && *action)
	{
		(*action)();
	}
}

- (BOOL)validateMenuItem:(NSMenuItem*)item
{
	switch ([item tag])
	{
	case TAG_UNDO:			return gStatus.canUndo ? YES : NO;
	case TAG_REDO:			return gStatus.canRedo ? YES : NO;
	case TAG_DUPLICATE:
	case TAG_DELETE:
	case TAG_GROUP_SELECTION:	return gStatus.hasSelection ? YES : NO;
	case TAG_CREATE_PREFAB:
		return (gStatus.hasSelection && gStatus.projectOpen) ? YES : NO;
	case TAG_CLOSE_PROJECT:	return gStatus.projectOpen ? YES : NO;
	case TAG_ADD_SCENE_TO_LEVELS:	return gStatus.sceneInProject ? YES : NO;
	case TAG_EXPORT_MACOS:
	case TAG_EXPORT_IOS_SIMULATOR:
	case TAG_EXPORT_ANDROID:
							return gStatus.canExport ? YES : NO;
	default:				return YES;
	}
}
@end

namespace
{
	OrkigeMenuTarget* gTarget = nil;
	NSMenuItem* gUndoItem = nil;
	NSMenuItem* gRedoItem = nil;
	NSMenu* gOpenRecentMenu = nil;
	NSMenu* gOpenRecentProjectMenu = nil;
	NSMenuItem* gPanelItems[Orkige::PANEL_COUNT] = {};

	//! find (by title) or create+insert a top-level menu. SDL pre-creates the
	//! app, Window and View menus - reusing by title keeps its items (e.g.
	//! View > Enter Full Screen) instead of stacking duplicate menus.
	NSMenu* ensureTopLevelMenu(NSString* title)
	{
		NSMenu* mainMenu = [NSApp mainMenu];
		if (!mainMenu)
		{
			mainMenu = [[NSMenu alloc] init];
			[NSApp setMainMenu:mainMenu];
		}
		NSMenuItem* existing = [mainMenu itemWithTitle:title];
		if (existing && [existing submenu])
		{
			return [existing submenu];
		}
		NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
		NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
			action:nil keyEquivalent:@""];
		[item setSubmenu:menu];
		// keep the mac convention: our menus go between the app menu and the
		// Window menu (SDL's View menu also sits at the end - inserting
		// before "Window" keeps File/Edit/... in the expected order)
		const NSInteger windowIndex = [mainMenu indexOfItemWithTitle:@"Window"];
		if (windowIndex >= 0)
		{
			[mainMenu insertItem:item atIndex:windowIndex];
		}
		else
		{
			[mainMenu addItem:item];
		}
		return menu;
	}

	//! append an action item wired to the shared target
	NSMenuItem* addItem(NSMenu* menu, NSString* title, MenuTag tag,
		NSString* keyEquivalent, NSEventModifierFlags modifiers)
	{
		NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
			action:@selector(menuAction:) keyEquivalent:keyEquivalent];
		[item setKeyEquivalentModifierMask:modifiers];
		[item setTarget:gTarget];
		[item setTag:tag];
		[menu addItem:item];
		return item;
	}

	//! (re)build the File > Open Recent submenu from the given paths (newest
	//! first, each item carries its path as representedObject); an empty list
	//! shows a placeholder item (nil action = auto-disabled by AppKit)
	void rebuildOpenRecentMenu(std::vector<std::string> const& recentScenes)
	{
		if (!gOpenRecentMenu)
		{
			return;
		}
		[gOpenRecentMenu removeAllItems];
		for (std::string const& path : recentScenes)
		{
			NSString* title = [NSString stringWithUTF8String:path.c_str()];
			NSMenuItem* item = addItem(gOpenRecentMenu, title,
				TAG_OPEN_RECENT, @"", 0);
			[item setRepresentedObject:title];
		}
		if ([gOpenRecentMenu numberOfItems] == 0)
		{
			[gOpenRecentMenu addItem:[[NSMenuItem alloc]
				initWithTitle:@"No Recent Scenes" action:nil
				keyEquivalent:@""]];
		}
	}

	//! (re)build the File > Open Recent Project submenu (same rules as the
	//! recent scenes submenu above)
	void rebuildOpenRecentProjectMenu(
		std::vector<std::string> const& recentProjects)
	{
		if (!gOpenRecentProjectMenu)
		{
			return;
		}
		[gOpenRecentProjectMenu removeAllItems];
		for (std::string const& path : recentProjects)
		{
			NSString* title = [NSString stringWithUTF8String:path.c_str()];
			NSMenuItem* item = addItem(gOpenRecentProjectMenu, title,
				TAG_OPEN_RECENT_PROJECT, @"", 0);
			[item setRepresentedObject:title];
		}
		if ([gOpenRecentProjectMenu numberOfItems] == 0)
		{
			[gOpenRecentProjectMenu addItem:[[NSMenuItem alloc]
				initWithTitle:@"No Recent Projects" action:nil
				keyEquivalent:@""]];
		}
	}
}

namespace Orkige
{
	void macMenuInstall(MacMenuActions const& actions)
	{
		if (!NSApp)
		{
			return; // no AppKit application (headless run) - nothing to do
		}
		gActions = actions;
		if (gInstalled)
		{
			return;
		}
		gTarget = [[OrkigeMenuTarget alloc] init];

		// File - the Cmd shortcuts have no ImGui-side competitors, so the
		// menu may own them. Quit deliberately carries NO Cmd+Q: SDL's app
		// menu already binds Cmd+Q (-> SDL_EVENT_QUIT -> the editor's
		// unsaved-changes confirm), a duplicate key equivalent would be
		// ambiguous. This item routes into the same confirm path.
		NSMenu* fileMenu = ensureTopLevelMenu(@"File");
		addItem(fileMenu, @"New Project…", TAG_NEW_PROJECT, @"", 0);
		addItem(fileMenu, @"Open Project…", TAG_OPEN_PROJECT, @"", 0);
		NSMenuItem* openRecentProjectItem = [[NSMenuItem alloc]
			initWithTitle:@"Open Recent Project" action:nil keyEquivalent:@""];
		gOpenRecentProjectMenu =
			[[NSMenu alloc] initWithTitle:@"Open Recent Project"];
		[openRecentProjectItem setSubmenu:gOpenRecentProjectMenu];
		[fileMenu addItem:openRecentProjectItem];
		rebuildOpenRecentProjectMenu(gStatus.recentProjects);
		addItem(fileMenu, @"Close Project", TAG_CLOSE_PROJECT, @"", 0);
		[fileMenu addItem:[NSMenuItem separatorItem]];
		addItem(fileMenu, @"New Scene", TAG_NEW_SCENE, @"n",
			NSEventModifierFlagCommand);
		addItem(fileMenu, @"Open Scene…", TAG_OPEN_SCENE, @"o",
			NSEventModifierFlagCommand);
		NSMenuItem* openRecentItem = [[NSMenuItem alloc]
			initWithTitle:@"Open Recent" action:nil keyEquivalent:@""];
		gOpenRecentMenu = [[NSMenu alloc] initWithTitle:@"Open Recent"];
		[openRecentItem setSubmenu:gOpenRecentMenu];
		[fileMenu addItem:openRecentItem];
		rebuildOpenRecentMenu(gStatus.recentScenes);
		[fileMenu addItem:[NSMenuItem separatorItem]];
		addItem(fileMenu, @"Save Scene", TAG_SAVE_SCENE, @"s",
			NSEventModifierFlagCommand);
		addItem(fileMenu, @"Save Scene As…", TAG_SAVE_SCENE_AS, @"s",
			NSEventModifierFlagCommand | NSEventModifierFlagShift);
		[fileMenu addItem:[NSMenuItem separatorItem]];
		addItem(fileMenu, @"Add Scene to Level Sequence",
			TAG_ADD_SCENE_TO_LEVELS, @"", 0);
		[fileMenu addItem:[NSMenuItem separatorItem]];
		addItem(fileMenu, @"Import Mesh…", TAG_IMPORT_MESH, @"i",
			NSEventModifierFlagCommand | NSEventModifierFlagShift);
		[fileMenu addItem:[NSMenuItem separatorItem]];
		addItem(fileMenu, @"Quit", TAG_QUIT, @"", 0);

		// Edit - deliberately NO key equivalents: Cmd+Z/Shift+Cmd+Z/Cmd+D
		// are handled by the editor's ImGui shortcut map (which SDL still
		// delivers); a menu key equivalent would risk double-execution. The
		// menu items exist for discoverability and mouse access, with live
		// labels/enabled-states from macMenuUpdate.
		NSMenu* editMenu = ensureTopLevelMenu(@"Edit");
		gUndoItem = addItem(editMenu, @"Undo", TAG_UNDO, @"", 0);
		gRedoItem = addItem(editMenu, @"Redo", TAG_REDO, @"", 0);
		[editMenu addItem:[NSMenuItem separatorItem]];
		addItem(editMenu, @"Duplicate", TAG_DUPLICATE, @"", 0);
		addItem(editMenu, @"Delete", TAG_DELETE, @"", 0);
		addItem(editMenu, @"Group Selection", TAG_GROUP_SELECTION, @"", 0);

		NSMenu* gameObjectMenu = ensureTopLevelMenu(@"GameObject");
		addItem(gameObjectMenu, @"Create Cube", TAG_CREATE_CUBE, @"", 0);
		addItem(gameObjectMenu, @"Create Test Mesh", TAG_CREATE_TEST_MESH,
			@"", 0);
		[gameObjectMenu addItem:[NSMenuItem separatorItem]];
		addItem(gameObjectMenu, @"Create Prefab", TAG_CREATE_PREFAB, @"", 0);

		// Build - project export (Util/orkige_export.py); enabled only with
		// a project open and no export running (validateMenuItem/canExport)
		NSMenu* buildMenu = ensureTopLevelMenu(@"Build");
		addItem(buildMenu, @"Build for macOS", TAG_EXPORT_MACOS, @"", 0);
		addItem(buildMenu, @"Build for iOS Simulator",
			TAG_EXPORT_IOS_SIMULATOR, @"", 0);
		addItem(buildMenu, @"Build for Android APK", TAG_EXPORT_ANDROID,
			@"", 0);

		// View - SDL already made a View menu (fullscreen toggle); append the
		// editor's panel toggles and layout actions to it
		NSMenu* viewMenu = ensureTopLevelMenu(@"View");
		if ([viewMenu numberOfItems] > 0)
		{
			[viewMenu addItem:[NSMenuItem separatorItem]];
		}
		NSMenuItem* panelsItem = [[NSMenuItem alloc]
			initWithTitle:@"Panels" action:nil keyEquivalent:@""];
		NSMenu* panelsMenu = [[NSMenu alloc] initWithTitle:@"Panels"];
		[panelsItem setSubmenu:panelsMenu];
		[viewMenu addItem:panelsItem];
		NSString* const panelTitles[PANEL_COUNT] = { @"Scene Hierarchy",
			@"Inspector", @"Console", @"Stats", @"Scene" };
		for (int panel = 0; panel < PANEL_COUNT; ++panel)
		{
			gPanelItems[panel] = addItem(panelsMenu, panelTitles[panel],
				static_cast<MenuTag>(TAG_PANEL_BASE + panel), @"", 0);
			[gPanelItems[panel] setState:NSControlStateValueOn];
		}
		addItem(viewMenu, @"Reset Layout", TAG_RESET_LAYOUT, @"", 0);
		[viewMenu addItem:[NSMenuItem separatorItem]];
		addItem(viewMenu, @"View Settings…", TAG_VIEW_SETTINGS, @"", 0);

		NSMenu* helpMenu = ensureTopLevelMenu(@"Help");
		addItem(helpMenu, @"About Orkige Editor", TAG_ABOUT, @"", 0);

		gInstalled = true;
	}

	void macMenuUpdate(MacMenuStatus const& status)
	{
		if (!gInstalled)
		{
			return;
		}
		// enabled-states are pulled by validateMenuItem from gStatus; only
		// the dynamic strings and checkmarks need explicit (guarded) writes
		if (gUndoItem && status.undoLabel != gStatus.undoLabel)
		{
			[gUndoItem setTitle:
				[NSString stringWithUTF8String:status.undoLabel.c_str()]];
		}
		if (gRedoItem && status.redoLabel != gStatus.redoLabel)
		{
			[gRedoItem setTitle:
				[NSString stringWithUTF8String:status.redoLabel.c_str()]];
		}
		for (int panel = 0; panel < PANEL_COUNT; ++panel)
		{
			if (gPanelItems[panel] &&
				status.panelVisible[panel] != gStatus.panelVisible[panel])
			{
				[gPanelItems[panel] setState:status.panelVisible[panel]
					? NSControlStateValueOn : NSControlStateValueOff];
			}
		}
		if (status.recentScenes != gStatus.recentScenes)
		{
			rebuildOpenRecentMenu(status.recentScenes);
		}
		if (status.recentProjects != gStatus.recentProjects)
		{
			rebuildOpenRecentProjectMenu(status.recentProjects);
		}
		gStatus = status;
	}

	int macMenuItemCount()
	{
		if (!NSApp || ![NSApp mainMenu])
		{
			return 0;
		}
		return static_cast<int>([[NSApp mainMenu] numberOfItems]);
	}
}
