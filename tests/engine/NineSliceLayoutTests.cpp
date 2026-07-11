/**************************************************************
	created:	2026/07/11 at 10:00
	filename: 	NineSliceLayoutTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the pure nine-slice / tiled quad emitters
	(engine_gui/UiRenderer.h UiNineSlice): the 9-quad decomposition,
	fixed corner bands, edge/centre UV sub-rects, proportional shrink on a
	too-small target, and the tiled row/column counts with clamped edge UVs.
	No render system - the emitters are pure geometry.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine_gui/UiRenderer.h"

using namespace Orkige;
using Catch::Approx;

TEST_CASE("nine-slice: a comfortable target emits 9 quads with fixed corners",
	"[unit][nineslice]")
{
	std::vector<UiNineSlice::Quad> quads;
	// 100x80 target, 10px device corners, sprite insets = 0.2 of the span
	UiNineSlice::buildNineSlice(0, 0, 100, 80,
		10, 10, 10, 10,			// corner bands (device px)
		0.0f, 0.0f, 1.0f, 1.0f,	// full-texture UV rect
		0.2f, 0.2f, 0.2f, 0.2f,	// UV split fractions
		quads);

	REQUIRE(quads.size() == 9u);

	// row-major order: 0 TL, 1 top, 2 TR, 3 left, 4 centre, 5 right, 6 BL,
	// 7 bottom, 8 BR
	const UiNineSlice::Quad & tl = quads[0];
	CHECK(tl.x0 == Approx(0.0f));
	CHECK(tl.y0 == Approx(0.0f));
	CHECK(tl.x1 == Approx(10.0f));	// corner stays its device size
	CHECK(tl.y1 == Approx(10.0f));
	CHECK(tl.u0 == Approx(0.0f));
	CHECK(tl.u1 == Approx(0.2f));	// corner UV = the inset fraction

	const UiNineSlice::Quad & centre = quads[4];
	CHECK(centre.x0 == Approx(10.0f));
	CHECK(centre.x1 == Approx(90.0f));	// centre stretches (100 - 2*10)
	CHECK(centre.y0 == Approx(10.0f));
	CHECK(centre.y1 == Approx(70.0f));
	CHECK(centre.u0 == Approx(0.2f));
	CHECK(centre.u1 == Approx(0.8f));

	const UiNineSlice::Quad & br = quads[8];
	CHECK(br.x0 == Approx(90.0f));
	CHECK(br.x1 == Approx(100.0f));
	CHECK(br.u0 == Approx(0.8f));
	CHECK(br.u1 == Approx(1.0f));
	CHECK(br.v1 == Approx(1.0f));
}

TEST_CASE("nine-slice: a UV sub-rect maps corners onto its inset lines",
	"[unit][nineslice]")
{
	std::vector<UiNineSlice::Quad> quads;
	// a sprite occupying UV x[0.25,0.75], y[0.10,0.30]; 25% corner fractions
	UiNineSlice::buildNineSlice(0, 0, 200, 100,
		8, 8, 8, 8,
		0.25f, 0.10f, 0.75f, 0.30f,
		0.25f, 0.25f, 0.25f, 0.25f,
		quads);
	REQUIRE(quads.size() == 9u);
	// the left corner column ends 25% into the 0.5-wide UV span => 0.25 + 0.125
	CHECK(quads[0].u1 == Approx(0.375f));
	// the top corner row ends 25% into the 0.2-tall UV span => 0.10 + 0.05
	CHECK(quads[0].v1 == Approx(0.15f));
	// the centre samples the inner half of the sub-rect
	CHECK(quads[4].u0 == Approx(0.375f));
	CHECK(quads[4].u1 == Approx(0.625f));
}

TEST_CASE("nine-slice: corners shrink proportionally on a too-small target",
	"[unit][nineslice]")
{
	std::vector<UiNineSlice::Quad> quads;
	// width 12 < 2*10 corners -> horizontal bands shrink to 6 each and the
	// middle column collapses (its 3 quads are dropped); height is comfortable
	UiNineSlice::buildNineSlice(0, 0, 12, 80,
		10, 10, 10, 10,
		0.0f, 0.0f, 1.0f, 1.0f,
		0.2f, 0.2f, 0.2f, 0.2f,
		quads);

	CHECK(quads.size() == 6u);	// the three middle-column cells collapsed
	for(UiNineSlice::Quad const & q : quads)
	{
		// no cell overruns the target and every cell has positive area
		CHECK(q.x0 >= Approx(0.0f));
		CHECK(q.x1 <= Approx(12.0f));
		CHECK(q.x1 > q.x0);
		CHECK(q.y1 > q.y0);
	}
	// the left corner shrank to width/2 = 6 (no overlap with the right corner)
	CHECK(quads[0].x1 == Approx(6.0f));
}

TEST_CASE("tiled: full and partial tile counts with clamped edge UVs",
	"[unit][nineslice]")
{
	std::vector<UiNineSlice::Quad> full;
	UiNineSlice::buildTiled(0, 0, 30, 10, 10, 10,
		0.0f, 0.0f, 1.0f, 1.0f, full);
	CHECK(full.size() == 3u);	// 3 columns x 1 row, all whole
	for(UiNineSlice::Quad const & q : full)
	{
		CHECK(q.u1 == Approx(1.0f));	// whole tiles sample the full sprite
	}

	std::vector<UiNineSlice::Quad> partial;
	UiNineSlice::buildTiled(0, 0, 25, 10, 10, 10,
		0.0f, 0.0f, 1.0f, 1.0f, partial);
	CHECK(partial.size() == 3u);	// 10 + 10 + 5
	const UiNineSlice::Quad & last = partial[2];
	CHECK(last.x0 == Approx(20.0f));
	CHECK(last.x1 == Approx(25.0f));	// the partial column
	CHECK(last.u1 == Approx(0.5f));		// samples only half the sprite width
}
