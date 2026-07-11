/**************************************************************
	created:	2026/07/11 at 10:00
	filename: 	UiLayoutResolverTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the pure rect-anchor resolver (core_util/
	UiLayout): resolveRect across anchor presets, pivots, offsets, point vs
	stretch anchors, nesting, layout-scale application, and the safe-area
	root case regression-locked against SafeArea.h's UiAnchor::place. No
	renderer, no window.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/UiLayout.h"
#include "core_util/SafeArea.h"

using namespace Orkige;
using Catch::Approx;

namespace
{
	//! a point-anchor node at a preset with an explicit size + anchoredPosition
	LayoutNode pointNode(LayoutAnchorPreset preset, float pivotX, float pivotY,
		float sizeW, float sizeH, float posX, float posY)
	{
		LayoutNode node;
		applyAnchorPreset(node, preset);
		node.pivot.x = pivotX;
		node.pivot.y = pivotY;
		node.setSizeDelta(sizeW, sizeH);
		node.setAnchoredPosition(posX, posY);
		return node;
	}
}

TEST_CASE("resolveRect: top-left point anchor places by offset", "[unit][uilayout]")
{
	LayoutRect parent{0, 0, 1280, 720};
	LayoutNode node;
	applyAnchorPreset(node, LAP_TOPLEFT);
	node.setOffsets(12, 20, 12 + 100, 20 + 40);	// pos (12,20) size (100,40)

	const LayoutRect r = resolveRect(parent, node, 1.0f);
	CHECK(r.x == Approx(12.0f));
	CHECK(r.y == Approx(20.0f));
	CHECK(r.w == Approx(100.0f));
	CHECK(r.h == Approx(40.0f));
}

TEST_CASE("resolveRect: stretch-all with insets = the parent minus the insets",
	"[unit][uilayout]")
{
	LayoutRect parent{10, 20, 400, 300};
	LayoutNode node;
	applyAnchorPreset(node, LAP_STRETCH_ALL);
	node.setOffsets(16, 16, -16, -16);	// 16px inset on every edge

	const LayoutRect r = resolveRect(parent, node, 1.0f);
	CHECK(r.x == Approx(10.0f + 16.0f));
	CHECK(r.y == Approx(20.0f + 16.0f));
	CHECK(r.w == Approx(400.0f - 32.0f));
	CHECK(r.h == Approx(300.0f - 32.0f));
}

TEST_CASE("resolveRect: centre anchor + centre pivot centres the widget",
	"[unit][uilayout]")
{
	LayoutRect parent{0, 0, 800, 600};
	// a 200x100 widget, pivot at its centre, anchoredPosition 0 => centred
	const LayoutNode node = pointNode(LAP_CENTER, 0.5f, 0.5f, 200, 100, 0, 0);

	const LayoutRect r = resolveRect(parent, node, 1.0f);
	CHECK(r.w == Approx(200.0f));
	CHECK(r.h == Approx(100.0f));
	CHECK(r.x + r.w * 0.5f == Approx(400.0f));	// centre of the parent
	CHECK(r.y + r.h * 0.5f == Approx(300.0f));
}

TEST_CASE("resolveRect: layoutScale multiplies offsets but not anchor fractions",
	"[unit][uilayout]")
{
	LayoutRect parent{0, 0, 1000, 1000};
	LayoutNode node;
	applyAnchorPreset(node, LAP_TOPLEFT);
	node.setOffsets(10, 10, 10 + 50, 10 + 50);	// pos 10, size 50 (design units)

	const LayoutRect r = resolveRect(parent, node, 2.0f);
	// offsets scale: position 20, size 100
	CHECK(r.x == Approx(20.0f));
	CHECK(r.y == Approx(20.0f));
	CHECK(r.w == Approx(100.0f));
	CHECK(r.h == Approx(100.0f));

	// a stretch anchor's span comes from the parent fractions (unaffected by
	// scale); only its offsets scale
	LayoutNode stretch;
	applyAnchorPreset(stretch, LAP_STRETCH_ALL);
	stretch.setOffsets(10, 10, -10, -10);
	const LayoutRect s = resolveRect(parent, stretch, 3.0f);
	CHECK(s.x == Approx(30.0f));			// 10 * 3
	CHECK(s.w == Approx(1000.0f - 60.0f));	// span 1000 minus 10*3 both sides
}

TEST_CASE("resolveRect: nested parent rects compose", "[unit][uilayout]")
{
	LayoutRect window{0, 0, 1000, 800};
	// a panel filling the window minus 50px, then a child pinned top-right of it
	LayoutNode panelNode;
	applyAnchorPreset(panelNode, LAP_STRETCH_ALL);
	panelNode.setOffsets(50, 50, -50, -50);
	const LayoutRect panel = resolveRect(window, panelNode, 1.0f);
	REQUIRE(panel.x == Approx(50.0f));
	REQUIRE(panel.w == Approx(900.0f));

	LayoutNode childNode = pointNode(LAP_TOPRIGHT, 1.0f, 0.0f, 80, 30, -10, 10);
	const LayoutRect child = resolveRect(panel, childNode, 1.0f);
	// right edge 10px inside the panel's right edge; top 10px below its top
	CHECK((child.x + child.w) == Approx(panel.x + panel.w - 10.0f));
	CHECK(child.y == Approx(panel.y + 10.0f));
	CHECK(child.w == Approx(80.0f));
}

TEST_CASE("LayoutNode: anchoredPosition/sizeDelta accessors round-trip",
	"[unit][uilayout]")
{
	LayoutNode node;
	node.pivot.x = 0.5f;
	node.pivot.y = 1.0f;
	node.setSizeDelta(120, 60);
	node.setAnchoredPosition(30, -15);

	const LayoutVec2 size = node.sizeDelta();
	const LayoutVec2 pos = node.anchoredPosition();
	CHECK(size.x == Approx(120.0f));
	CHECK(size.y == Approx(60.0f));
	CHECK(pos.x == Approx(30.0f));
	CHECK(pos.y == Approx(-15.0f));

	// setting the size again must keep the anchoredPosition fixed
	node.setSizeDelta(200, 40);
	const LayoutVec2 pos2 = node.anchoredPosition();
	CHECK(pos2.x == Approx(30.0f));
	CHECK(pos2.y == Approx(-15.0f));
	CHECK(node.sizeDelta().x == Approx(200.0f));
}

TEST_CASE("resolveRect reproduces UiAnchor::place against the safe root "
	"(top-left)", "[unit][uilayout][safearea]")
{
	SafeAreaInsets insets;
	insets.mTop = 60;
	insets.mLeft = 20;
	const unsigned int W = 1179, H = 2556;

	float ax = 0.0f, ay = 0.0f;
	UiAnchor::place(200, 40, 16, 16, W, H, insets, false, false, ax, ay);

	// the resolver's equivalent: a top-left point anchor against the safe root
	LayoutRect safeRoot{float(insets.mLeft), float(insets.mTop),
		float(W - insets.mLeft - insets.mRight),
		float(H - insets.mTop - insets.mBottom)};
	LayoutNode node;
	applyAnchorPreset(node, LAP_TOPLEFT);
	node.setOffsets(16, 16, 16 + 200, 16 + 40);	// margin (16,16), size (200,40)
	const LayoutRect r = resolveRect(safeRoot, node, 1.0f);
	CHECK(r.x == Approx(ax));
	CHECK(r.y == Approx(ay));
}

TEST_CASE("resolveRect reproduces UiAnchor::place against the safe root "
	"(bottom-right)", "[unit][uilayout][safearea]")
{
	SafeAreaInsets insets;
	insets.mBottom = 40;
	insets.mRight = 24;
	const unsigned int W = 1179, H = 2556;

	float ax = 0.0f, ay = 0.0f;
	UiAnchor::place(200, 40, 16, 16, W, H, insets, true, true, ax, ay);

	LayoutRect safeRoot{float(insets.mLeft), float(insets.mTop),
		float(W - insets.mLeft - insets.mRight),
		float(H - insets.mTop - insets.mBottom)};
	LayoutNode node;
	applyAnchorPreset(node, LAP_BOTTOMRIGHT);
	node.pivot.x = 1.0f;
	node.pivot.y = 1.0f;
	// right/bottom edges 16px inside the safe edge; size 200x40
	node.setOffsets(-16 - 200, -16 - 40, -16, -16);
	const LayoutRect r = resolveRect(safeRoot, node, 1.0f);
	CHECK(r.x == Approx(ax));
	CHECK(r.y == Approx(ay));
}

TEST_CASE("applyAnchorPreset: the stretch presets span their axis",
	"[unit][uilayout]")
{
	LayoutNode all;
	applyAnchorPreset(all, LAP_STRETCH_ALL);
	CHECK(all.anchorMin.x == Approx(0.0f));
	CHECK(all.anchorMax.x == Approx(1.0f));
	CHECK(all.anchorMin.y == Approx(0.0f));
	CHECK(all.anchorMax.y == Approx(1.0f));

	LayoutNode bottom;
	applyAnchorPreset(bottom, LAP_STRETCH_BOTTOM);
	CHECK(bottom.anchorMin.x == Approx(0.0f));
	CHECK(bottom.anchorMax.x == Approx(1.0f));
	CHECK(bottom.anchorMin.y == Approx(1.0f));	// pinned to the bottom edge
	CHECK(bottom.anchorMax.y == Approx(1.0f));
}
