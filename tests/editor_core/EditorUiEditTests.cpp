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
