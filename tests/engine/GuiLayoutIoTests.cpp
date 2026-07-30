/**************************************************************
	created:	2026/07/11 at 16:30
	filename: 	GuiLayoutIoTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the .oui declarative-layout document model
	(engine_gui/GuiLayout): parse into ordered sections/entries, the
	round-trip (parse -> serialize -> parse is stable), and honest failure on
	malformed input. No renderer, no window - pure text, so it also covers the
	ORKIGE_NOSCRIPT path (the loader must not require Lua).
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "engine_gui/GuiLayout.h"

using namespace Orkige;

TEST_CASE("oui: parse reads sections + ordered entries", "[unit][oui]")
{
	const String text =
		"# a comment\n"
		"[Layout]\n"
		"atlas = gui_default\n"
		"design = 1280 720 0.5\n"
		"\n"
		"[ScrollView settings]\n"
		"z = 5\n"
		"anchor = stretchall\n"
		"offsets = 20 20 -20 -20\n";

	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));
	REQUIRE(error.empty());
	REQUIRE(doc.sections.size() == 2);

	CHECK(doc.sections[0].type == "Layout");
	CHECK(doc.sections[0].id.empty());
	REQUIRE(doc.sections[0].find("atlas") != nullptr);
	CHECK(*doc.sections[0].find("atlas") == "gui_default");
	CHECK(*doc.sections[0].find("design") == "1280 720 0.5");

	CHECK(doc.sections[1].type == "ScrollView");
	CHECK(doc.sections[1].id == "settings");
	CHECK(*doc.sections[1].find("anchor") == "stretchall");
	CHECK(*doc.sections[1].find("offsets") == "20 20 -20 -20");
}

TEST_CASE("oui: accepts ':' and tab separators like the classic grammar",
	"[unit][oui]")
{
	const String text =
		"[Label title]\n"
		"text : Hello\n"
		"font\t24\n";
	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));
	REQUIRE(doc.sections.size() == 1);
	CHECK(*doc.sections[0].find("text") == "Hello");
	CHECK(*doc.sections[0].find("font") == "24");
}

TEST_CASE("oui: round-trips through serialize -> parse -> serialize",
	"[unit][oui]")
{
	const String text =
		"[Layout]\n"
		"atlas = gui_default\n"
		"root = safearea\n"
		"\n"
		"[DecorWidget panel]\n"
		"sprite = panel\n"
		"anchor = stretchall\n"
		"offsets = 16 16 -16 -16\n"
		"nineSlice = true\n"
		"\n"
		"[Label title]\n"
		"parent = panel\n"
		"font = 24\n"
		"text = @settings.title\n";

	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));

	// the serialized canonical form re-parses to an identical document
	const String canonical = doc.serialize();
	GuiLayoutDoc doc2;
	REQUIRE(GuiLayoutDoc::parse(canonical, doc2, error));
	CHECK(doc2.serialize() == canonical);

	// and the canonical form equals the (already-canonical) input
	CHECK(canonical == text);
}

TEST_CASE("oui: the screen-level `input` key round-trips (on/off/absent)",
	"[unit][oui]")
{
	// the input-enablement key rides the [Layout] header like design/root: a
	// plain value the generic document model preserves through a round-trip.
	// GuiFactory::loadLayout reads it as an explicit input choice (@see the
	// gui_input auto-enable selfcheck).
	SECTION("input off is preserved")
	{
		const String text = "[Layout]\natlas = gui_default\ninput = off\n";
		GuiLayoutDoc doc;
		String error;
		REQUIRE(GuiLayoutDoc::parse(text, doc, error));
		REQUIRE(doc.sections.size() == 1);
		REQUIRE(doc.sections[0].find("input") != nullptr);
		CHECK(*doc.sections[0].find("input") == "off");
		// canonical form round-trips
		const String canonical = doc.serialize();
		GuiLayoutDoc doc2;
		REQUIRE(GuiLayoutDoc::parse(canonical, doc2, error));
		REQUIRE(doc2.findSection("Layout") != nullptr);
		REQUIRE(doc2.findSection("Layout")->find("input") != nullptr);
		CHECK(*doc2.findSection("Layout")->find("input") == "off");
	}
	SECTION("input on is preserved")
	{
		const String text = "[Layout]\ninput = on\n";
		GuiLayoutDoc doc;
		String error;
		REQUIRE(GuiLayoutDoc::parse(text, doc, error));
		REQUIRE(doc.sections[0].find("input") != nullptr);
		CHECK(*doc.sections[0].find("input") == "on");
	}
	SECTION("an absent input key is simply not present")
	{
		const String text = "[Layout]\natlas = gui_default\n";
		GuiLayoutDoc doc;
		String error;
		REQUIRE(GuiLayoutDoc::parse(text, doc, error));
		CHECK(doc.sections[0].find("input") == nullptr);
	}
}

TEST_CASE("oui: a button's nineSlice / tiled draw-mode keys round-trip",
	"[unit][oui]")
{
	// the draw-mode keys ride a [Button] section (GuiFactory::loadLayout applies
	// them to the button's decor - previously decor-only, now buttons too); the
	// generic document model preserves them through a parse -> serialize -> parse
	// round-trip. The APPLY (the decor's DrawMode flips) is asserted end to end
	// in the demo_oui selfcheck; here we pin the doc-model contract.
	const String text =
		"[Button nine]\n"
		"sprite = button\n"
		"nineSlice = true\n"
		"\n"
		"[Button tiles]\n"
		"sprite = button\n"
		"tiled = true\n";

	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));
	REQUIRE(doc.sections.size() == 2);
	REQUIRE(doc.sections[0].find("nineSlice") != nullptr);
	CHECK(*doc.sections[0].find("nineSlice") == "true");
	REQUIRE(doc.sections[1].find("tiled") != nullptr);
	CHECK(*doc.sections[1].find("tiled") == "true");

	// the canonical form re-parses to an identical document (the keys survive)
	const String canonical = doc.serialize();
	GuiLayoutDoc doc2;
	REQUIRE(GuiLayoutDoc::parse(canonical, doc2, error));
	CHECK(doc2.serialize() == canonical);
	REQUIRE(doc2.sections.size() == 2);
	CHECK(*doc2.sections[0].find("nineSlice") == "true");
	CHECK(*doc2.sections[1].find("tiled") == "true");
}

TEST_CASE("oui: a [TabBar] section round-trips (tabs/panels/selected)",
	"[unit][oui]")
{
	// the tab bar is pure composition the loader wires after the widgets exist:
	// `tabs` names the tab checkboxes, `panels` the sibling content widgets shown
	// per selection, `selected` the initial tab. The generic document model
	// preserves the keys through a parse -> serialize -> parse round-trip; the
	// APPLY (panel visibility follows the selected tab) is asserted end to end in
	// the demo_oui selfcheck.
	const String text =
		"[TabBar mainTabs]\n"
		"tabs = tabA tabB tabC\n"
		"panels = panelA panelB panelC\n"
		"selected = 1\n";

	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));
	REQUIRE(doc.sections.size() == 1);
	CHECK(doc.sections[0].type == "TabBar");
	CHECK(doc.sections[0].id == "mainTabs");
	REQUIRE(doc.sections[0].find("tabs") != nullptr);
	CHECK(*doc.sections[0].find("tabs") == "tabA tabB tabC");
	CHECK(*doc.sections[0].find("panels") == "panelA panelB panelC");
	CHECK(*doc.sections[0].find("selected") == "1");

	const String canonical = doc.serialize();
	GuiLayoutDoc doc2;
	REQUIRE(GuiLayoutDoc::parse(canonical, doc2, error));
	CHECK(doc2.serialize() == canonical);
}

TEST_CASE("oui: a [ListView] section round-trips (items pipe-separated)",
	"[unit][oui]")
{
	// the list view is a scroll viewport with a built-in vertical content group;
	// `items` seeds initial rows (pipe-separated so a label may hold spaces). The
	// doc model preserves it; the APPLY (rows created, list re-flows + scrolls) is
	// asserted in the demo_oui selfcheck.
	const String text =
		"[ListView inventory]\n"
		"z = 6\n"
		"anchor = stretchall\n"
		"offsets = 8 8 -8 -8\n"
		"items = Sword | Shield | Potion of Healing\n";

	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));
	REQUIRE(doc.sections.size() == 1);
	CHECK(doc.sections[0].type == "ListView");
	CHECK(doc.sections[0].id == "inventory");
	REQUIRE(doc.sections[0].find("items") != nullptr);
	CHECK(*doc.sections[0].find("items") == "Sword | Shield | Potion of Healing");

	const String canonical = doc.serialize();
	GuiLayoutDoc doc2;
	REQUIRE(GuiLayoutDoc::parse(canonical, doc2, error));
	CHECK(doc2.serialize() == canonical);
	REQUIRE(doc2.sections.size() == 1);
	CHECK(*doc2.sections[0].find("items") == "Sword | Shield | Potion of Healing");
}

TEST_CASE("oui: a label's wrap key round-trips", "[unit][oui]")
{
	// wrap-to-width rides a [Label] (or [Textbox]) section as a plain value the
	// generic document model preserves; GuiFactory::loadLayout applies it to the
	// caption. The APPLY (the label wraps + grows under content-size-fit) is
	// asserted end to end in the demo_layout selfcheck.
	const String text =
		"[Label body]\n"
		"anchor = stretchtop\n"
		"offsets = 0 0 0 0\n"
		"fit = none preferred\n"
		"wrap = true\n";

	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));
	REQUIRE(doc.sections.size() == 1);
	REQUIRE(doc.sections[0].find("wrap") != nullptr);
	CHECK(*doc.sections[0].find("wrap") == "true");

	const String canonical = doc.serialize();
	GuiLayoutDoc doc2;
	REQUIRE(GuiLayoutDoc::parse(canonical, doc2, error));
	CHECK(doc2.serialize() == canonical);
	REQUIRE(doc2.sections[0].find("wrap") != nullptr);
	CHECK(*doc2.sections[0].find("wrap") == "true");
}

TEST_CASE("oui: a text entry's multiline key round-trips", "[unit][oui]")
{
	// `multiline = true` turns the field into a text area (soft wrap, Return
	// inserts a line break). The APPLY (the wrapped line count grows, the view
	// scrolls to follow the caret) is asserted in the player_gallery selfcheck.
	const String text =
		"[TextEntry notes]\n"
		"sprite = select_menu_field\n"
		"text = notes\n"
		"anchor = stretchtop\n"
		"offsets = 0 0 0 120\n"
		"multiline = true\n"
		"maxLength = 400\n";

	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));
	REQUIRE(doc.sections.size() == 1);
	CHECK(doc.sections[0].type == "TextEntry");
	REQUIRE(doc.sections[0].find("multiline") != nullptr);
	CHECK(*doc.sections[0].find("multiline") == "true");

	const String canonical = doc.serialize();
	GuiLayoutDoc doc2;
	REQUIRE(GuiLayoutDoc::parse(canonical, doc2, error));
	CHECK(doc2.serialize() == canonical);
	REQUIRE(doc2.sections[0].find("multiline") != nullptr);
	CHECK(*doc2.sections[0].find("multiline") == "true");
	CHECK(*doc2.sections[0].find("maxLength") == "400");
}

TEST_CASE("oui: a list view's virtualized / itemHeight keys round-trip",
	"[unit][oui]")
{
	// a virtualized list materialises only the rows the viewport shows; the
	// uniform row height is the contract that makes the window computable
	const String text =
		"[ListView bigList]\n"
		"z = 6\n"
		"anchor = stretchall\n"
		"offsets = 8 8 -8 -8\n"
		"itemHeight = 28\n"
		"virtualized = true\n";

	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));
	REQUIRE(doc.sections.size() == 1);
	REQUIRE(doc.sections[0].find("virtualized") != nullptr);
	CHECK(*doc.sections[0].find("virtualized") == "true");
	CHECK(*doc.sections[0].find("itemHeight") == "28");

	const String canonical = doc.serialize();
	GuiLayoutDoc doc2;
	REQUIRE(GuiLayoutDoc::parse(canonical, doc2, error));
	CHECK(doc2.serialize() == canonical);
	CHECK(*doc2.sections[0].find("virtualized") == "true");
	CHECK(*doc2.sections[0].find("itemHeight") == "28");
}

TEST_CASE("oui: the text-style keys (font/textColor/textScale) round-trip",
	"[unit][oui]")
{
	// the three styling keys ride any text-bearing section as plain values the
	// generic document model preserves. `font` takes EITHER the atlas
	// `[Font.N]` index or the role NAME a font declares - both are just text
	// here, and GuiFactory resolves them through the atlas. The APPLY (the
	// caption really swaps font / ink / size) is asserted end to end in the
	// player_gallery selfcheck; the VALUE verdicts live in GuiStyleTests.
	const String text =
		"[Label title]\n"
		"text = Hello\n"
		"font = heading\n"
		"textColor = 1 0.82 0.35 1\n"
		"textScale = 1.5\n"
		"\n"
		"[Button play]\n"
		"font = 24\n";

	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));
	REQUIRE(doc.sections.size() == 2);
	CHECK(*doc.sections[0].find("font") == "heading");
	CHECK(*doc.sections[0].find("textColor") == "1 0.82 0.35 1");
	CHECK(*doc.sections[0].find("textScale") == "1.5");
	CHECK(*doc.sections[1].find("font") == "24");	// the index form survives

	const String canonical = doc.serialize();
	GuiLayoutDoc doc2;
	REQUIRE(GuiLayoutDoc::parse(canonical, doc2, error));
	CHECK(doc2.serialize() == canonical);
	CHECK(*doc2.sections[0].find("font") == "heading");
	CHECK(*doc2.sections[0].find("textColor") == "1 0.82 0.35 1");
	CHECK(*doc2.sections[0].find("textScale") == "1.5");
}

TEST_CASE("oui: a [Style NAME] section and a widget's style key round-trip",
	"[unit][oui]")
{
	// a named style is an ordinary `[Type id]` section carrying widget keys, so
	// the document model preserves it with no grammar change; a widget points at
	// it with `style = NAME`. The PRECEDENCE (style first, own keys override)
	// is the pure GuiStyleTests contract.
	const String text =
		"[Style hero]\n"
		"font = heading\n"
		"textColor = 1 0.82 0.35 1\n"
		"textScale = 1.4\n"
		"nineSlice = true\n"
		"\n"
		"[Button play]\n"
		"style = hero\n"
		"sprite = button\n"
		"textColor = 0.1 0.1 0.1 1\n";

	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));
	REQUIRE(doc.sections.size() == 2);
	CHECK(doc.sections[0].type == "Style");
	CHECK(doc.sections[0].id == "hero");
	CHECK(*doc.sections[0].find("nineSlice") == "true");
	CHECK(*doc.sections[1].find("style") == "hero");

	const String canonical = doc.serialize();
	GuiLayoutDoc doc2;
	REQUIRE(GuiLayoutDoc::parse(canonical, doc2, error));
	CHECK(doc2.serialize() == canonical);
	CHECK(doc2.sections[0].type == "Style");
	CHECK(doc2.sections[0].id == "hero");
	CHECK(*doc2.sections[1].find("style") == "hero");
}

TEST_CASE("oui: MALFORMED style values survive the document model verbatim",
	"[unit][oui]")
{
	// the document model is a text carrier: it does NOT validate values, so an
	// unknown font name, an unknown style name and a bad colour arity all parse
	// and round-trip unchanged. The runtime is what warns and falls back
	// (GuiFactory) - and the verdicts themselves are GuiStyleTests. Asserting
	// this HERE pins the division of labour: a typo never breaks the file.
	const String text =
		"[Label bad]\n"
		"style = nosuchstyle\n"
		"font = nosuchfont\n"
		"textColor = 1 0\n"
		"textScale = -3\n";

	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));
	CHECK(error.empty());
	REQUIRE(doc.sections.size() == 1);
	CHECK(*doc.sections[0].find("style") == "nosuchstyle");
	CHECK(*doc.sections[0].find("font") == "nosuchfont");
	CHECK(*doc.sections[0].find("textColor") == "1 0");
	CHECK(*doc.sections[0].find("textScale") == "-3");

	const String canonical = doc.serialize();
	GuiLayoutDoc doc2;
	REQUIRE(GuiLayoutDoc::parse(canonical, doc2, error));
	CHECK(doc2.serialize() == canonical);
}

TEST_CASE("oui: a key before any section fails honestly", "[unit][oui]")
{
	const String text = "atlas = gui_default\n[Label a]\ntext = x\n";
	GuiLayoutDoc doc;
	String error;
	CHECK_FALSE(GuiLayoutDoc::parse(text, doc, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("oui: an unterminated header fails honestly", "[unit][oui]")
{
	const String text = "[Label a\ntext = x\n";
	GuiLayoutDoc doc;
	String error;
	CHECK_FALSE(GuiLayoutDoc::parse(text, doc, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("oui: section.set overwrites or appends", "[unit][oui]")
{
	GuiLayoutSection section;
	section.type = "Label";
	section.id = "a";
	section.set("text", "one");
	section.set("font", "24");
	section.set("text", "two");	// overwrite the first
	REQUIRE(section.find("text") != nullptr);
	CHECK(*section.find("text") == "two");
	CHECK(section.entries.size() == 2);
}
