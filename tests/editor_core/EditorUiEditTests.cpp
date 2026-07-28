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

TEST_CASE("ui-edit: hit test prefers the deeper child inside its parent",
	"[unit][uiedit]")
{
	// the reported bug: a button parented into a decor panel could not be
	// click-selected because the parent (same z) swallowed the pick. A child is
	// DEEPER (depth > parent), so it must win at equal z regardless of submission
	// order - here the parent is submitted LAST (a later painter index).
	std::vector<UiRect> rects;
	UiRect parent{ "panel", 0, 0, 200, 200 };	// depth 0
	UiRect child{ "button", 40, 40, 80, 40 };	// depth 1, inside the panel
	child.depth = 1;
	// the child is submitted FIRST, the parent LAST (the swallow condition)
	rects.push_back(child);
	rects.push_back(parent);
	// a point inside both -> the child wins (deeper), never the parent
	CHECK(hitTestWidget(rects, 60, 55) == "button");
	// a point inside only the parent -> the parent
	CHECK(hitTestWidget(rects, 10, 10) == "panel");

	// z order is respected: a sibling with a HIGHER z wins over a deeper child
	UiRect overlay{ "overlay", 40, 40, 80, 40 };	// depth 0 but z 20 (on top)
	overlay.z = 20.0f;
	rects.push_back(overlay);
	CHECK(hitTestWidget(rects, 60, 55) == "overlay");
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

TEST_CASE("ui-edit: handle picking on a rect smaller than the grab tolerance "
	"resolves to the NEAREST edge", "[unit][uiedit]")
{
	// a widget whose screen height (4) is under the grab tolerance (6): the
	// pointer at the bottom-right corner is within grab of BOTH vertical edges,
	// and the fixed corner priority used to answer TopRight - the outward
	// resize then collapsed the height to zero. Nearest-edge disambiguation
	// keeps the corner the pointer actually touches.
	UiRect tiny{ "w", 100, 100, 16, 4 };	// x:100..116 y:100..104
	const float grab = 6.0f;
	CHECK(handleAt(tiny, 116, 104, grab) == UiHandle::BottomRight);
	CHECK(handleAt(tiny, 100, 100, grab) == UiHandle::TopLeft);
	CHECK(handleAt(tiny, 116, 100, grab) == UiHandle::TopRight);
	CHECK(handleAt(tiny, 100, 104, grab) == UiHandle::BottomLeft);
	// both dims tiny: every corner still resolves to itself
	UiRect dot{ "w", 50, 50, 4, 4 };
	CHECK(handleAt(dot, 54, 54, grab) == UiHandle::BottomRight);
	CHECK(handleAt(dot, 50, 50, grab) == UiHandle::TopLeft);
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

TEST_CASE("ui-edit: widget-name validation - blank / spaces / collision",
	"[unit][uiedit]")
{
	GuiLayoutDoc doc;
	GuiLayoutSection a; a.type = "Button"; a.id = "ok"; doc.sections.push_back(a);
	GuiLayoutSection b; b.type = "Label"; b.id = "title"; doc.sections.push_back(b);

	std::string err;
	CHECK_FALSE(isValidWidgetName(doc, "", "", err));		// empty
	CHECK_FALSE(err.empty());
	CHECK_FALSE(isValidWidgetName(doc, "my widget", "", err));	// space
	CHECK_FALSE(isValidWidgetName(doc, "title", "", err));		// collision
	// a fresh unique name is valid
	CHECK(isValidWidgetName(doc, "footer", "", err));
	CHECK(err.empty());
	// a name that collides with SELF is allowed (a no-op rename)
	CHECK(isValidWidgetName(doc, "title", "title", err));
}

TEST_CASE("ui-edit: rename a widget rewrites its children's parent refs and "
	"enforces uniqueness", "[unit][uiedit]")
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
	add("DecorWidget", "panel", "");
	add("Button", "ok", "panel");
	add("Label", "caption", "panel");
	add("Button", "cancel", "");

	std::string err;
	// a collision fails and changes nothing
	CHECK_FALSE(renameWidget(doc, "panel", "cancel", err));
	CHECK_FALSE(err.empty());
	CHECK(sectionIndex(doc, "panel") >= 0);
	CHECK(*sectionById(doc, "ok").find("parent") == "panel");

	// a valid rename updates the id AND every child's parent reference
	REQUIRE(renameWidget(doc, "panel", "hud", err));
	CHECK(err.empty());
	CHECK(sectionIndex(doc, "panel") < 0);
	CHECK(sectionIndex(doc, "hud") >= 0);
	CHECK(*sectionById(doc, "ok").find("parent") == "hud");
	CHECK(*sectionById(doc, "caption").find("parent") == "hud");
	CHECK(sectionById(doc, "cancel").find("parent") == nullptr);	// untouched

	// a no-op rename to the same id succeeds
	CHECK(renameWidget(doc, "hud", "hud", err));
	// renaming a missing widget fails
	CHECK_FALSE(renameWidget(doc, "nope", "x", err));

	// the rename round-trips through serialize (a fixed point)
	const std::string text = doc.serialize();
	GuiLayoutDoc reparsed;
	std::string perr;
	REQUIRE(GuiLayoutDoc::parse(text, reparsed, perr));
	CHECK(reparsed.serialize() == text);
	CHECK(sectionIndex(reparsed, "hud") >= 0);
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

//=============================================================================
//=== the ALIGNMENT-SWITCHING MATRIX ==========================================
//=============================================================================
// Every anchor preset (16) x every gizmo modifier variant (plain / keep-rect /
// also-pivot) applied to representative widget kinds whose STARTING geometry
// forms span the problem space (an offsets-form stretch caption - the reported
// bug's shape; a friendly-form point-anchored nine-slice decor; a full-stretch
// offsets textbox; a friendly-form point-anchored button). Each application is
// asserted for the whole outcome contract: (a) the doc-model anchor fields are
// exactly the preset's spec, (b) the resolved rect matches the pure-math
// expectation (keep-rect keeps the WHOLE rect; plain keeps the SIZE and the
// anchoredPosition, re-homing to the new anchor), (c) CONTENT CONTAINMENT - a
// NON-DEGENERATE (positive) box, the pure necessary-and-sufficient condition
// for caption/sprite/nine-slice geometry to stay inside the widget (the runtime
// only escapes a zero/negative box: a centred caption's cursor is
// left+width*0.5-textWidth*0.5 with clipping SKIPPED when width==0 - see
// UiCaption::_redraw; the live glyph-rect check is the editor_uiedit selfcheck's
// content leg), (d) save->reload->re-resolve equality, (e) undo restores the
// pre-apply text byte-exact. This matrix is the regression proof for the
// content-escapes-its-rect bug and the systematic net for its siblings.
namespace
{
	// the 16 preset names in LayoutAnchorPreset enum order (the key the section
	// carries; matches applyAnchorPresetToSection's own table)
	char const* const kPresetNames[16] = {
		"topleft","top","topright","left","center","right",
		"bottomleft","bottom","bottomright","stretchtop","stretchmiddle",
		"stretchbottom","stretchleft","stretchcenter","stretchright","stretchall" };

	// a representative widget section by kind, in its natural authored form
	GuiLayoutSection matrixSection(std::string const& kind)
	{
		GuiLayoutSection s;
		s.id = "w";
		if(kind == "label")
		{
			// the reported bug's shape: an offsets-form stretch-top caption with
			// the .oui default (centre) text alignment
			s.type = "Label";
			s.set("font", "9");
			s.set("text", "Content");
			s.set("anchor", "stretchtop");
			s.set("offsets", "24 16 -24 48");
			s.set("textAlignment", "center");
		}
		else if(kind == "decor")
		{
			// a friendly-form point-anchored nine-slice panel (settingsPanel shape)
			s.type = "DecorWidget";
			s.set("sprite", "panel");
			s.set("nineSlice", "true");
			s.set("anchor", "center");
			s.set("pivot", "0.5 0.5");
			s.set("anchoredPos", "0 0");
			s.set("sizeDelta", "460 460");
		}
		else if(kind == "textbox")
		{
			// a full-stretch offsets-form wrap textbox
			s.type = "TextBox";
			s.set("font", "9");
			s.set("text", "A long wrapped paragraph of body text");
			s.set("wrap", "true");
			s.set("anchor", "stretchall");
			s.set("offsets", "16 64 -16 -16");
		}
		else	// button
		{
			s.type = "Button";
			s.set("sprite", "button");
			s.set("font", "9");
			s.set("text", "OK");
			s.set("anchor", "topleft");
			s.set("anchoredPos", "40 120");
			s.set("sizeDelta", "160 44");
		}
		return s;
	}

	// the [Layout] + one widget document text for a kind (the canonical form)
	std::string matrixDocText(std::string const& kind)
	{
		GuiLayoutDoc doc;
		GuiLayoutSection layout;
		layout.type = "Layout";
		layout.set("atlas", "gui_default");
		doc.sections.push_back(layout);
		doc.sections.push_back(matrixSection(kind));
		return doc.serialize();
	}
}

TEST_CASE("ui-edit: anchor-preset switching matrix - fields, resolved rect, "
	"containment, round-trip, undo", "[unit][uiedit]")
{
	const LayoutRect parent{ 0.0f, 0.0f, 400.0f, 800.0f };
	const float scale = 2.0f;
	const char* kinds[] = { "label", "decor", "textbox", "button" };
	enum Variant { V_PLAIN = 0, V_KEEP = 1, V_PIVOT = 2 };
	const char* variantName[] = { "plain", "keep-rect", "also-pivot" };

	for(char const* kind : kinds)
	{
		for(int p = 0; p < 16; ++p)
		{
			const LayoutAnchorPreset preset = static_cast<LayoutAnchorPreset>(p);
			for(int v = 0; v < 3; ++v)
			{
				INFO("kind=" << kind << " preset=" << kPresetNames[p]
					<< " variant=" << variantName[v]);
				UiEditDoc edit;
				std::string err;
				REQUIRE(edit.load(matrixDocText(kind), err));
				const std::string textBefore = edit.text();
				GuiLayoutSection& sec = mutableSection(edit.doc(), "w");
				const LayoutNode nodeBefore = sectionLayoutNode(sec);
				const LayoutRect before = resolveSection(sec, parent, scale);
				const LayoutVec2 apBefore = nodeBefore.anchoredPosition();
				REQUIRE(before.w > 0.0f);
				REQUIRE(before.h > 0.0f);

				AnchorPresetMods mods;
				mods.alsoKeepRect = (v == V_KEEP);
				mods.alsoPivot = (v == V_PIVOT);
				edit.beginEdit();
				applyAnchorPresetToSection(sec, preset, mods, parent, scale);
				edit.commitEdit();

				// (a) the doc-model anchor fields ARE the preset's spec
				LayoutNode spec;
				applyAnchorPreset(spec, preset);
				const LayoutNode got = sectionLayoutNode(sec);
				CHECK_THAT(got.anchorMin.x, WithinAbs(spec.anchorMin.x, 1e-4f));
				CHECK_THAT(got.anchorMin.y, WithinAbs(spec.anchorMin.y, 1e-4f));
				CHECK_THAT(got.anchorMax.x, WithinAbs(spec.anchorMax.x, 1e-4f));
				CHECK_THAT(got.anchorMax.y, WithinAbs(spec.anchorMax.y, 1e-4f));
				REQUIRE(sec.find("anchor") != nullptr);
				CHECK(*sec.find("anchor") == kPresetNames[p]);
				CHECK(sec.find("anchorMin") == nullptr);	// named, not raw
				CHECK(sec.find("anchorMax") == nullptr);

				const LayoutRect after = resolveSection(sec, parent, scale);

				// (c) CONTENT CONTAINMENT: a non-degenerate box on every path
				CHECK(after.w > 0.0f);
				CHECK(after.h > 0.0f);

				// (b) the resolved rect matches the pure-math expectation
				if(v == V_KEEP)
				{
					// keep-rect pins the WHOLE on-screen rect
					CHECK_THAT(after.x, WithinAbs(before.x, 1e-2f));
					CHECK_THAT(after.y, WithinAbs(before.y, 1e-2f));
					CHECK_THAT(after.w, WithinAbs(before.w, 1e-2f));
					CHECK_THAT(after.h, WithinAbs(before.h, 1e-2f));
				}
				else
				{
					// plain / also-pivot preserve the SIZE (re-homing to the new
					// anchor); this is what keeps the caption inside its box across
					// a stretch<->point span change (the reported bug)
					CHECK_THAT(after.w, WithinAbs(before.w, 1e-2f));
					CHECK_THAT(after.h, WithinAbs(before.h, 1e-2f));
				}
				if(v == V_PLAIN)
				{
					// plain keeps the anchoredPosition (the pivot's offset from the
					// anchor point) - the widget follows the new anchor, no jump in
					// its anchor-relative placement
					CHECK_THAT(got.anchoredPosition().x,
						WithinAbs(apBefore.x, 1e-2f));
					CHECK_THAT(got.anchoredPosition().y,
						WithinAbs(apBefore.y, 1e-2f));
				}
				if(v == V_PIVOT)
				{
					// also-pivot lands the pivot on the preset point
					REQUIRE(sec.find("pivot") != nullptr);
					const LayoutVec2 pt = anchorPresetPoint(preset);
					CHECK_THAT(got.pivot.x, WithinAbs(pt.x, 1e-3f));
					CHECK_THAT(got.pivot.y, WithinAbs(pt.y, 1e-3f));
				}

				// (d) save -> reload -> re-resolve equality
				GuiLayoutDoc reloaded;
				std::string parseErr;
				REQUIRE(GuiLayoutDoc::parse(edit.text(), reloaded, parseErr));
				const LayoutRect afterReload =
					resolveSection(sectionById(reloaded, "w"), parent, scale);
				CHECK_THAT(afterReload.x, WithinAbs(after.x, 1e-2f));
				CHECK_THAT(afterReload.y, WithinAbs(after.y, 1e-2f));
				CHECK_THAT(afterReload.w, WithinAbs(after.w, 1e-2f));
				CHECK_THAT(afterReload.h, WithinAbs(after.h, 1e-2f));

				// (e) undo restores the pre-apply text byte-exact (a no-op apply
				// pushes no step and leaves the text already equal)
				if(edit.canUndo())
				{
					edit.undo();
				}
				CHECK(edit.text() == textBefore);
			}
		}
	}
}

TEST_CASE("ui-edit: interaction chain - preset -> drag -> preset keeps a valid "
	"box and the final preset's fields", "[unit][uiedit]")
{
	const LayoutRect parent{ 0.0f, 0.0f, 400.0f, 800.0f };
	const float scale = 2.0f;
	// start from the reported bug's shape (offsets-form stretch caption)
	GuiLayoutSection s = matrixSection("label");
	const LayoutRect start = resolveSection(s, parent, scale);

	// 1) re-anchor to centre (plain) - size preserved, box valid
	applyAnchorPresetToSection(s, LAP_CENTER, {}, parent, scale);
	LayoutRect r1 = resolveSection(s, parent, scale);
	CHECK(r1.w > 0.0f);
	CHECK_THAT(r1.w, WithinAbs(start.w, 1e-2f));

	// 2) drag it (a body move) - the pure move path the canvas uses
	applyMove(s, 30.0f, 20.0f, scale, 0.0f);
	LayoutRect r2 = resolveSection(s, parent, scale);
	CHECK_THAT(r2.x, WithinAbs(r1.x + 30.0f, 1e-2f));
	CHECK_THAT(r2.y, WithinAbs(r1.y + 20.0f, 1e-2f));
	CHECK_THAT(r2.w, WithinAbs(start.w, 1e-2f));	// drag never resizes

	// 3) re-anchor again to bottom-right (plain) - fields are the new preset,
	// the box stays valid and the same size
	applyAnchorPresetToSection(s, LAP_BOTTOMRIGHT, {}, parent, scale);
	LayoutRect r3 = resolveSection(s, parent, scale);
	CHECK(r3.w > 0.0f);
	CHECK(r3.h > 0.0f);
	CHECK_THAT(r3.w, WithinAbs(start.w, 1e-2f));
	REQUIRE(s.find("anchor") != nullptr);
	CHECK(*s.find("anchor") == std::string("bottomright"));
}

TEST_CASE("ui-edit: interaction chain - preset -> resize -> undo -> redo",
	"[unit][uiedit]")
{
	const LayoutRect parent{ 0.0f, 0.0f, 400.0f, 800.0f };
	const float scale = 2.0f;
	UiEditDoc edit;
	std::string err;
	REQUIRE(edit.load(matrixDocText("button"), err));
	const std::string t0 = edit.text();

	// preset (one step)
	edit.beginEdit();
	applyAnchorPresetToSection(mutableSection(edit.doc(), "w"), LAP_STRETCH_ALL,
		{}, parent, scale);
	edit.commitEdit();
	const std::string t1 = edit.text();
	CHECK(t1 != t0);
	const LayoutRect afterPreset =
		resolveSection(sectionById(edit.doc(), "w"), parent, scale);
	CHECK(afterPreset.w > 0.0f);

	// resize by a right-edge drag (a second step)
	edit.beginEdit();
	applyResize(mutableSection(edit.doc(), "w"), UiHandle::Right, 40.0f, 0.0f,
		scale, 0.0f);
	edit.commitEdit();
	const std::string t2 = edit.text();
	CHECK(t2 != t1);

	// undo the resize -> back to the preset state
	edit.undo();
	CHECK(edit.text() == t1);
	// undo the preset -> back to the original
	edit.undo();
	CHECK(edit.text() == t0);
	// redo both
	edit.redo();
	CHECK(edit.text() == t1);
	edit.redo();
	CHECK(edit.text() == t2);
}

TEST_CASE("ui-edit: a wrap label keeps a positive width across an anchor change "
	"(the height-for-width precondition)", "[unit][uiedit]")
{
	// A wrapped label measures its height from the width the resolver settles on
	// (GuiLabel::getHeightForWidthMeasurer, exercised live by the GuiManager
	// resolve pass). The editor's job is to never hand that path a degenerate
	// width - assert the resolved width stays positive across a stretch->point
	// re-anchor (the runtime height re-measure itself is a GuiManager concern,
	// covered by the layout resolver's own suite).
	const LayoutRect parent{ 0.0f, 0.0f, 400.0f, 800.0f };
	const float scale = 2.0f;
	GuiLayoutSection s = matrixSection("textbox");	// wrap = true, stretchall
	CHECK(resolveSection(s, parent, scale).w > 0.0f);
	applyAnchorPresetToSection(s, LAP_CENTER, {}, parent, scale);
	CHECK(resolveSection(s, parent, scale).w > 0.0f);
	applyAnchorPresetToSection(s, LAP_STRETCH_LEFT, {}, parent, scale);
	CHECK(resolveSection(s, parent, scale).w > 0.0f);
}

TEST_CASE("ui-edit: anchor preset on a layout-group child - the editor sets the "
	"fields; the runtime group owns the rect (documented caveat)",
	"[unit][uiedit]")
{
	// A widget parented into a layout GROUP has its rect ASSIGNED by the group's
	// arrange pass at runtime (GuiManager::resolveLayouts), so its own anchor
	// preset does not place it - a documented v1 caveat. The editor still lets
	// you set the anchor (the doc field updates and the editor's own doc-resolve,
	// which does NOT simulate groups, reflects the anchor). This asserts BOTH
	// halves: the field is written, and the divergence is real (the editor's
	// doc-resolved rect follows the anchor, i.e. it is NOT clamped to a group cell
	// here - the parent is a plain container in this pure fixture).
	const LayoutRect parent{ 0.0f, 0.0f, 400.0f, 800.0f };
	const float scale = 2.0f;
	GuiLayoutSection child = matrixSection("button");
	child.set("parent", "content");	// a group parent in the real doc
	const LayoutRect before = resolveSection(child, parent, scale);
	applyAnchorPresetToSection(child, LAP_BOTTOMRIGHT, {}, parent, scale);
	// the field is written (the editor honoured the gesture)
	REQUIRE(child.find("anchor") != nullptr);
	CHECK(*child.find("anchor") == std::string("bottomright"));
	// the editor's doc-resolve (group-unaware) moved the rect to the new anchor -
	// this is the divergence the caveat names; the box stays valid either way
	const LayoutRect after = resolveSection(child, parent, scale);
	CHECK(after.w > 0.0f);
	CHECK(after.h > 0.0f);
	CHECK(after.x != before.x);	// re-homed in the group-unaware editor resolve
}

//=============================================================================
//=== the PALETTE-ADD matrix ==================================================
//=============================================================================
// The palette-add path (paletteSection) picks a freshly added widget's DEFAULT
// geometry. A degenerate (zero-size) default is the content-escapes bug's OTHER
// door: a centred caption whose box has width 0 skips the per-glyph clip (@see
// UiCaption::_redraw) and spills outside its rect - exactly what the owner saw
// when ADDING a label (its default carried an anchoredPos but no sizeDelta, so
// the resolved box was 0-wide). This matrix asserts EVERY palette kind lands a
// positive (containing) default box, and that the box stays positive across an
// immediate re-anchor to every preset (the add -> anchor-switch chain).
namespace
{
	// a [Layout] + the palette default for a kind, serialized (the doc the
	// editor writes when a widget is dropped onto an empty canvas)
	std::string paletteDocText(std::string const& kind)
	{
		GuiLayoutDoc doc;
		GuiLayoutSection layout;
		layout.type = "Layout";
		layout.set("atlas", "gui_default");
		doc.sections.push_back(layout);
		doc.sections.push_back(paletteSection(doc, kind, ""));
		return doc.serialize();
	}
	// the palette section's id in a freshly loaded palette doc (the [Layout]
	// carries no id, so the one non-empty-id section is the added widget)
	std::string firstWidgetId(GuiLayoutDoc const& doc)
	{
		for(GuiLayoutSection const& s : doc.sections)
		{
			if(!s.id.empty()) { return s.id; }
		}
		return std::string();
	}
}

TEST_CASE("ui-edit: palette-add of every widget kind lands a positive box "
	"(content containment)", "[unit][uiedit]")
{
	const LayoutRect parent{ 0.0f, 0.0f, 400.0f, 800.0f };
	const float scale = 2.0f;
	for(UiWidgetKind const& kind : uiWidgetKinds())
	{
		INFO("kind=" << kind.type);
		GuiLayoutDoc doc;
		const GuiLayoutSection s = paletteSection(doc, kind.type, "");
		const LayoutRect r = resolveSection(s, parent, scale);
		// a NON-DEGENERATE box is the necessary-and-sufficient containment
		// condition (the runtime only escapes a zero/negative box - @see the
		// alignment matrix note and UiCaption::_redraw)
		CHECK(r.w > 0.0f);
		CHECK(r.h > 0.0f);
	}
}

TEST_CASE("ui-edit: label/textbox palette defaults carry a positive sizeDelta "
	"(the reported add-a-label regression guard)", "[unit][uiedit]")
{
	// the direct regression guard: the fix is a positive default sizeDelta on
	// the text-bearing kinds (they used to carry none -> a 0-wide box)
	GuiLayoutDoc doc;
	for(char const* kind : { "label", "textbox" })
	{
		INFO("kind=" << kind);
		const GuiLayoutSection s = paletteSection(doc, kind, "");
		REQUIRE(s.find("sizeDelta") != nullptr);
		float w = 0.0f, h = 0.0f;
		std::istringstream in(*s.find("sizeDelta"));
		in >> w >> h;
		CHECK(w > 0.0f);
		CHECK(h > 0.0f);
	}
}

TEST_CASE("ui-edit: palette-add then re-anchor keeps a positive box across "
	"every preset (the add -> anchor-switch chain)", "[unit][uiedit]")
{
	const LayoutRect parent{ 0.0f, 0.0f, 400.0f, 800.0f };
	const float scale = 2.0f;
	for(UiWidgetKind const& kind : uiWidgetKinds())
	{
		for(int p = 0; p < 16; ++p)
		{
			const LayoutAnchorPreset preset =
				static_cast<LayoutAnchorPreset>(p);
			INFO("kind=" << kind.type << " preset=" << kPresetNames[p]);
			UiEditDoc edit;
			std::string err;
			REQUIRE(edit.load(paletteDocText(kind.type), err));
			const std::string id = firstWidgetId(edit.doc());
			REQUIRE(!id.empty());
			GuiLayoutSection& sec = mutableSection(edit.doc(), id);
			// the added widget starts with a positive box (the fix)
			REQUIRE(resolveSection(sec, parent, scale).w > 0.0f);

			edit.beginEdit();
			applyAnchorPresetToSection(sec, preset, {}, parent, scale);
			edit.commitEdit();

			// still positive after the re-anchor (containment holds), and the
			// saved file re-resolves to the same rect
			const LayoutRect after = resolveSection(sec, parent, scale);
			CHECK(after.w > 0.0f);
			CHECK(after.h > 0.0f);
			GuiLayoutDoc reloaded;
			std::string parseErr;
			REQUIRE(GuiLayoutDoc::parse(edit.text(), reloaded, parseErr));
			const LayoutRect afterReload =
				resolveSection(sectionById(reloaded, id), parent, scale);
			CHECK_THAT(afterReload.w, WithinAbs(after.w, 1e-2f));
			CHECK_THAT(afterReload.h, WithinAbs(after.h, 1e-2f));
		}
	}
}
