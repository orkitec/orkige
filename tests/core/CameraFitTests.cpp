/**************************************************************
	created:	2026/07/11 at 12:00
	filename: 	CameraFitTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless orthographic fit-policy math: fitHeight keeps the authored
	height across aspects, fitWidth keeps the design width, expand keeps the
	whole design rect visible and only ever grows. Verified across 4:3 .. 21:9.
	The rendered proof (visible world rect on the live window camera) is the
	player camera-fit selfcheck.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <core_util/CameraFit.h>

#include <vector>

using Catch::Approx;
using namespace Orkige::CameraFit;

namespace
{
	//! the aspect band the policy is verified across (portrait to ultrawide)
	std::vector<float> testAspects()
	{
		return { 3.0f / 4.0f, 1.0f, 4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f };
	}
}

TEST_CASE("CameraFit fitHeight keeps the authored height at every aspect",
	"[unit][camera]")
{
	const float designW = 16.0f;
	const float designH = 9.0f;
	for(float aspect : testAspects())
	{
		const float half = orthoHalfHeight(FIT_HEIGHT, designW, designH, aspect);
		// the vertical half-extent is exactly designHeight/2 regardless of aspect
		REQUIRE(half == Approx(designH * 0.5f));
		float visW = 0.0f, visH = 0.0f;
		visibleWorldSize(half, aspect, visW, visH);
		REQUIRE(visH == Approx(designH));		// full design height always shown
		REQUIRE(visW == Approx(designH * aspect));	// width tracks the aspect
	}
}

TEST_CASE("CameraFit fitWidth keeps the design width at every aspect",
	"[unit][camera]")
{
	const float designW = 16.0f;
	const float designH = 9.0f;
	for(float aspect : testAspects())
	{
		const float half = orthoHalfHeight(FIT_WIDTH, designW, designH, aspect);
		float visW = 0.0f, visH = 0.0f;
		visibleWorldSize(half, aspect, visW, visH);
		// the full design WIDTH is always exactly visible; height tracks aspect
		REQUIRE(visW == Approx(designW));
		REQUIRE(visH == Approx(designW / aspect));
	}
}

TEST_CASE("CameraFit expand always shows the whole design rect and only grows",
	"[unit][camera]")
{
	const float designW = 16.0f;
	const float designH = 9.0f;
	const float designAspect = designW / designH;
	for(float aspect : testAspects())
	{
		const float half = orthoHalfHeight(EXPAND, designW, designH, aspect);
		float visW = 0.0f, visH = 0.0f;
		visibleWorldSize(half, aspect, visW, visH);
		// the entire design rect fits (never crops), within float tolerance
		REQUIRE(visW >= Approx(designW).margin(1.0e-4));
		REQUIRE(visH >= Approx(designH).margin(1.0e-4));
		if(aspect > designAspect)
		{
			// wider than design: height is pinned, width expanded
			REQUIRE(visH == Approx(designH));
			REQUIRE(visW >= Approx(designW));
		}
		else
		{
			// taller than (or equal to) design: width is pinned, height expanded
			REQUIRE(visW == Approx(designW));
			REQUIRE(visH >= Approx(designH));
		}
	}
}

TEST_CASE("CameraFit letterbox rect is centered and only bars the slack axis",
	"[unit][camera]")
{
	float left = 0, top = 0, w = 0, h = 0;
	// viewport wider than design -> pillarbox (left/right bars), centered
	letterboxRect(16.0f / 9.0f, 21.0f / 9.0f, left, top, w, h);
	REQUIRE(top == Approx(0.0f));
	REQUIRE(h == Approx(1.0f));
	REQUIRE(w < 1.0f);
	REQUIRE(left == Approx((1.0f - w) * 0.5f));
	// viewport taller than design -> letterbox (top/bottom bars), centered
	letterboxRect(16.0f / 9.0f, 4.0f / 3.0f, left, top, w, h);
	REQUIRE(left == Approx(0.0f));
	REQUIRE(w == Approx(1.0f));
	REQUIRE(h < 1.0f);
	REQUIRE(top == Approx((1.0f - h) * 0.5f));
	// exact match -> no bars
	letterboxRect(1.5f, 1.5f, left, top, w, h);
	REQUIRE(left == Approx(0.0f));
	REQUIRE(top == Approx(0.0f));
	REQUIRE(w == Approx(1.0f));
	REQUIRE(h == Approx(1.0f));
}
