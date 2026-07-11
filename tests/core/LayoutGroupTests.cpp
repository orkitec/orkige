/**************************************************************
	created:	2026/07/11 at 14:00
	filename: 	LayoutGroupTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the two-pass layout-group + content-size-fit
	core (core_util/UiLayout): horizontal/vertical stacks (spacing, padding,
	cross alignment, force-expand), grids (fixed columns/rows/flexible),
	bottom-up preferred sizing of nested groups, content-size-fit sizing a
	node to its (mocked) preferred content, and anchored rects nesting inside
	groups. Pure logic - no renderer, no window.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/UiLayout.h"

#include <memory>
#include <vector>

using namespace Orkige;
using Catch::Approx;

namespace
{
	//! a leaf item with a fixed measured content size (a mocked widget)
	LayoutItem* leaf(std::vector<std::unique_ptr<LayoutItem>> & pool,
		float w, float h)
	{
		pool.push_back(std::make_unique<LayoutItem>());
		LayoutItem* item = pool.back().get();
		item->contentSize.x = w;
		item->contentSize.y = h;
		return item;
	}
}

TEST_CASE("layout group: horizontal stack places children with spacing + padding",
	"[unit][uilayout][group]")
{
	std::vector<std::unique_ptr<LayoutItem>> pool;
	LayoutItem root;
	root.group.type = LGT_Horizontal;
	root.group.spacing = 10.0f;
	root.group.padding.left = 8.0f;
	root.group.padding.top = 4.0f;
	root.children.push_back(leaf(pool, 30, 20));
	root.children.push_back(leaf(pool, 40, 20));
	root.children.push_back(leaf(pool, 50, 20));

	// preferred: pad 8 + 30 + 10 + 40 + 10 + 50 = 148 wide, pad 4 + 20 = 24 tall
	const LayoutVec2 pref = measurePreferred(root, 1.0f);
	CHECK(pref.x == Approx(148.0f));
	CHECK(pref.y == Approx(24.0f));

	assignRects(root, LayoutRect{100, 200, 400, 100}, 1.0f);
	// first child at content origin (100+8, 200+4)
	CHECK(root.children[0]->resolved.x == Approx(108.0f));
	CHECK(root.children[0]->resolved.y == Approx(204.0f));
	CHECK(root.children[0]->resolved.w == Approx(30.0f));
	// second child after child0 width + spacing: 108 + 30 + 10 = 148
	CHECK(root.children[1]->resolved.x == Approx(148.0f));
	// third: 148 + 40 + 10 = 198
	CHECK(root.children[2]->resolved.x == Approx(198.0f));
}

TEST_CASE("layout group: vertical stack + cross-axis alignment",
	"[unit][uilayout][group]")
{
	std::vector<std::unique_ptr<LayoutItem>> pool;
	LayoutItem root;
	root.group.type = LGT_Vertical;
	root.group.spacing = 5.0f;
	root.group.childAlign = LAL_Center;
	root.children.push_back(leaf(pool, 40, 20));	// narrower than the 100 content
	root.children.push_back(leaf(pool, 60, 20));

	measurePreferred(root, 1.0f);
	assignRects(root, LayoutRect{0, 0, 100, 400}, 1.0f);
	// child0 centred in 100 wide content: (100-40)/2 = 30
	CHECK(root.children[0]->resolved.x == Approx(30.0f));
	CHECK(root.children[0]->resolved.y == Approx(0.0f));
	// child1 centred: (100-60)/2 = 20, y = 20 + spacing 5 = 25
	CHECK(root.children[1]->resolved.x == Approx(20.0f));
	CHECK(root.children[1]->resolved.y == Approx(25.0f));
}

TEST_CASE("layout group: childForceExpand stretches children across the cross axis",
	"[unit][uilayout][group]")
{
	std::vector<std::unique_ptr<LayoutItem>> pool;
	LayoutItem root;
	root.group.type = LGT_Vertical;
	root.group.childForceExpand = true;
	root.children.push_back(leaf(pool, 40, 30));

	measurePreferred(root, 1.0f);
	assignRects(root, LayoutRect{0, 0, 250, 400}, 1.0f);
	// forced to the full content width, kept at its own height
	CHECK(root.children[0]->resolved.x == Approx(0.0f));
	CHECK(root.children[0]->resolved.w == Approx(250.0f));
	CHECK(root.children[0]->resolved.h == Approx(30.0f));
}

TEST_CASE("layout group: grid fixed columns", "[unit][uilayout][group]")
{
	std::vector<std::unique_ptr<LayoutItem>> pool;
	LayoutItem root;
	root.group.type = LGT_Grid;
	root.group.constraint = LGC_FixedColumns;
	root.group.constraintCount = 2;
	root.group.cellSize.x = 50.0f;
	root.group.cellSize.y = 40.0f;
	root.group.spacing = 10.0f;
	for(int i = 0; i < 5; ++i) { root.children.push_back(leaf(pool, 0, 0)); }

	// 5 children, 2 columns => 3 rows.
	// width: 2*50 + 1*10 = 110; height: 3*40 + 2*10 = 140
	const LayoutVec2 pref = measurePreferred(root, 1.0f);
	CHECK(pref.x == Approx(110.0f));
	CHECK(pref.y == Approx(140.0f));

	assignRects(root, LayoutRect{0, 0, 110, 140}, 1.0f);
	// child index 3 => col 1, row 1 => x = 50+10 = 60, y = 40+10 = 50
	CHECK(root.children[3]->resolved.x == Approx(60.0f));
	CHECK(root.children[3]->resolved.y == Approx(50.0f));
	CHECK(root.children[3]->resolved.w == Approx(50.0f));
	CHECK(root.children[3]->resolved.h == Approx(40.0f));
}

TEST_CASE("layout group: grid fixed rows fills column-major",
	"[unit][uilayout][group]")
{
	std::vector<std::unique_ptr<LayoutItem>> pool;
	LayoutItem root;
	root.group.type = LGT_Grid;
	root.group.constraint = LGC_FixedRows;
	root.group.constraintCount = 2;
	root.group.cellSize.x = 20.0f;
	root.group.cellSize.y = 20.0f;
	for(int i = 0; i < 4; ++i) { root.children.push_back(leaf(pool, 0, 0)); }

	measurePreferred(root, 1.0f);
	assignRects(root, LayoutRect{0, 0, 40, 40}, 1.0f);
	// column-major: index 1 => col 0, row 1 => (0, 20)
	CHECK(root.children[1]->resolved.x == Approx(0.0f));
	CHECK(root.children[1]->resolved.y == Approx(20.0f));
	// index 2 => col 1, row 0 => (20, 0)
	CHECK(root.children[2]->resolved.x == Approx(20.0f));
	CHECK(root.children[2]->resolved.y == Approx(0.0f));
}

TEST_CASE("layout group: nested group preferred size feeds the parent (two-pass)",
	"[unit][uilayout][group]")
{
	std::vector<std::unique_ptr<LayoutItem>> pool;
	// an inner horizontal group of two 30x20 leaves (spacing 0) => 60x20
	auto inner = std::make_unique<LayoutItem>();
	inner->group.type = LGT_Horizontal;
	inner->children.push_back(leaf(pool, 30, 20));
	inner->children.push_back(leaf(pool, 30, 20));

	LayoutItem root;
	root.group.type = LGT_Vertical;
	root.group.spacing = 5.0f;
	root.children.push_back(leaf(pool, 100, 10));	// a wide leaf
	root.children.push_back(inner.get());

	// root preferred height: 10 + 5 + 20 = 35; width: max(100, 60) = 100
	const LayoutVec2 pref = measurePreferred(root, 1.0f);
	CHECK(pref.y == Approx(35.0f));
	CHECK(pref.x == Approx(100.0f));
	// the inner group measured its own extent bottom-up
	CHECK(inner->preferred.x == Approx(60.0f));
	CHECK(inner->preferred.y == Approx(20.0f));

	assignRects(root, LayoutRect{0, 0, 100, 35}, 1.0f);
	// inner group placed below the first leaf: y = 10 + 5 = 15
	CHECK(inner->resolved.y == Approx(15.0f));
	// its children arranged inside it
	CHECK(inner->children[1]->resolved.x == Approx(30.0f));
}

TEST_CASE("content-size-fit: a node sizes to its preferred content",
	"[unit][uilayout][fit]")
{
	// a button-like leaf whose measured content is 84x28 wants to fit both axes
	LayoutItem button;
	button.contentSize.x = 84.0f;
	button.contentSize.y = 28.0f;
	button.fit.horizontal = LFM_Preferred;
	button.fit.vertical = LFM_Preferred;
	applyAnchorPreset(button.node, LAP_CENTER);
	button.node.pivot.x = 0.5f;
	button.node.pivot.y = 0.5f;

	measurePreferred(button, 1.0f);
	// centred in an 800x600 parent, its size becomes the content, centred on pivot
	const LayoutRect r = resolveItemRect(LayoutRect{0, 0, 800, 600}, button, 1.0f);
	CHECK(r.w == Approx(84.0f));
	CHECK(r.h == Approx(28.0f));
	CHECK(r.x + r.w * 0.5f == Approx(400.0f));
	CHECK(r.y + r.h * 0.5f == Approx(300.0f));
}

TEST_CASE("content-size-fit: a vertical group fits its arranged children",
	"[unit][uilayout][fit]")
{
	std::vector<std::unique_ptr<LayoutItem>> pool;
	LayoutItem panel;
	panel.group.type = LGT_Vertical;
	panel.group.spacing = 4.0f;
	panel.group.padding.top = 6.0f;
	panel.group.padding.bottom = 6.0f;
	panel.fit.vertical = LFM_Preferred;
	applyAnchorPreset(panel.node, LAP_TOP);
	panel.children.push_back(leaf(pool, 200, 40));
	panel.children.push_back(leaf(pool, 200, 40));

	// preferred height: 6 + 40 + 4 + 40 + 6 = 96
	measurePreferred(panel, 1.0f);
	CHECK(panel.preferred.y == Approx(96.0f));
	resolveTree(panel, LayoutRect{0, 0, 400, 800}, 1.0f);
	CHECK(panel.resolved.h == Approx(96.0f));
}

TEST_CASE("layout group: an anchored child nests inside a group child",
	"[unit][uilayout][group]")
{
	std::vector<std::unique_ptr<LayoutItem>> pool;
	// a group row that is itself a plain container holding an anchored badge
	auto row = std::make_unique<LayoutItem>();
	row->contentSize.x = 200;	// the row's own preferred (plain container leaf-ish)
	row->contentSize.y = 40;
	auto badge = std::make_unique<LayoutItem>();
	applyAnchorPreset(badge->node, LAP_TOPRIGHT);
	badge->node.pivot.x = 1.0f;
	badge->node.setSizeDelta(20, 20);
	badge->node.setAnchoredPosition(-4, 4);	// 4px inside the row's top-right
	row->children.push_back(badge.get());

	LayoutItem group;
	group.group.type = LGT_Vertical;
	group.group.childForceExpand = true;
	group.children.push_back(row.get());

	measurePreferred(group, 1.0f);
	assignRects(group, LayoutRect{0, 0, 300, 400}, 1.0f);
	// the row is force-expanded to width 300 at y 0
	REQUIRE(row->resolved.w == Approx(300.0f));
	// the badge pins to the row's top-right, 4px in: right edge at 300-4 = 296
	CHECK((badge->resolved.x + badge->resolved.w) == Approx(296.0f));
	CHECK(badge->resolved.y == Approx(4.0f));
}
