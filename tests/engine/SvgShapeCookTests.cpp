/**************************************************************
	created:	2026/07/29 at 21:00
	filename: 	SvgShapeCookTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the `.svg` -> `.oshape` import cook: the element kinds it
	maps, transforms and group-inherited paint, multiple fill regions, hole
	detection, gradients, morph sets (including the structure refusal) and the
	verdicts an unusable drawing gets.

	Three cases are GOLDEN text: the exact bytes a straight-edge drawing, an
	adaptively flattened bezier and a fixed-flatten morph set must cook to. They
	pin the whole chain - flatten decisions, the bbox/extent/flip placement and
	the printed precision - so a change in any of it is a diff, not a silent
	drift in every shape a project imports.
***************************************************************/

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core_util/VectorShapeAsset.h"
#include "core_util/VectorShapeCook.h"
#include "engine_gui/SvgShapeCook.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <vector>

using namespace Orkige;
using Catch::Approx;

namespace
{
	typedef VectorTessellator::Region Region;

	bool cookText(char const * svg, String & out, String * error = nullptr,
		VectorShapeCook::Options const & options = VectorShapeCook::Options())
	{
		String reason;
		const bool ok = SvgShapeCook::cook(
			reinterpret_cast<unsigned char const *>(svg), int(std::strlen(svg)),
			options, out, error ? error : &reason);
		return ok;
	}

	//! cook and re-parse through the runtime parser - what a project loads
	bool cookRegions(char const * svg, std::vector<Region> & out,
		String * error = nullptr)
	{
		String text;
		if(!cookText(svg, text, error))
		{
			return false;
		}
		return VectorShapeAsset::parse(text, out);
	}

	//! the cooked text without its banner comments (the banner names the tool,
	//! the body is the asset)
	String body(String const & text)
	{
		std::ostringstream out;
		std::istringstream lines(text);
		String line;
		while(std::getline(lines, line))
		{
			if(!line.empty() && line[0] != '#')
			{
				out << line << "\n";
			}
		}
		return out.str();
	}

	SvgShapeCook::Source pose(char const * svg, char const * name)
	{
		SvgShapeCook::Source source;
		source.data = reinterpret_cast<unsigned char const *>(svg);
		source.size = int(std::strlen(svg));
		source.name = name;
		return source;
	}
}

TEST_CASE("svgcook_golden_straight_edges", "[unit][vectorshape]")
{
	// a rect: four vertices, centered, its 60-unit side spanning 2 world units
	const String expected =
		"version 1\n"
		"fill 0.2667 0.5333 0.8000 1.0000\n"
		"contour 4\n"
		"v -1.00000 1.00000\n"
		"v 1.00000 1.00000\n"
		"v 1.00000 -1.00000\n"
		"v -1.00000 -1.00000\n";
	String text;
	REQUIRE(cookText("<svg xmlns=\"http://www.w3.org/2000/svg\" "
		"viewBox=\"0 0 100 100\"><rect x=\"20\" y=\"20\" width=\"60\" "
		"height=\"60\" fill=\"#4488cc\"/></svg>", text));
	CHECK(body(text) == expected);

	SECTION("a polygon, an explicit path and relative commands agree")
	{
		// the same square written three ways cooks to the same bytes
		String asPolygon;
		String asPath;
		String asRelative;
		REQUIRE(cookText("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><polygon points=\"20,20 80,20 80,80 "
			"20,80\" fill=\"#4488cc\"/></svg>", asPolygon));
		REQUIRE(cookText("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><path d=\"M 20 20 L 80 20 L 80 80 L 20 80 "
			"Z\" fill=\"#4488cc\"/></svg>", asPath));
		REQUIRE(cookText("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><path d=\"m 20 20 h 60 v 60 h -60 z\" "
			"fill=\"#4488cc\"/></svg>", asRelative));
		CHECK(body(asPolygon) == expected);
		CHECK(body(asPath) == expected);
		CHECK(body(asRelative) == expected);
	}
}

TEST_CASE("svgcook_golden_adaptive_curves", "[unit][vectorshape]")
{
	// two cubic arcs closed into a lens: the adaptive flatten's exact output at
	// the default tolerance (1% of the drawing's larger side)
	const String expected =
		"version 1\n"
		"fill 0.8980 0.4196 0.3804 1.0000\n"
		"contour 24\n"
		"v -1.00000 -0.00000\n"
		"v -0.97754 0.17578\n"
		"v -0.91406 0.32812\n"
		"v -0.81543 0.45703\n"
		"v -0.68750 0.56250\n"
		"v -0.36719 0.70312\n"
		"v 0.00000 0.75000\n"
		"v 0.36719 0.70312\n"
		"v 0.68750 0.56250\n"
		"v 0.81543 0.45703\n"
		"v 0.91406 0.32812\n"
		"v 0.97754 0.17578\n"
		"v 1.00000 -0.00000\n"
		"v 0.97754 -0.17578\n"
		"v 0.91406 -0.32812\n"
		"v 0.81543 -0.45703\n"
		"v 0.68750 -0.56250\n"
		"v 0.36719 -0.70312\n"
		"v 0.00000 -0.75000\n"
		"v -0.36719 -0.70312\n"
		"v -0.68750 -0.56250\n"
		"v -0.81543 -0.45703\n"
		"v -0.91406 -0.32812\n"
		"v -0.97754 -0.17578\n";
	String text;
	REQUIRE(cookText("<svg xmlns=\"http://www.w3.org/2000/svg\" "
		"viewBox=\"0 0 100 100\"><path d=\"M 10 50 C 10 10 90 10 90 50 C 90 90 "
		"10 90 10 50 Z\" fill=\"#e56b61\"/></svg>", text));
	CHECK(body(text) == expected);

	SECTION("a tighter tolerance buys more vertices, a looser one fewer")
	{
		VectorShapeCook::Options fine;
		fine.tolerance = 0.05;
		VectorShapeCook::Options coarse;
		coarse.tolerance = 8.0;
		std::vector<Region> fineRegions;
		std::vector<Region> coarseRegions;
		String fineText;
		String coarseText;
		REQUIRE(cookText("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><path d=\"M 10 50 C 10 10 90 10 90 50 "
			"C 90 90 10 90 10 50 Z\" fill=\"#e56b61\"/></svg>", fineText,
			nullptr, fine));
		REQUIRE(cookText("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><path d=\"M 10 50 C 10 10 90 10 90 50 "
			"C 90 90 10 90 10 50 Z\" fill=\"#e56b61\"/></svg>", coarseText,
			nullptr, coarse));
		REQUIRE(VectorShapeAsset::parse(fineText, fineRegions));
		REQUIRE(VectorShapeAsset::parse(coarseText, coarseRegions));
		CHECK(fineRegions[0].outer.size() > 24u);
		CHECK(coarseRegions[0].outer.size() < 24u);
	}

	SECTION("a quadratic flattens too, and a straight closing edge stays one "
		"vertex")
	{
		std::vector<Region> regions;
		REQUIRE(cookRegions("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><path d=\"M 10 80 Q 50 10 90 80 Z\" "
			"fill=\"#334455\"/></svg>", regions));
		REQUIRE(regions.size() == 1);
		// the curve subdivides; the Z back to the start adds no extra vertex
		CHECK(regions[0].outer.size() == 9);
	}
}

TEST_CASE("svgcook_golden_morph_set", "[unit][vectorshape]")
{
	// both poses flatten at the FIXED count, so their vertices correspond; the
	// TARGET is placed with the BASE's transform, so its wider silhouette
	// really is wider in world units
	const String expected =
		"version 1\n"
		"fill 0.6667 0.2667 0.5333 1.0000\n"
		"contour 20\n"
		"v -1.00000 -0.00000\n"
		"v -0.94400 0.27000\n"
		"v -0.79200 0.48000\n"
		"v -0.56800 0.63000\n"
		"v -0.29600 0.72000\n"
		"v 0.00000 0.75000\n"
		"v 0.29600 0.72000\n"
		"v 0.56800 0.63000\n"
		"v 0.79200 0.48000\n"
		"v 0.94400 0.27000\n"
		"v 1.00000 -0.00000\n"
		"v 0.94400 -0.27000\n"
		"v 0.79200 -0.48000\n"
		"v 0.56800 -0.63000\n"
		"v 0.29600 -0.72000\n"
		"v 0.00000 -0.75000\n"
		"v -0.29600 -0.72000\n"
		"v -0.56800 -0.63000\n"
		"v -0.79200 -0.48000\n"
		"v -0.94400 -0.27000\n"
		"morph squash\n"
		"fill 0.6667 0.2667 0.5333 1.0000\n"
		"contour 20\n"
		"v -1.12500 -0.00000\n"
		"v -1.06200 0.16875\n"
		"v -0.89100 0.30000\n"
		"v -0.63900 0.39375\n"
		"v -0.33300 0.45000\n"
		"v 0.00000 0.46875\n"
		"v 0.33300 0.45000\n"
		"v 0.63900 0.39375\n"
		"v 0.89100 0.30000\n"
		"v 1.06200 0.16875\n"
		"v 1.12500 -0.00000\n"
		"v 1.06200 -0.16875\n"
		"v 0.89100 -0.30000\n"
		"v 0.63900 -0.39375\n"
		"v 0.33300 -0.45000\n"
		"v 0.00000 -0.46875\n"
		"v -0.33300 -0.45000\n"
		"v -0.63900 -0.39375\n"
		"v -0.89100 -0.30000\n"
		"v -1.06200 -0.16875\n";
	char const * const base =
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
		"<path d=\"M 10 50 C 10 10 90 10 90 50 C 90 90 10 90 10 50 Z\" "
		"fill=\"#aa4488\"/></svg>";
	char const * const wider =
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
		"<path d=\"M 5 50 C 5 25 95 25 95 50 C 95 75 5 75 5 50 Z\" "
		"fill=\"#aa4488\"/></svg>";
	std::vector<SvgShapeCook::Source> poses;
	poses.push_back(pose(base, ""));
	poses.push_back(pose(wider, "squash"));
	String text;
	String error;
	const VectorShapeCook::Options options;
	REQUIRE(SvgShapeCook::cookMorphSet(poses, options, text, &error));
	CHECK(body(text) == expected);

	SECTION("a straight-edged pose set keeps its edge count")
	{
		char const * const square =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
			"<polygon points=\"20,20 80,20 80,80 20,80\" fill=\"#88cc44\"/>"
			"</svg>";
		char const * const flat =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
			"<polygon points=\"10,35 90,35 90,65 10,65\" fill=\"#88cc44\"/>"
			"</svg>";
		std::vector<SvgShapeCook::Source> flatPoses;
		flatPoses.push_back(pose(square, ""));
		flatPoses.push_back(pose(flat, "squash"));
		String flatText;
		REQUIRE(SvgShapeCook::cookMorphSet(flatPoses, options, flatText,
			&error));
		VectorShapeAsset::ParsedShape parsed;
		REQUIRE(VectorShapeAsset::parse(flatText, parsed));
		REQUIRE(parsed.base.size() == 1);
		REQUIRE(parsed.morphs.size() == 1);
		// a fixed-count flatten must NOT subdivide straight edges
		CHECK(parsed.base[0].outer.size() == 4);
		CHECK(parsed.morphs[0].regions[0].outer.size() == 4);
	}

	SECTION("a pose whose structure differs is refused with both structures")
	{
		char const * const square =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
			"<polygon points=\"20,20 80,20 80,80 20,80\" fill=\"#88cc44\"/>"
			"</svg>";
		char const * const triangle =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
			"<polygon points=\"50,20 80,80 20,80\" fill=\"#88cc44\"/></svg>";
		std::vector<SvgShapeCook::Source> bad;
		bad.push_back(pose(square, ""));
		bad.push_back(pose(triangle, "bad"));
		String out = "untouched";
		error.clear();
		CHECK_FALSE(SvgShapeCook::cookMorphSet(bad, options, out, &error));
		CHECK(out == "untouched");
		CHECK(error.find("morph target 'bad'") != String::npos);
		CHECK(error.find("[3]") != String::npos);
		CHECK(error.find("[4]") != String::npos);
	}

	SECTION("an unusable pose names itself")
	{
		char const * const square =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
			"<polygon points=\"20,20 80,20 80,80 20,80\" fill=\"#88cc44\"/>"
			"</svg>";
		std::vector<SvgShapeCook::Source> bad;
		bad.push_back(pose(square, ""));
		bad.push_back(pose("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>",
			"blank"));
		String out;
		error.clear();
		CHECK_FALSE(SvgShapeCook::cookMorphSet(bad, options, out, &error));
		CHECK(error.find("morph target 'blank'") != String::npos);
		CHECK(error.find("no fillable shapes") != String::npos);
	}

	SECTION("a base with nothing to cook says so")
	{
		std::vector<SvgShapeCook::Source> bad;
		bad.push_back(pose("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>",
			""));
		String out;
		error.clear();
		CHECK_FALSE(SvgShapeCook::cookMorphSet(bad, options, out, &error));
		CHECK(error.find("the base SVG has no fillable contours") !=
			String::npos);
	}
}

TEST_CASE("svgcook_transforms_and_inherited_paint", "[unit][vectorshape]")
{
	// a GROUP carries the fill and a transform: both must reach the child, or
	// the drawing cooks black and in the wrong place
	std::vector<Region> regions;
	REQUIRE(cookRegions("<svg xmlns=\"http://www.w3.org/2000/svg\" "
		"viewBox=\"0 0 100 100\">"
		"<g fill=\"#3366aa\" transform=\"translate(10,10) scale(2)\">"
		"<rect x=\"0\" y=\"0\" width=\"20\" height=\"20\"/></g></svg>",
		regions));
	REQUIRE(regions.size() == 1);
	CHECK(regions[0].fill.r == Approx(0.2f).margin(0.005));
	CHECK(regions[0].fill.g == Approx(0.4f).margin(0.005));
	CHECK(regions[0].fill.b == Approx(0.6667f).margin(0.005));

	SECTION("a transform positions siblings relative to each other")
	{
		// two identical rects, one translated right: the cook must place them
		// apart (a cook that dropped transforms would stack them)
		std::vector<Region> pair;
		REQUIRE(cookRegions("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\">"
			"<rect x=\"10\" y=\"40\" width=\"20\" height=\"20\" "
			"fill=\"#ffffff\"/>"
			"<g transform=\"translate(60,0)\">"
			"<rect x=\"10\" y=\"40\" width=\"20\" height=\"20\" "
			"fill=\"#000000\"/></g></svg>", pair));
		REQUIRE(pair.size() == 2);
		CHECK(pair[0].outer[0].x < 0.0f);
		CHECK(pair[1].outer[0].x > 0.0f);
		// paint order follows the document: white first, then black
		CHECK(pair[0].fill.r == Approx(1.0f).margin(0.005));
		CHECK(pair[1].fill.r == Approx(0.0f).margin(0.005));
	}

	SECTION("a style attribute resolves like a presentation attribute")
	{
		std::vector<Region> styled;
		REQUIRE(cookRegions("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><rect x=\"20\" y=\"20\" width=\"60\" "
			"height=\"60\" style=\"fill:#4488cc\"/></svg>", styled));
		REQUIRE(styled.size() == 1);
		CHECK(styled[0].fill.b == Approx(0.8f).margin(0.005));
	}

	SECTION("fill-opacity reaches the region's alpha")
	{
		std::vector<Region> faded;
		REQUIRE(cookRegions("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><rect x=\"20\" y=\"20\" width=\"60\" "
			"height=\"60\" fill=\"#4488cc\" fill-opacity=\"0.5\"/></svg>",
			faded));
		REQUIRE(faded.size() == 1);
		CHECK(faded[0].fill.a == Approx(0.5f).margin(0.01));
	}
}

TEST_CASE("svgcook_holes_and_region_order", "[unit][vectorshape]")
{
	// a donut: the CONTAINED subpath is the outer region's hole, not a second
	// opaque region painted over the gap
	std::vector<Region> regions;
	REQUIRE(cookRegions("<svg xmlns=\"http://www.w3.org/2000/svg\" "
		"viewBox=\"0 0 100 100\"><path d=\"M 10 10 L 90 10 L 90 90 L 10 90 Z "
		"M 30 30 L 30 70 L 70 70 L 70 30 Z\" fill=\"#334455\"/></svg>",
		regions));
	REQUIRE(regions.size() == 1);
	REQUIRE(regions[0].holes.size() == 1);
	CHECK(regions[0].outer.size() == 4);
	CHECK(regions[0].holes[0].size() == 4);

	SECTION("two DISJOINT subpaths stay two regions")
	{
		std::vector<Region> disjoint;
		REQUIRE(cookRegions("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><path d=\"M 5 5 L 40 5 L 40 40 L 5 40 Z "
			"M 60 60 L 95 60 L 95 95 L 60 95 Z\" fill=\"#334455\"/></svg>",
			disjoint));
		REQUIRE(disjoint.size() == 2);
		CHECK(disjoint[0].holes.empty());
		CHECK(disjoint[1].holes.empty());
	}

	SECTION("a contained subpath of ANOTHER element is its own region")
	{
		// containment only groups subpaths WITHIN one element - a separate
		// element drawn on top is a deliberate overlay, not a cut-out
		std::vector<Region> overlay;
		REQUIRE(cookRegions("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\">"
			"<rect x=\"10\" y=\"10\" width=\"80\" height=\"80\" "
			"fill=\"#334455\"/>"
			"<rect x=\"40\" y=\"40\" width=\"20\" height=\"20\" "
			"fill=\"#ffffff\"/></svg>", overlay));
		REQUIRE(overlay.size() == 2);
		CHECK(overlay[0].holes.empty());
		CHECK(overlay[1].holes.empty());
	}

	SECTION("multiple fill regions keep their own colours in paint order")
	{
		std::vector<Region> multi;
		REQUIRE(cookRegions("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\">"
			"<rect x=\"10\" y=\"10\" width=\"80\" height=\"60\" "
			"fill=\"#e56b61\"/>"
			"<path d=\"M 20 20 L 60 20 L 40 50 Z\" fill=\"rgb(40,40,60)\"/>"
			"</svg>", multi));
		REQUIRE(multi.size() == 2);
		CHECK(multi[0].outer.size() == 4);
		CHECK(multi[1].outer.size() == 3);
		CHECK(multi[0].fill.r == Approx(0.898f).margin(0.005));
		CHECK(multi[1].fill.b == Approx(60.0f / 255.0f).margin(0.005));
	}
}

TEST_CASE("svgcook_unpainted_and_malformed_verdicts", "[unit][vectorshape]")
{
	String text = "untouched";
	String error;

	SECTION("an empty document has no fillable shape")
	{
		CHECK_FALSE(cookText("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"></svg>", text, &error));
		CHECK(error == "no fillable shapes in the SVG");
		CHECK(text == "untouched");
	}

	SECTION("an unfilled outline paints nothing")
	{
		CHECK_FALSE(cookText("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><rect x=\"20\" y=\"20\" width=\"60\" "
			"height=\"60\" fill=\"none\" stroke=\"#000000\" "
			"stroke-width=\"4\"/></svg>", text, &error));
		CHECK(error == "no fillable shapes in the SVG");
	}

	SECTION("a hidden element paints nothing")
	{
		CHECK_FALSE(cookText("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><rect x=\"20\" y=\"20\" width=\"60\" "
			"height=\"60\" fill=\"#4488cc\" display=\"none\"/></svg>", text,
			&error));
		CHECK(error == "no fillable shapes in the SVG");
	}

	SECTION("a filled element enclosing no area is refused, not emitted")
	{
		// a degenerate two-point path: filled, but nothing to triangulate
		CHECK_FALSE(cookText("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><path d=\"M 10 10 L 90 90\" "
			"fill=\"#4488cc\"/></svg>", text, &error));
		CHECK(error == "no closed contours with >= 3 vertices");
	}

	SECTION("garbage is a readable refusal, never a crash")
	{
		CHECK_FALSE(cookText("not an svg at all {{{", text, &error));
		CHECK_FALSE(error.empty());
		CHECK_FALSE(cookText("<svg><rect", text, &error));
		CHECK_FALSE(error.empty());
		CHECK(text == "untouched");
	}

	SECTION("no bytes at all is refused")
	{
		String out;
		CHECK_FALSE(SvgShapeCook::cook(nullptr, 0, VectorShapeCook::Options(),
			out, &error));
		CHECK(error == "the SVG source is empty");
		unsigned char const byte = 0;
		CHECK_FALSE(SvgShapeCook::cook(&byte, 0, VectorShapeCook::Options(),
			out, &error));
		CHECK(error == "the SVG source is empty");
	}

	SECTION("an empty morph set is refused")
	{
		std::vector<SvgShapeCook::Source> none;
		CHECK_FALSE(SvgShapeCook::cookMorphSet(none, VectorShapeCook::Options(),
			text, &error));
		CHECK(error == "a morph set needs a base SVG");
	}
}

TEST_CASE("svgcook_curved_primitives_and_gradients", "[unit][vectorshape]")
{
	// a circle is real bezier arcs, flattened at the SAME tolerance as any
	// other curve - the drawing's contours are consistently smooth
	std::vector<Region> regions;
	REQUIRE(cookRegions("<svg xmlns=\"http://www.w3.org/2000/svg\" "
		"viewBox=\"0 0 100 100\"><circle cx=\"50\" cy=\"50\" r=\"20\" "
		"fill=\"blue\"/></svg>", regions));
	REQUIRE(regions.size() == 1);
	CHECK(regions[0].outer.size() >= 8);
	CHECK(regions[0].fill.b == Approx(1.0f).margin(0.005));
	// every vertex sits on the circle, which now spans the requested extent
	for(VectorTessellator::Point const & point : regions[0].outer)
	{
		CHECK(std::sqrt(point.x * point.x + point.y * point.y) ==
			Approx(1.0f).margin(0.02));
	}

	SECTION("an ellipse keeps its aspect")
	{
		std::vector<Region> ellipse;
		REQUIRE(cookRegions("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><ellipse cx=\"50\" cy=\"50\" rx=\"40\" "
			"ry=\"10\" fill=\"#88cc44\"/></svg>", ellipse));
		REQUIRE(ellipse.size() == 1);
		float maxX = 0.0f;
		float maxY = 0.0f;
		for(VectorTessellator::Point const & point : ellipse[0].outer)
		{
			maxX = std::max(maxX, std::fabs(point.x));
			maxY = std::max(maxY, std::fabs(point.y));
		}
		CHECK(maxX == Approx(1.0f).margin(0.01));
		CHECK(maxY == Approx(0.25f).margin(0.02));
	}

	SECTION("a gradient fill flattens to its first stop")
	{
		std::vector<Region> graded;
		REQUIRE(cookRegions("<svg xmlns=\"http://www.w3.org/2000/svg\" "
			"viewBox=\"0 0 100 100\"><defs><linearGradient id=\"g\">"
			"<stop offset=\"0\" stop-color=\"#4488cc\"/>"
			"<stop offset=\"1\" stop-color=\"#ffffff\"/></linearGradient>"
			"</defs><rect x=\"20\" y=\"20\" width=\"60\" height=\"60\" "
			"fill=\"url(#g)\"/></svg>", graded));
		REQUIRE(graded.size() == 1);
		CHECK(graded[0].fill.r == Approx(0.2667f).margin(0.01));
		CHECK(graded[0].fill.b == Approx(0.8f).margin(0.01));
	}
}

TEST_CASE("svgcook_extract_reports_the_drawing_space", "[unit][vectorshape]")
{
	// the parse+flatten step alone hands back the drawing's OWN coordinates
	// (y-down, unplaced) - the shape of the seam a future geometry consumer uses
	char const * const svg =
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
		"<rect x=\"20\" y=\"30\" width=\"40\" height=\"10\" fill=\"#4488cc\"/>"
		"</svg>";
	std::vector<Region> regions;
	int filled = -1;
	String error;
	REQUIRE(SvgShapeCook::extractRegions(
		reinterpret_cast<unsigned char const *>(svg), int(std::strlen(svg)),
		VectorShapeCook::Options(), regions, &filled, &error));
	CHECK(filled == 1);
	REQUIRE(regions.size() == 1);
	REQUIRE(regions[0].outer.size() == 4);
	// y is still DOWN and the units are still the document's
	CHECK(regions[0].outer[0].x == Approx(20.0f).margin(1.0e-4));
	CHECK(regions[0].outer[0].y == Approx(30.0f).margin(1.0e-4));
	CHECK(regions[0].outer[2].y == Approx(40.0f).margin(1.0e-4));

	SECTION("the filled count separates 'nothing there' from 'nothing usable'")
	{
		char const * const degenerate =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
			"<path d=\"M 10 10 L 90 90\" fill=\"#4488cc\"/></svg>";
		regions.clear();
		filled = -1;
		CHECK_FALSE(SvgShapeCook::extractRegions(
			reinterpret_cast<unsigned char const *>(degenerate),
			int(std::strlen(degenerate)), VectorShapeCook::Options(), regions,
			&filled, &error));
		CHECK(filled == 1);
		CHECK(regions.empty());
		CHECK(error == "no closed contours with >= 3 vertices");
	}
}
