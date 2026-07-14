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
	REQUIRE(PANEL_COUNT == 8);

	const char* expected[] = { "Scene Hierarchy", "Inspector", "Console",
		"Stats", "Scene", "Assets", "Tile Palette", "GUI Preview" };
	std::set<std::string> uniqueLabels;
	for (int each = 0; each < PANEL_COUNT; ++each)
	{
		INFO("panel index " << each);
		CHECK(EDITOR_PANEL_REGISTRY[each].index == each);
		CHECK(std::string(EDITOR_PANEL_REGISTRY[each].label) == expected[each]);
		CHECK(uniqueLabels.insert(EDITOR_PANEL_REGISTRY[each].label).second);
	}

	CHECK(EDITOR_PANEL_REGISTRY[PANEL_GUI_PREVIEW].defaultVisible == false);
}
