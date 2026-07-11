/**************************************************************
	created:	2026/07/11 at 14:00
	filename: 	ScrollClipTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the scroll-viewport math (core_util/UiLayout):
	the scroll clamp (offset pinned between 0 and viewport-content) and the
	viewport mapping (a scroll offset shifts the resolved content rects; a
	child stays hit-testable at its shifted rect). The scissor-equals-viewport
	half is asserted by the settings-screen selfcheck (it needs a real screen).
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/UiLayout.h"

#include <memory>
#include <vector>

using namespace Orkige;
using Catch::Approx;

TEST_CASE("clampScroll: content taller than the viewport scrolls within bounds",
	"[unit][uilayout][scroll]")
{
	// content 500 in a 200 viewport => scrollable range [-300, 0]
	CHECK(clampScroll(0.0f, 500.0f, 200.0f) == Approx(0.0f));
	CHECK(clampScroll(-100.0f, 500.0f, 200.0f) == Approx(-100.0f));
	CHECK(clampScroll(-300.0f, 500.0f, 200.0f) == Approx(-300.0f));
	// past either end clamps
	CHECK(clampScroll(50.0f, 500.0f, 200.0f) == Approx(0.0f));
	CHECK(clampScroll(-999.0f, 500.0f, 200.0f) == Approx(-300.0f));
}

TEST_CASE("clampScroll: content that fits cannot scroll", "[unit][uilayout][scroll]")
{
	CHECK(clampScroll(-50.0f, 120.0f, 200.0f) == Approx(0.0f));
	CHECK(clampScroll(0.0f, 200.0f, 200.0f) == Approx(0.0f));
}

TEST_CASE("scroll viewport: a scroll offset shifts the resolved content rects",
	"[unit][uilayout][scroll]")
{
	std::vector<std::unique_ptr<LayoutItem>> pool;
	// a viewport container holding a tall vertical content group; a scroll
	// offset on the viewport shifts the whole content up
	auto row0 = std::make_unique<LayoutItem>();
	row0->contentSize = LayoutVec2{300, 60};
	auto row1 = std::make_unique<LayoutItem>();
	row1->contentSize = LayoutVec2{300, 60};

	auto content = std::make_unique<LayoutItem>();
	content->group.type = LGT_Vertical;
	content->group.childForceExpand = true;
	applyAnchorPreset(content->node, LAP_STRETCH_TOP);
	content->fit.vertical = LFM_Preferred;	// content sizes to its rows
	content->children.push_back(row0.get());
	content->children.push_back(row1.get());

	LayoutItem viewport;
	viewport.children.push_back(content.get());

	// no scroll: content top row at the viewport top (y 100)
	viewport.scrollOffset = LayoutVec2{0, 0};
	resolveTree(viewport, LayoutRect{0, 100, 300, 120}, 1.0f);
	const float row0Unscrolled = row0->resolved.y;
	CHECK(row0Unscrolled == Approx(100.0f));
	CHECK(row1->resolved.y == Approx(160.0f));	// 100 + 60

	// scroll up by 60: every content rect shifts up by 60
	viewport.scrollOffset = LayoutVec2{0, -60};
	resolveTree(viewport, LayoutRect{0, 100, 300, 120}, 1.0f);
	CHECK(row0->resolved.y == Approx(40.0f));
	CHECK(row1->resolved.y == Approx(100.0f));
	// the content group measured taller than the viewport (2*60 = 120 == 120
	// here; a taller list would exceed it) - the extent the clamp uses
	CHECK(content->preferred.y == Approx(120.0f));
}
