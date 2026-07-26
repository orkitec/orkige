/**************************************************************
	created:	2026/07/26 at 12:00
	filename: 	EditorUiEditTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the visual .oui editor's editing core
	(tools/editor/EditorUiEdit): hit-testing over resolved rects, the
	anchor-preserving move/resize math (both geometry modes) checked against
	the pure UiLayout resolver, palette placement, add/remove subtree,
	snapshot undo/redo with gesture grouping, and the serialize() round-trip
	on the real shipped sample .oui files. No renderer, no ImGui.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "EditorUiEdit.h"
#include "engine_gui/GuiLayout.h"
#include "core_util/UiLayout.h"

#include <fstream>
#include <sstream>
#include <string>

using namespace Orkige;
using namespace OrkigeEditor;
using Catch::Matchers::WithinAbs;

namespace
{
	std::string readFile(char const* path)
	{
		std::ifstream in(path, std::ios::binary);
		std::ostringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}
	// resolve a section's rect against a parent rect at a layout scale (the
	// same path GuiManager takes, exercised on the edited section)
	LayoutRect resolveSection(GuiLayoutSection const& s, LayoutRect const& parent,
		float scale)
	{
		return resolveRect(parent, sectionLayoutNode(s), scale);
	}
	GuiLayoutSection const& sectionById(GuiLayoutDoc const& doc, std::string const& id)
	{
		const int idx = sectionIndex(doc, id);
		REQUIRE(idx >= 0);
		return doc.sections[static_cast<size_t>(idx)];
	}
	GuiLayoutSection& mutableSection(GuiLayoutDoc& doc, std::string const& id)
	{
		const int idx = sectionIndex(doc, id);
		REQUIRE(idx >= 0);
		return doc.sections[static_cast<size_t>(idx)];
	}
}

TEST_CASE("ui-edit: hit test picks the topmost (last submitted) rect", "[unit][uiedit]")
{
	std::vector<UiRect> rects = {
		{ "back",  0,  0, 100, 100 },
		{ "front", 20, 20, 40, 40 },
	};
	// inside the overlap -> the later rect wins
	CHECK(hitTestWidget(rects, 30, 30) == "front");
	// only the back rect covers this point
	CHECK(hitTestWidget(rects, 5, 5) == "back");
	// outside everything
	CHECK(hitTestWidget(rects, 200, 200).empty());
}

TEST_CASE("ui-edit: handle picking - corners beat edges, interior is a move",
	"[unit][uiedit]")
{
	UiRect r{ "w", 100, 100, 200, 100 };	// x:100..300 y:100..200
	const float grab = 6.0f;
	CHECK(handleAt(r, 100, 100, grab) == UiHandle::TopLeft);
	CHECK(handleAt(r, 300, 200, grab) == UiHandle::BottomRight);
	CHECK(handleAt(r, 200, 100, grab) == UiHandle::Top);
	CHECK(handleAt(r, 300, 150, grab) == UiHandle::Right);
	CHECK(handleAt(r, 200, 150, grab) == UiHandle::Move);
	CHECK(handleAt(r, 500, 500, grab) == UiHandle::None);
}

TEST_CASE("ui-edit: geometry mode detection", "[unit][uiedit]")
{
	GuiLayoutSection layout;
	layout.type = "DecorWidget";
	layout.set("anchor", "center");
	layout.set("sizeDelta", "460 460");
	CHECK(geomMode(layout) == UiGeomMode::Layout);

	GuiLayoutSection absolute;
	absolute.type = "Label";
	absolute.set("position", "40 40");
	absolute.set("size", "100 20");
	CHECK(geomMode(absolute) == UiGeomMode::Absolute);
}

TEST_CASE("ui-edit: move on the offsets form shifts the resolved rect by the "
	"surface delta and preserves the anchor", "[unit][uiedit]")
{
	// a stretch-top widget in offsets form (the settingsTitle shape)
	GuiLayoutSection s;
	s.type = "Label";
	s.set("anchor", "stretchtop");
	s.set("offsets", "0 0 0 0");

	const LayoutRect parent{ 0, 0, 400, 800 };
	const float scale = 2.0f;
	const LayoutRect before = resolveSection(s, parent, scale);

	// drag 20 surface px right, 40 down
	applyMove(s, 20.0f, 40.0f, scale, 0.0f);

	// the anchor is untouched (still stretch-top: min.y==max.y==0, spans x)
	CHECK(s.find("anchor") != nullptr);
	CHECK(s.find("anchoredPos") == nullptr);	// offsets form kept
	// design delta = surfacePx / scale = 10, 20
	CHECK(*s.find("offsets") == "10 20 10 20");

	const LayoutRect after = resolveSection(s, parent, scale);
	CHECK_THAT(after.x - before.x, WithinAbs(20.0f, 1e-3f));
	CHECK_THAT(after.y - before.y, WithinAbs(40.0f, 1e-3f));
	// a move does not change the size
	CHECK_THAT(after.w - before.w, WithinAbs(0.0f, 1e-3f));
	CHECK_THAT(after.h - before.h, WithinAbs(0.0f, 1e-3f));
}

TEST_CASE("ui-edit: move on the friendly form shifts anchoredPos", "[unit][uiedit]")
{
	GuiLayoutSection s;
	s.type = "DecorWidget";
	s.set("anchor", "center");
	s.set("pivot", "0.5 0.5");
	s.set("anchoredPos", "0 0");
	s.set("sizeDelta", "460 460");

	const LayoutRect parent{ 0, 0, 1000, 1000 };
	const float scale = 1.0f;
	const LayoutRect before = resolveSection(s, parent, scale);

	applyMove(s, 30.0f, -10.0f, scale, 0.0f);
	CHECK(*s.find("anchoredPos") == "30 -10");
	CHECK(*s.find("sizeDelta") == "460 460");	// untouched

	const LayoutRect after = resolveSection(s, parent, scale);
	CHECK_THAT(after.x - before.x, WithinAbs(30.0f, 1e-3f));
	CHECK_THAT(after.y - before.y, WithinAbs(-10.0f, 1e-3f));
}

TEST_CASE("ui-edit: resize on the offsets form moves the dragged edge",
	"[unit][uiedit]")
{
	GuiLayoutSection s;
	s.type = "ScrollView";
	s.set("anchor", "stretchall");
	s.set("offsets", "16 64 -16 -16");

	const LayoutRect parent{ 0, 0, 500, 500 };
	const float scale = 1.0f;
	const LayoutRect before = resolveSection(s, parent, scale);

	// drag the bottom-right handle out by (10, 20) surface px
	applyResize(s, UiHandle::BottomRight, 10.0f, 20.0f, scale, 0.0f);
	CHECK(*s.find("offsets") == "16 64 -6 4");

	const LayoutRect after = resolveSection(s, parent, scale);
	CHECK_THAT(after.x, WithinAbs(before.x, 1e-3f));		// left edge fixed
	CHECK_THAT(after.y, WithinAbs(before.y, 1e-3f));		// top edge fixed
	CHECK_THAT(after.w - before.w, WithinAbs(10.0f, 1e-3f));
	CHECK_THAT(after.h - before.h, WithinAbs(20.0f, 1e-3f));
}

TEST_CASE("ui-edit: resize on the friendly form grows sizeDelta", "[unit][uiedit]")
{
	GuiLayoutSection s;
	s.type = "DecorWidget";
	s.set("anchor", "center");
	s.set("sizeDelta", "200 100");

	applyResize(s, UiHandle::Right, 40.0f, 0.0f, 2.0f, 0.0f);	// +20 design
	CHECK(*s.find("sizeDelta") == "220 100");
	// a left drag also grows (pivot fixed) - v1 documented behavior
	applyResize(s, UiHandle::Left, -20.0f, 0.0f, 2.0f, 0.0f);	// ddx=-10 -> dw=+10
	CHECK(*s.find("sizeDelta") == "230 100");
}

TEST_CASE("ui-edit: absolute move/resize edit position and size", "[unit][uiedit]")
{
	GuiLayoutSection s;
	s.type = "Label";
	s.set("position", "40 40");
	s.set("size", "100 20");

	applyMove(s, 10.0f, 5.0f, 1.0f, 0.0f);
	CHECK(*s.find("position") == "50 45");

	applyResize(s, UiHandle::Right, 30.0f, 0.0f, 1.0f, 0.0f);
	CHECK(*s.find("size") == "130 20");
}

TEST_CASE("ui-edit: move snaps to a design-unit grid", "[unit][uiedit]")
{
	GuiLayoutSection s;
	s.type = "Label";
	s.set("anchor", "topleft");
	s.set("anchoredPos", "0 0");
	// drag 23 surface px at scale 1, snap to a grid of 10 -> 20
	applyMove(s, 23.0f, 0.0f, 1.0f, 10.0f);
	CHECK(*s.find("anchoredPos") == "20 0");
}

TEST_CASE("ui-edit: palette section has a unique id + parent + defaults",
	"[unit][uiedit]")
{
	GuiLayoutDoc doc;
	GuiLayoutSection existing;
	existing.type = "DecorWidget";
	existing.id = "panel1";
	doc.sections.push_back(existing);

	const GuiLayoutSection button = paletteSection(doc, "button", "panel1");
	CHECK(button.type == "button");
	CHECK(button.id == "button1");
	REQUIRE(button.find("parent") != nullptr);
	CHECK(*button.find("parent") == "panel1");
	CHECK(button.find("anchor") != nullptr);

	// an unknown type falls back to a panel
	const GuiLayoutSection unknown = paletteSection(doc, "bogus", "");
	CHECK(unknown.type == "panel");
	CHECK(unknown.find("parent") == nullptr);	// missing parent not stamped
}

TEST_CASE("ui-edit: remove a subtree deletes the widget and its descendants",
	"[unit][uiedit]")
{
	GuiLayoutDoc doc;
	auto add = [&](std::string type, std::string id, std::string parent)
	{
		GuiLayoutSection s;
		s.type = std::move(type);
		s.id = std::move(id);
		if(!parent.empty()) { s.set("parent", parent); }
		doc.sections.push_back(s);
	};
	add("DecorWidget", "root", "");
	add("Button", "child", "root");
	add("Label", "grandchild", "child");
	add("Button", "sibling", "");

	const std::vector<std::string> removed = removeWidgetSubtree(doc, "root");
	CHECK(removed.size() == 3);	// root + child + grandchild
	CHECK(sectionIndex(doc, "root") < 0);
	CHECK(sectionIndex(doc, "child") < 0);
	CHECK(sectionIndex(doc, "grandchild") < 0);
	CHECK(sectionIndex(doc, "sibling") >= 0);	// untouched
}

TEST_CASE("ui-edit: UiEditDoc undo/redo groups a gesture into one step",
	"[unit][uiedit]")
{
	UiEditDoc edit;
	std::string error;
	REQUIRE(edit.load(
		"[Label a]\ntext = one\nanchoredPos = 0 0\n", error));
	REQUIRE(error.empty());
	CHECK_FALSE(edit.dirty());
	CHECK_FALSE(edit.canUndo());

	// one gesture, two mutations -> ONE undo step
	edit.beginEdit();
	applyMove(mutableSection(edit.doc(), "a"), 10, 0, 1.0f, 0.0f);
	applyMove(mutableSection(edit.doc(), "a"), 5, 0, 1.0f, 0.0f);
	edit.commitEdit();
	CHECK(edit.dirty());
	CHECK(edit.canUndo());
	CHECK(*sectionById(edit.doc(), "a").find("anchoredPos") == "15 0");

	edit.undo();
	CHECK(*sectionById(edit.doc(), "a").find("anchoredPos") == "0 0");
	CHECK_FALSE(edit.dirty());		// back at the saved baseline
	CHECK(edit.canRedo());

	edit.redo();
	CHECK(*sectionById(edit.doc(), "a").find("anchoredPos") == "15 0");

	edit.markSaved();
	CHECK_FALSE(edit.dirty());
}

TEST_CASE("ui-edit: a no-op gesture leaves the history untouched", "[unit][uiedit]")
{
	UiEditDoc edit;
	std::string error;
	REQUIRE(edit.load("[Label a]\ntext = one\n", error));
	edit.beginEdit();
	edit.commitEdit();	// nothing changed
	CHECK_FALSE(edit.canUndo());
	CHECK_FALSE(edit.dirty());
}

namespace
{
	// a helper to build a friendly-form layout section
	GuiLayoutSection layoutSection(std::string id, std::string anchor,
		std::string anchoredPos, std::string sizeDelta)
	{
		GuiLayoutSection s;
		s.type = "DecorWidget";
		s.id = std::move(id);
		s.set("anchor", anchor);
		s.set("anchoredPos", anchoredPos);
		s.set("sizeDelta", sizeDelta);
		return s;
	}
	// a surface rect that is absolute-mode aware (position/size relative to the
	// parent origin) - the same resolution the panel's docRectOf uses
	UiRect rectOf(GuiLayoutSection const& s, LayoutRect const& parent, float scale)
	{
		if(geomMode(s) == UiGeomMode::Absolute)
		{
			float p[2] = { 0, 0 };
			float sz[2] = { 0, 0 };
			if(String const* v = s.find("position"))
			{
				std::istringstream in(*v); in >> p[0] >> p[1];
			}
			if(String const* v = s.find("size"))
			{
				std::istringstream in(*v); in >> sz[0] >> sz[1];
			}
			return { s.id, parent.x + p[0] * scale, parent.y + p[1] * scale,
				sz[0] * scale, sz[1] * scale };
		}
		const LayoutRect r = resolveSection(s, parent, scale);
		return { s.id, r.x, r.y, r.w, r.h };
	}
}

TEST_CASE("ui-edit: anchor preset point is the anchor-rect centre", "[unit][uiedit]")
{
	CHECK_THAT(anchorPresetPoint(LAP_TOPLEFT).x, WithinAbs(0.0f, 1e-4f));
	CHECK_THAT(anchorPresetPoint(LAP_TOPLEFT).y, WithinAbs(0.0f, 1e-4f));
	CHECK_THAT(anchorPresetPoint(LAP_CENTER).x, WithinAbs(0.5f, 1e-4f));
	CHECK_THAT(anchorPresetPoint(LAP_BOTTOMRIGHT).x, WithinAbs(1.0f, 1e-4f));
	CHECK_THAT(anchorPresetPoint(LAP_BOTTOMRIGHT).y, WithinAbs(1.0f, 1e-4f));
	// a full stretch centres at (0.5, 0.5)
	CHECK_THAT(anchorPresetPoint(LAP_STRETCH_ALL).x, WithinAbs(0.5f, 1e-4f));
	CHECK_THAT(anchorPresetPoint(LAP_STRETCH_ALL).y, WithinAbs(0.5f, 1e-4f));
}

TEST_CASE("ui-edit: anchor preset gizmo - all 16 presets and the modifier "
	"variants", "[unit][uiedit]")
{
	const LayoutRect parent{ 0, 0, 400, 800 };
	const float scale = 2.0f;
	for(int i = 0; i < 16; ++i)
	{
		const LayoutAnchorPreset preset = static_cast<LayoutAnchorPreset>(i);

		// plain: anchors move to the preset, the on-screen rect is NOT kept
		GuiLayoutSection plain = layoutSection("w", "topleft", "40 60", "100 30");
		applyAnchorPresetToSection(plain, preset, {}, parent, scale);
		LayoutNode pn;
		applyAnchorPreset(pn, preset);
		const LayoutNode got = sectionLayoutNode(plain);
		CHECK_THAT(got.anchorMin.x, WithinAbs(pn.anchorMin.x, 1e-4f));
		CHECK_THAT(got.anchorMax.y, WithinAbs(pn.anchorMax.y, 1e-4f));

		// keep-rect: anchors change but the resolved rect is unchanged
		GuiLayoutSection keep = layoutSection("w", "topleft", "40 60", "100 30");
		const LayoutRect before = resolveSection(keep, parent, scale);
		AnchorPresetMods mods; mods.alsoKeepRect = true;
		applyAnchorPresetToSection(keep, preset, mods, parent, scale);
		const LayoutRect after = resolveSection(keep, parent, scale);
		CHECK_THAT(after.x, WithinAbs(before.x, 1e-2f));
		CHECK_THAT(after.y, WithinAbs(before.y, 1e-2f));
		CHECK_THAT(after.w, WithinAbs(before.w, 1e-2f));
		CHECK_THAT(after.h, WithinAbs(before.h, 1e-2f));
		// the anchor really changed to the preset
		const LayoutNode kn = sectionLayoutNode(keep);
		CHECK_THAT(kn.anchorMin.x, WithinAbs(pn.anchorMin.x, 1e-4f));

		// also-pivot: the pivot lands on the preset point
		GuiLayoutSection piv = layoutSection("w", "topleft", "40 60", "100 30");
		AnchorPresetMods pmods; pmods.alsoPivot = true;
		applyAnchorPresetToSection(piv, preset, pmods, parent, scale);
		REQUIRE(piv.find("pivot") != nullptr);
		const LayoutVec2 pt = anchorPresetPoint(preset);
		const LayoutNode pvn = sectionLayoutNode(piv);
		CHECK_THAT(pvn.pivot.x, WithinAbs(pt.x, 1e-3f));
		CHECK_THAT(pvn.pivot.y, WithinAbs(pt.y, 1e-3f));
	}
}

TEST_CASE("ui-edit: anchor drag keeps the on-screen rect", "[unit][uiedit]")
{
	const LayoutRect parent{ 0, 0, 1000, 1000 };
	const float scale = 1.0f;
	GuiLayoutSection s = layoutSection("w", "topleft", "100 100", "200 80");
	const LayoutRect before = resolveSection(s, parent, scale);

	// drag the min corner to the parent centre; rect must not jump
	applyAnchorDrag(s, UiAnchorCorner::Min, 0.5f, 0.5f, parent, scale);
	const LayoutRect after = resolveSection(s, parent, scale);
	CHECK_THAT(after.x, WithinAbs(before.x, 1e-2f));
	CHECK_THAT(after.y, WithinAbs(before.y, 1e-2f));
	CHECK_THAT(after.w, WithinAbs(before.w, 1e-2f));
	CHECK_THAT(after.h, WithinAbs(before.h, 1e-2f));
	// the raw anchor was written (custom drops the named preset)
	CHECK(s.find("anchor") == nullptr);
	REQUIRE(s.find("anchorMin") != nullptr);
}

TEST_CASE("ui-edit: pivot drag keeps the rect and re-derives anchoredPos",
	"[unit][uiedit]")
{
	const LayoutRect parent{ 0, 0, 1000, 1000 };
	const float scale = 1.0f;
	GuiLayoutSection s = layoutSection("w", "center", "0 0", "200 100");
	s.set("pivot", "0.5 0.5");
	const LayoutRect before = resolveSection(s, parent, scale);

	applyPivotDrag(s, 0.0f, 0.0f);	// pivot to the top-left corner
	const LayoutRect after = resolveSection(s, parent, scale);
	// the visible rect is unchanged
	CHECK_THAT(after.x, WithinAbs(before.x, 1e-2f));
	CHECK_THAT(after.y, WithinAbs(before.y, 1e-2f));
	CHECK_THAT(after.w, WithinAbs(before.w, 1e-2f));
	// anchoredPos was re-derived against the new pivot (offsetMin, since pivot=0)
	const LayoutNode n = sectionLayoutNode(s);
	CHECK_THAT(n.pivot.x, WithinAbs(0.0f, 1e-4f));
	CHECK_THAT(n.anchoredPosition().x, WithinAbs(n.offsetMin.x, 1e-3f));
}

TEST_CASE("ui-edit: align deltas snap each rect to the key's edge/centre",
	"[unit][uiedit]")
{
	std::vector<UiRect> rects = {
		{ "key",  100, 100, 200, 100 },	// key: x 100..300, cx 200, y 100..200, cy 150
		{ "a",     50,  50, 100,  40 },
		{ "b",    400, 400, 300, 200 },
	};
	// left: every rect's left -> 100
	auto dl = alignDeltas(rects, UiAlignOp::Left);
	CHECK_THAT(dl[0].x, WithinAbs(0.0f, 1e-4f));	// key holds
	CHECK_THAT(rects[1].left + dl[1].x, WithinAbs(100.0f, 1e-4f));
	CHECK_THAT(rects[2].left + dl[2].x, WithinAbs(100.0f, 1e-4f));
	// right: right edges -> 300
	auto dr = alignDeltas(rects, UiAlignOp::Right);
	CHECK_THAT(rects[1].left + rects[1].width + dr[1].x, WithinAbs(300.0f, 1e-4f));
	// hcenter: centres -> 200
	auto dc = alignDeltas(rects, UiAlignOp::HCenter);
	CHECK_THAT(rects[1].left + rects[1].width * 0.5f + dc[1].x,
		WithinAbs(200.0f, 1e-4f));
	// top: tops -> 100 ; bottom: bottoms -> 200 ; vcenter -> 150
	auto dt = alignDeltas(rects, UiAlignOp::Top);
	CHECK_THAT(rects[1].top + dt[1].y, WithinAbs(100.0f, 1e-4f));
	auto db = alignDeltas(rects, UiAlignOp::Bottom);
	CHECK_THAT(rects[2].top + rects[2].height + db[2].y, WithinAbs(200.0f, 1e-4f));
	auto dm = alignDeltas(rects, UiAlignOp::VCenter);
	CHECK_THAT(rects[2].top + rects[2].height * 0.5f + dm[2].y,
		WithinAbs(150.0f, 1e-4f));
}

TEST_CASE("ui-edit: align writes back through each widget's own geometry mode, "
	"across parents", "[unit][uiedit]")
{
	// key friendly-form at parent centre; a is offsets-form; c is absolute
	const LayoutRect parent{ 0, 0, 1000, 500 };
	const float scale = 1.0f;
	GuiLayoutSection key = layoutSection("key", "topleft", "100 100", "200 50");
	GuiLayoutSection a; a.type = "Label"; a.id = "a";
	a.set("anchor", "topleft"); a.set("offsets", "300 40 460 90");	// x 300..460
	GuiLayoutSection c; c.type = "Label"; c.id = "c";
	c.set("position", "500 200"); c.set("size", "80 30");

	std::vector<UiRect> rects = { rectOf(key, parent, scale),
		rectOf(a, parent, scale), rectOf(c, parent, scale) };
	const auto deltas = alignDeltas(rects, UiAlignOp::Left);
	// replay each surface delta through the per-mode writer (the panel's path)
	applyMove(a, deltas[1].x, deltas[1].y, scale, 0.0f);
	applyMove(c, deltas[2].x, deltas[2].y, scale, 0.0f);

	const float keyLeft = rectOf(key, parent, scale).left;
	CHECK_THAT(rectOf(a, parent, scale).left, WithinAbs(keyLeft, 1e-2f));
	CHECK_THAT(rectOf(c, parent, scale).left, WithinAbs(keyLeft, 1e-2f));
	// the forms are preserved (offsets stays offsets, absolute stays position)
	CHECK(a.find("offsets") != nullptr);
	CHECK(c.find("position") != nullptr);
}

TEST_CASE("ui-edit: distribute equalises the gaps between rects", "[unit][uiedit]")
{
	// three unequal-gap rects; distribute so the two gaps match
	std::vector<UiRect> rects = {
		{ "l",   0, 0, 100, 20 },	// 0..100
		{ "m", 150, 0,  50, 20 },	// 150..200
		{ "r", 500, 0, 100, 20 },	// 500..600
	};
	const auto d = distributeDeltas(rects, UiDistributeOp::Horizontal);
	// extremes hold
	CHECK_THAT(d[0].x, WithinAbs(0.0f, 1e-4f));
	CHECK_THAT(d[2].x, WithinAbs(0.0f, 1e-4f));
	// span 0..600, extents 100+50+100=250, free 350, gap 175
	// m's new left = 100 + 175 = 275
	CHECK_THAT(rects[1].left + d[1].x, WithinAbs(275.0f, 1e-3f));
	// fewer than three is a no-op
	std::vector<UiRect> pair = { rects[0], rects[2] };
	const auto none = distributeDeltas(pair, UiDistributeOp::Horizontal);
	CHECK_THAT(none[0].x, WithinAbs(0.0f, 1e-4f));
	CHECK_THAT(none[1].x, WithinAbs(0.0f, 1e-4f));
}

TEST_CASE("ui-edit: marquee selects intersecting rects", "[unit][uiedit]")
{
	std::vector<UiRect> rects = {
		{ "in",   10, 10, 20, 20 },	// inside
		{ "edge", 90, 90, 40, 40 },	// straddles the corner
		{ "out", 300, 300, 10, 10 },
	};
	// marquee 0,0 -> 100,100 (any corner order)
	const auto hit = widgetsInMarquee(rects, 100, 100, 0, 0);
	REQUIRE(hit.size() == 2);
	CHECK(hit[0] == "in");
	CHECK(hit[1] == "edge");
}

TEST_CASE("ui-edit: guide candidates + snap within the threshold", "[unit][uiedit]")
{
	std::vector<UiRect> others = { { "s", 100, 50, 40, 40 } };	// left 100, cx 120
	const UiRect parent{ "", 0, 0, 400, 400 };	// centre 200,200
	const auto cands = guideCandidates(others, parent, true, 200, 200);
	// 3 vlines + 3 hlines per sibling, +3+3 parent, +1+1 design centre = 14
	CHECK(cands.size() == 14);

	// a moving rect whose left is 3px shy of the sibling's left (100) snaps
	UiRect moving{ "m", 97, 300, 30, 30 };
	const UiSnap nearSnap = snapToGuides(moving, cands, 6.0f);
	CHECK(nearSnap.snappedX);
	CHECK_THAT(nearSnap.dx, WithinAbs(3.0f, 1e-4f));	// +3 -> left == 100
	CHECK_THAT(nearSnap.guideX, WithinAbs(100.0f, 1e-4f));

	// out of threshold on every edge/centre: no snap (left 60, cx 70, right 80;
	// nearest candidate is the sibling left at 100, a 20px gap)
	UiRect farRect{ "m", 60, 300, 20, 20 };
	const UiSnap farSnap = snapToGuides(farRect, cands, 6.0f);
	CHECK_FALSE(farSnap.snappedX);
}

TEST_CASE("ui-edit: coalesced nudges fold into one undo step", "[unit][uiedit]")
{
	UiEditDoc edit;
	std::string error;
	REQUIRE(edit.load("[Label a]\nanchoredPos = 0 0\n", error));

	// three nudges on the same key -> ONE undo step back to the start
	for(int i = 0; i < 3; ++i)
	{
		edit.beginCoalesced("nudge:a");
		applyMove(mutableSection(edit.doc(), "a"), 5, 0, 1.0f, 0.0f);
		edit.commitEdit();
	}
	CHECK(*sectionById(edit.doc(), "a").find("anchoredPos") == "15 0");
	CHECK(edit.canUndo());
	edit.undo();
	CHECK(*sectionById(edit.doc(), "a").find("anchoredPos") == "0 0");
	CHECK_FALSE(edit.canUndo());	// the whole burst was one step

	// a different key starts a fresh step (no merge)
	edit.beginCoalesced("nudge:a");
	applyMove(mutableSection(edit.doc(), "a"), 5, 0, 1.0f, 0.0f);
	edit.commitEdit();
	edit.beginCoalesced("nudge:other");
	applyMove(mutableSection(edit.doc(), "a"), 5, 0, 1.0f, 0.0f);
	edit.commitEdit();
	edit.undo();	// undoes only the "other" burst
	CHECK(*sectionById(edit.doc(), "a").find("anchoredPos") == "5 0");
}

TEST_CASE("ui-edit: serialize round-trip is a fixed point on the sample .oui set",
	"[unit][uiedit][oui]")
{
	// The doc model's canonical form drops comments/reflows spacing, so a raw
	// file is not byte-identical after one pass; the CONTRACT is that the
	// canonical form is a fixed point (a second pass changes nothing). The
	// visual editor saves this canonical text.
	char const* samples[] = {
		ORKIGE_UI_SAMPLE_SETTINGS,
		ORKIGE_UI_SAMPLE_BENCH_HUD,
		ORKIGE_UI_SAMPLE_BENCH_SETTINGS,
		ORKIGE_UI_SAMPLE_MATRIX,
	};
	for(char const* path : samples)
	{
		const std::string raw = readFile(path);
		REQUIRE_FALSE(raw.empty());
		GuiLayoutDoc doc;
		std::string error;
		REQUIRE(GuiLayoutDoc::parse(raw, doc, error));
		const std::string canonical = doc.serialize();

		GuiLayoutDoc again;
		REQUIRE(GuiLayoutDoc::parse(canonical, again, error));
		CHECK(again.serialize() == canonical);	// idempotent (byte-identical)
	}
}

TEST_CASE("ui-edit: edit -> save -> reload -> resolve equality", "[unit][uiedit][oui]")
{
	const std::string raw = readFile(ORKIGE_UI_SAMPLE_SETTINGS);
	REQUIRE_FALSE(raw.empty());
	UiEditDoc edit;
	std::string error;
	REQUIRE(edit.load(raw, error));

	// move the title label, capture its resolved rect delta
	const LayoutRect parent{ 0, 0, 460, 460 };
	const float scale = 1.0f;
	const LayoutRect before =
		resolveSection(sectionById(edit.doc(), "settingsTitle"), parent, scale);

	edit.beginEdit();
	applyMove(mutableSection(edit.doc(), "settingsTitle"), 12, 8, scale, 0.0f);
	edit.commitEdit();

	// "save" = serialize; "reload" = parse the serialized text afresh
	const std::string saved = edit.text();
	GuiLayoutDoc reloaded;
	REQUIRE(GuiLayoutDoc::parse(saved, reloaded, error));
	const LayoutRect after =
		resolveSection(sectionById(reloaded, "settingsTitle"), parent, scale);

	CHECK_THAT(after.x - before.x, WithinAbs(12.0f, 1e-3f));
	CHECK_THAT(after.y - before.y, WithinAbs(8.0f, 1e-3f));
}

TEST_CASE("ui-edit: surface->screen rect mapping under a device preset",
	"[unit][uiedit]")
{
	// a real notch-phone-like preset surface (device pixels), fit into a canvas
	// column and centred the way the Preview panel places the composite image.
	UiCanvasPlacement c;
	c.surfaceW = 1179.0f;	// device px (content scale is baked into the surface)
	c.surfaceH = 2556.0f;
	c.imageX = 100.0f;		// screen offset of the fitted image
	c.imageY = 40.0f;
	c.drawH = 900.0f;		// height-fit
	c.drawW = c.drawH * (c.surfaceW / c.surfaceH);	// keep aspect

	// a stretchtop widget resolves to the FULL surface width - it must map to
	// EXACTLY the image rect, never wider than the device screen (bug (b) guard)
	const UiRect stretchTop{ "bar", 0.0f, 0.0f, c.surfaceW, 160.0f };
	const UiRect m = mapSurfaceRectToScreen(c, stretchTop);
	CHECK_THAT(m.left, WithinAbs(c.imageX, 1e-3f));
	CHECK_THAT(m.width, WithinAbs(c.drawW, 1e-3f));
	CHECK(m.left + m.width <= c.imageX + c.drawW + 1e-3f);

	// an interior widget maps proportionally and stays inside the canvas
	const UiRect inner{ "btn", 300.0f, 500.0f, 400.0f, 120.0f };
	const UiRect mi = mapSurfaceRectToScreen(c, inner);
	const float sx = c.drawW / c.surfaceW;
	CHECK_THAT(mi.left, WithinAbs(c.imageX + 300.0f * sx, 1e-3f));
	CHECK_THAT(mi.width, WithinAbs(400.0f * sx, 1e-3f));
	CHECK(mi.left >= c.imageX - 1e-3f);
	CHECK(mi.left + mi.width <= c.imageX + c.drawW + 1e-3f);
}

TEST_CASE("ui-edit: adornment bounds vs the canvas clip rect", "[unit][uiedit]")
{
	// the canvas image rect the panel pushes as the adornment clip
	UiCanvasPlacement c;
	c.surfaceW = 1179.0f; c.surfaceH = 2556.0f;
	c.imageX = 100.0f; c.imageY = 40.0f;
	c.drawH = 900.0f;
	c.drawW = c.drawH * (c.surfaceW / c.surfaceH);
	const float canvasRight = c.imageX + c.drawW;

	// a full-width (stretch) selection: its outline maps to the image edges, so
	// the grips (handlePad) reach JUST PAST the canvas - exactly why the panel
	// clips to the canvas rect. The bounds prove the mapping is in-surface (the
	// outline itself never exceeds the canvas) while the pad is what the clip trims.
	std::vector<UiRect> sel{ { "bar", 0.0f, 0.0f, c.surfaceW, 160.0f } };
	const UiRect noPad = adornmentBoundsScreen(c, sel, 0.0f);
	CHECK(noPad.left >= c.imageX - 1e-3f);
	CHECK(noPad.left + noPad.width <= canvasRight + 1e-3f);
	const UiRect withPad = adornmentBoundsScreen(c, sel, 7.0f);
	CHECK(withPad.left < c.imageX);				// a grip pokes left of the canvas
	CHECK(withPad.left + withPad.width > canvasRight);	// and right of it

	// an interior selection: even with the grip pad it stays inside the canvas
	std::vector<UiRect> inner{ { "btn", 300.0f, 500.0f, 400.0f, 120.0f } };
	const UiRect ib = adornmentBoundsScreen(c, inner, 7.0f);
	CHECK(ib.left >= c.imageX - 1e-3f);
	CHECK(ib.left + ib.width <= canvasRight + 1e-3f);

	// an empty selection yields a zero box (nothing to bound)
	const UiRect empty = adornmentBoundsScreen(c, {}, 7.0f);
	CHECK(empty.width == 0.0f);
	CHECK(empty.height == 0.0f);
}
