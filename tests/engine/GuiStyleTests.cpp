/**************************************************************
	created:	2026/07/29 at 11:00
	filename: 	GuiStyleTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the PURE .oui styling vocabulary
	(engine_gui/GuiStyle): the named-style merge and its precedence contract
	(style first, the widget's own keys override), the honest verdicts on
	malformed values (bad colour arity, a non-positive / non-numeric scale)
	and the font-reference index-vs-name discrimination. No renderer, no
	atlas, no window - plain text and floats, so this also covers the
	ORKIGE_NOSCRIPT path.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine_gui/GuiStyle.h"

using namespace Orkige;

namespace
{
	//! parse .oui text into a doc, failing the test on a malformed input
	GuiLayoutDoc parseDoc(String const & text)
	{
		GuiLayoutDoc doc;
		String error;
		REQUIRE(GuiLayoutDoc::parse(text, doc, error));
		REQUIRE(error.empty());
		return doc;
	}
	//! the widget section carrying @p id (the test's subject)
	GuiLayoutSection const & widget(GuiLayoutDoc const & doc, String const & id)
	{
		for(GuiLayoutSection const & s : doc.sections)
		{
			if(s.id == id)
			{
				return s;
			}
		}
		FAIL("no section with id " + id);
		return doc.sections.front();	// unreachable
	}
	String valueOf(GuiLayoutSection const & s, String const & key)
	{
		String const * v = s.find(key);
		return v != NULL ? *v : String("<absent>");
	}
}

//=== the named-style merge + precedence ==============================

TEST_CASE("oui style: a style SEEDS the widget and its own keys override",
	"[unit][oui][style]")
{
	const GuiLayoutDoc doc = parseDoc(
		"[Style hero]\n"
		"font = heading\n"
		"textColor = 1 0.8 0.2 1\n"
		"textScale = 1.5\n"
		"sprite = button\n"
		"\n"
		"[Button play]\n"
		"style = hero\n"
		"textColor = 0 1 0 1\n"		// the LOCAL override
		"text = Play\n");

	String unknown = "sentinel";
	const GuiLayoutSection resolved =
		GuiStyle::resolveSection(doc, widget(doc, "play"), &unknown);

	CHECK(unknown.empty());				// the style exists
	CHECK(resolved.type == "Button");
	CHECK(resolved.id == "play");
	// seeded from the style
	CHECK(valueOf(resolved, "font") == "heading");
	CHECK(valueOf(resolved, "textScale") == "1.5");
	CHECK(valueOf(resolved, "sprite") == "button");
	// the widget's OWN key wins over the style's
	CHECK(valueOf(resolved, "textColor") == "0 1 0 1");
	// the widget's own extra key survives
	CHECK(valueOf(resolved, "text") == "Play");
	// the reference itself is not a widget property
	CHECK(resolved.find(GuiStyle::styleKey()) == nullptr);
}

TEST_CASE("oui style: a widget with no style resolves to itself, unchanged",
	"[unit][oui][style]")
{
	const GuiLayoutDoc doc = parseDoc(
		"[Style hero]\n"
		"font = heading\n"
		"\n"
		"[Label plain]\n"
		"text = Hello\n"
		"font = body\n");

	String unknown = "sentinel";
	GuiLayoutSection const & source = widget(doc, "plain");
	const GuiLayoutSection resolved =
		GuiStyle::resolveSection(doc, source, &unknown);

	CHECK(unknown.empty());
	// byte-identical: an unstyled screen must behave exactly as before
	REQUIRE(resolved.entries.size() == source.entries.size());
	for(size_t each = 0; each < resolved.entries.size(); ++each)
	{
		CHECK(resolved.entries[each].key == source.entries[each].key);
		CHECK(resolved.entries[each].value == source.entries[each].value);
	}
}

TEST_CASE("oui style: an UNKNOWN style name is reported and ignored",
	"[unit][oui][style]")
{
	const GuiLayoutDoc doc = parseDoc(
		"[Label lost]\n"
		"style = nosuchstyle\n"
		"text = Hello\n");

	String unknown;
	const GuiLayoutSection resolved =
		GuiStyle::resolveSection(doc, widget(doc, "lost"), &unknown);

	CHECK(unknown == "nosuchstyle");			// the caller warns once
	CHECK(valueOf(resolved, "text") == "Hello");	// the widget still builds
	CHECK(resolved.find(GuiStyle::styleKey()) == nullptr);
}

TEST_CASE("oui style: styles do NOT nest - a style's own `style` key is ignored",
	"[unit][oui][style]")
{
	const GuiLayoutDoc doc = parseDoc(
		"[Style base]\n"
		"textScale = 3\n"
		"\n"
		"[Style hero]\n"
		"style = base\n"
		"font = heading\n"
		"\n"
		"[Label title]\n"
		"style = hero\n");

	const GuiLayoutSection resolved =
		GuiStyle::resolveSection(doc, widget(doc, "title"));

	CHECK(valueOf(resolved, "font") == "heading");
	// `base` is NOT pulled in (v1 has no nesting) and the key does not leak
	CHECK(valueOf(resolved, "textScale") == "<absent>");
	CHECK(resolved.find(GuiStyle::styleKey()) == nullptr);
}

TEST_CASE("oui style: the STYLE's key order seeds, the widget's order overrides",
	"[unit][oui][style]")
{
	// declaration order is the precedence rule, so the merged entry list must
	// keep the style's keys in the style's order with the widget's values in them
	const GuiLayoutDoc doc = parseDoc(
		"[Style hero]\n"
		"font = heading\n"
		"textScale = 2\n"
		"\n"
		"[Label title]\n"
		"textScale = 4\n"
		"text = Title\n");
	GuiLayoutSection styled = widget(doc, "title");
	styled.set(GuiStyle::styleKey(), "hero");

	const GuiLayoutSection resolved = GuiStyle::resolveSection(doc, styled);
	REQUIRE(resolved.entries.size() == 3);
	CHECK(resolved.entries[0].key == "font");		// style-seeded, style order
	CHECK(resolved.entries[1].key == "textScale");
	CHECK(resolved.entries[1].value == "4");		// the widget's value
	CHECK(resolved.entries[2].key == "text");		// widget-only key, appended
}

TEST_CASE("oui style: resolveDocument styles every widget and leaves the rest",
	"[unit][oui][style]")
{
	const GuiLayoutDoc doc = parseDoc(
		"[Layout]\n"
		"atlas = gui_default\n"
		"\n"
		"[Style hero]\n"
		"font = heading\n"
		"\n"
		"[Label a]\n"
		"style = hero\n"
		"\n"
		"[Label b]\n"
		"style = missing\n"
		"\n"
		"[ToggleGroup g]\n"
		"members = a b\n");

	std::vector<String> unknown;
	const GuiLayoutDoc resolved = GuiStyle::resolveDocument(doc, &unknown);

	REQUIRE(resolved.sections.size() == doc.sections.size());
	// the [Layout], the [Style] itself and the [ToggleGroup] pass through
	CHECK(resolved.sections[0].type == "Layout");
	CHECK(*resolved.sections[0].find("atlas") == "gui_default");
	CHECK(resolved.sections[1].type == "Style");
	CHECK(*resolved.sections[1].find("font") == "heading");
	CHECK(resolved.sections[4].type == "ToggleGroup");
	CHECK(*resolved.sections[4].find("members") == "a b");
	// widget a picked the style up, widget b is reported once
	CHECK(valueOf(resolved.sections[2], "font") == "heading");
	REQUIRE(unknown.size() == 1);
	CHECK(unknown[0] == "b:missing");
}

TEST_CASE("oui style: styleNames / findStyle read the document's declarations",
	"[unit][oui][style]")
{
	const GuiLayoutDoc doc = parseDoc(
		"[Style hero]\nfont = heading\n\n"
		"[Style quiet]\ntextColor = 0.6 0.6 0.6 1\n\n"
		"[Label a]\n");

	const std::vector<String> names = GuiStyle::styleNames(doc);
	REQUIRE(names.size() == 2);
	CHECK(names[0] == "hero");		// declaration order
	CHECK(names[1] == "quiet");
	REQUIRE(GuiStyle::findStyle(doc, "quiet") != nullptr);
	CHECK(*GuiStyle::findStyle(doc, "quiet")->find("textColor") ==
		"0.6 0.6 0.6 1");
	CHECK(GuiStyle::findStyle(doc, "nope") == nullptr);
	CHECK(GuiStyle::findStyle(doc, "") == nullptr);
}

//=== the text-style value parsers (warn-and-default contract) =========

TEST_CASE("oui style: textColor accepts 3 or 4 components", "[unit][oui][style]")
{
	float rgba[4] = { 0, 0, 0, 0 };
	String error = "sentinel";

	REQUIRE(GuiStyle::parseTextColour("0.25 0.5 0.75 0.5", rgba, error));
	CHECK(error.empty());
	CHECK(rgba[0] == 0.25f);
	CHECK(rgba[1] == 0.5f);
	CHECK(rgba[2] == 0.75f);
	CHECK(rgba[3] == 0.5f);

	// three components: the alpha defaults to opaque
	REQUIRE(GuiStyle::parseTextColour("1 0 0", rgba, error));
	CHECK(rgba[3] == 1.0f);
	// tabs / extra spacing are the grammar's business, not the value's
	REQUIRE(GuiStyle::parseTextColour("  0 1   0  ", rgba, error));
	CHECK(rgba[1] == 1.0f);
}

TEST_CASE("oui style: a malformed textColor is REFUSED with a message and "
	"leaves the value untouched", "[unit][oui][style]")
{
	float rgba[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
	String error;

	// too few components
	CHECK_FALSE(GuiStyle::parseTextColour("1 0", rgba, error));
	CHECK_FALSE(error.empty());
	// too many
	error.clear();
	CHECK_FALSE(GuiStyle::parseTextColour("1 0 0 1 0", rgba, error));
	CHECK_FALSE(error.empty());
	// non-numeric
	error.clear();
	CHECK_FALSE(GuiStyle::parseTextColour("red", rgba, error));
	CHECK_FALSE(error.empty());
	// empty
	error.clear();
	CHECK_FALSE(GuiStyle::parseTextColour("", rgba, error));
	CHECK_FALSE(error.empty());

	// every refusal left the caller's colour ALONE (it keeps its current look)
	CHECK(rgba[0] == 0.1f);
	CHECK(rgba[1] == 0.2f);
	CHECK(rgba[2] == 0.3f);
	CHECK(rgba[3] == 0.4f);
}

TEST_CASE("oui style: textScale takes a positive factor and refuses the rest",
	"[unit][oui][style]")
{
	float scale = -1.0f;
	String error = "sentinel";

	REQUIRE(GuiStyle::parseTextScale("2.5", scale, error));
	CHECK(error.empty());
	CHECK(scale == 2.5f);

	// a refusal leaves the caller's factor untouched
	scale = 1.0f;
	CHECK_FALSE(GuiStyle::parseTextScale("0", scale, error));
	CHECK_FALSE(error.empty());
	CHECK(scale == 1.0f);
	CHECK_FALSE(GuiStyle::parseTextScale("-2", scale, error));
	CHECK(scale == 1.0f);
	CHECK_FALSE(GuiStyle::parseTextScale("big", scale, error));
	CHECK(scale == 1.0f);
	CHECK_FALSE(GuiStyle::parseTextScale("1 2", scale, error));
	CHECK(scale == 1.0f);
	CHECK_FALSE(GuiStyle::parseTextScale("", scale, error));
	CHECK(scale == 1.0f);
}

TEST_CASE("oui style: a font ref is an INDEX only when it is all digits",
	"[unit][oui][style]")
{
	uint index = 999;
	CHECK(GuiStyle::isFontIndexLiteral("24", index));
	CHECK(index == 24u);
	CHECK(GuiStyle::isFontIndexLiteral("0", index));
	CHECK(index == 0u);

	// anything else is a NAME the atlas resolves (never a silent index)
	CHECK_FALSE(GuiStyle::isFontIndexLiteral("heading", index));
	CHECK_FALSE(GuiStyle::isFontIndexLiteral("24heading", index));
	CHECK_FALSE(GuiStyle::isFontIndexLiteral("font24", index));
	CHECK_FALSE(GuiStyle::isFontIndexLiteral("-4", index));
	CHECK_FALSE(GuiStyle::isFontIndexLiteral("2.4", index));
	CHECK_FALSE(GuiStyle::isFontIndexLiteral(" 24", index));
	CHECK_FALSE(GuiStyle::isFontIndexLiteral("", index));
}
