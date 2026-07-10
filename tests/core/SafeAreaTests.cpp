/**************************************************************
	created:	2026/07/10 at 12:00
	filename: 	SafeAreaTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless safe-area unit tests: SafeAreaInsets::fromSafeRect (the pure
	window - safe = inset arithmetic the platform query funnels through) and
	UiAnchor::place (the anchor-in-safe-box math the fastgui layer and a
	device test share). No window, no renderer.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_util/SafeArea.h"

using namespace Orkige;

TEST_CASE("SafeAreaInsets from a safe rect = window minus safe on each edge",
	"[unit][safearea]")
{
	// a 1179x2556 phone surface with a 60px top notch and a 40px home bar
	const SafeAreaInsets insets = SafeAreaInsets::fromSafeRect(
		1179, 2556, /*x*/ 0, /*y*/ 60, /*w*/ 1179, /*h*/ 2556 - 60 - 40);
	CHECK(insets.mLeft == 0u);
	CHECK(insets.mTop == 60u);
	CHECK(insets.mRight == 0u);
	CHECK(insets.mBottom == 40u);
}

TEST_CASE("SafeAreaInsets: a full-surface safe rect yields zero insets",
	"[unit][safearea]")
{
	const SafeAreaInsets insets =
		SafeAreaInsets::fromSafeRect(1280, 720, 0, 0, 1280, 720);
	CHECK(insets.mLeft == 0u);
	CHECK(insets.mTop == 0u);
	CHECK(insets.mRight == 0u);
	CHECK(insets.mBottom == 0u);
}

TEST_CASE("SafeAreaInsets never reports a negative inset", "[unit][safearea]")
{
	// a safe rect larger than the surface (defensive; should clamp to zero)
	const SafeAreaInsets insets =
		SafeAreaInsets::fromSafeRect(100, 100, -10, -10, 200, 200);
	CHECK(insets.mLeft == 0u);
	CHECK(insets.mTop == 0u);
	CHECK(insets.mRight == 0u);
	CHECK(insets.mBottom == 0u);
}

TEST_CASE("UiAnchor top-left anchoring clamps below the top inset",
	"[unit][safearea][uianchor]")
{
	SafeAreaInsets insets;
	insets.mTop = 60;
	insets.mLeft = 20;
	float x = 0.0f;
	float y = 0.0f;
	// a 200x40 rect, top-left anchor, 16px margin
	UiAnchor::place(200, 40, 16, 16, 1179, 2556, insets,
		/*anchorRight*/ false, /*anchorBottom*/ false, x, y);
	CHECK(x == 20.0f + 16.0f);	// left inset + margin
	CHECK(y == 60.0f + 16.0f);	// top inset + margin
	CHECK(y >= static_cast<float>(insets.mTop));
}

TEST_CASE("UiAnchor bottom-right anchoring clamps above the home bar",
	"[unit][safearea][uianchor]")
{
	SafeAreaInsets insets;
	insets.mBottom = 40;
	insets.mRight = 24;
	float x = 0.0f;
	float y = 0.0f;
	UiAnchor::place(200, 40, 16, 16, 1179, 2556, insets,
		/*anchorRight*/ true, /*anchorBottom*/ true, x, y);
	// right edge of the rect stays left of the safe right edge
	CHECK((x + 200.0f) <= (1179.0f - static_cast<float>(insets.mRight)));
	// bottom edge stays above the home-bar inset
	CHECK((y + 40.0f) <= (2556.0f - static_cast<float>(insets.mBottom)));
	CHECK(x == (1179.0f - 24.0f - 200.0f - 16.0f));
	CHECK(y == (2556.0f - 40.0f - 40.0f - 16.0f));
}

TEST_CASE("UiAnchor with zero insets and zero margin is the identity corner",
	"[unit][safearea][uianchor]")
{
	SafeAreaInsets insets;	// all zero (desktop)
	float x = 0.0f;
	float y = 0.0f;
	UiAnchor::place(100, 30, 0, 0, 1280, 720, insets, false, false, x, y);
	CHECK(x == 0.0f);
	CHECK(y == 0.0f);
}

TEST_CASE("UiAnchor pins an oversized rect to the top-left safe edge",
	"[unit][safearea][uianchor]")
{
	SafeAreaInsets insets;
	insets.mTop = 50;
	insets.mLeft = 30;
	float x = 0.0f;
	float y = 0.0f;
	// a rect wider/taller than the safe box: clamps to the anchored edge
	UiAnchor::place(5000, 5000, 0, 0, 1179, 2556, insets, false, false, x, y);
	CHECK(x == 30.0f);
	CHECK(y == 50.0f);
}
