/********************************************************************
	created:	Monday 2026/07/13 at 12:00
	filename: 	EditorPanelRegistryTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
//! Shared editor/native-menu panel registry contract.
#include <catch2/catch_test_macros.hpp>

#include <EditorPanelRegistry.h>

#include <set>
#include <string>

TEST_CASE("ImGui and macOS menus share one complete panel registry",
	"[unit][editor][menus]")
{
	using namespace Orkige;
	REQUIRE(EDITOR_PANEL_REGISTRY.size() ==
		static_cast<std::size_t>(PANEL_COUNT));
	REQUIRE(PANEL_COUNT == 13);

	const char* expected[] = { "Scene Hierarchy", "Inspector", "Console",
		"Stats", "Scene", "Assets", "Tile Palette", "Preview", "UI Editor",
		"Debug", "Source Control", "Terminal", "Tests" };
	std::set<std::string> uniqueLabels;
	for (int each = 0; each < PANEL_COUNT; ++each)
	{
		INFO("panel index " << each);
		CHECK(EDITOR_PANEL_REGISTRY[each].index == each);
		CHECK(std::string(EDITOR_PANEL_REGISTRY[each].label) == expected[each]);
		CHECK(uniqueLabels.insert(EDITOR_PANEL_REGISTRY[each].label).second);
	}

	CHECK(EDITOR_PANEL_REGISTRY[PANEL_PREVIEW].defaultVisible == false);
	// the UI Editor is closed by default (it opens on demand - Edit UI mode)
	CHECK(EDITOR_PANEL_REGISTRY[PANEL_UI_EDITOR].defaultVisible == false);
	// the Tile Palette is closed by default (it auto-opens on entering 2D mode)
	CHECK(EDITOR_PANEL_REGISTRY[PANEL_TILE_PALETTE].defaultVisible == false);
	// the Debug panel is closed by default (it auto-opens on a debugger
	// break-hit; code-editor documents are transient windows, not a panel)
	CHECK(EDITOR_PANEL_REGISTRY[PANEL_DEBUG].defaultVisible == false);
	// Source Control is closed by default; it opens from the View menu and docks
	// as a tab in the bottom group (beside Console/Stats/Debug)
	CHECK(EDITOR_PANEL_REGISTRY[PANEL_SOURCE_CONTROL].defaultVisible == false);
	// the Terminal is closed by default; it opens from the View menu and docks
	// as a tab in the bottom group beside Console
	CHECK(EDITOR_PANEL_REGISTRY[PANEL_TERMINAL].defaultVisible == false);
	// the Tests panel is closed by default; it opens from the View menu and
	// docks as a tab in the bottom group beside Console
	CHECK(EDITOR_PANEL_REGISTRY[PANEL_TESTS].defaultVisible == false);
}
