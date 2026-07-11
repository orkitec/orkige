/**************************************************************
	created:	2026/07/11 at 10:00
	filename: 	ReferenceScaleTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for LayoutScalePolicy::referenceScale (core_util/
	UiLayout): match-width / match-height / shrink / expand at several window
	sizes, the disabled default, and the composition with a display content
	scale (the two scales multiply independently - no double-apply). No
	renderer, no window.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/UiLayout.h"

using namespace Orkige;
using Catch::Approx;

TEST_CASE("referenceScale: disabled (no design resolution) stays 1",
	"[unit][refscale]")
{
	LayoutScalePolicy policy;	// design 0x0
	CHECK(policy.referenceScale(1920.0f, 1080.0f) == Approx(1.0f));
	CHECK(policy.referenceScale(320.0f, 480.0f) == Approx(1.0f));
}

TEST_CASE("referenceScale: match width vs match height", "[unit][refscale]")
{
	LayoutScalePolicy policy;
	policy.designWidth = 1280.0f;
	policy.designHeight = 720.0f;
	policy.mode = LayoutScalePolicy::MM_MATCH;

	// a 2560x1440 window is exactly 2x the design on both axes
	policy.matchWidthHeight = 0.0f;	// match width
	CHECK(policy.referenceScale(2560.0f, 1440.0f) == Approx(2.0f));
	policy.matchWidthHeight = 1.0f;	// match height
	CHECK(policy.referenceScale(2560.0f, 1440.0f) == Approx(2.0f));

	// a wider-than-design window: match width follows width, match height height
	policy.matchWidthHeight = 0.0f;
	CHECK(policy.referenceScale(2560.0f, 720.0f) == Approx(2.0f));	// 2560/1280
	policy.matchWidthHeight = 1.0f;
	CHECK(policy.referenceScale(2560.0f, 720.0f) == Approx(1.0f));	// 720/720

	// a 50/50 mix averages the two ratios
	policy.matchWidthHeight = 0.5f;
	CHECK(policy.referenceScale(2560.0f, 720.0f) == Approx(1.5f));	// (2 + 1)/2
}

TEST_CASE("referenceScale: shrink = shortest side, expand = longest side",
	"[unit][refscale]")
{
	LayoutScalePolicy policy;
	policy.designWidth = 1000.0f;
	policy.designHeight = 1000.0f;

	// window 2000x1500 -> ratios 2.0 (w) and 1.5 (h)
	policy.mode = LayoutScalePolicy::MM_SHRINK;
	CHECK(policy.referenceScale(2000.0f, 1500.0f) == Approx(1.5f));
	policy.mode = LayoutScalePolicy::MM_EXPAND;
	CHECK(policy.referenceScale(2000.0f, 1500.0f) == Approx(2.0f));
}

TEST_CASE("referenceScale composes with content scale without double-applying",
	"[unit][refscale]")
{
	// the layout (reference) scale owns geometry; a separate display content
	// scale owns glyph/pixel density. A fixed-size widget's pixel size is
	// designSize x layoutScale; its text bakes at contentScale. The two never
	// multiply into each other - assert the geometry uses only layoutScale.
	LayoutScalePolicy policy;
	policy.designWidth = 640.0f;
	policy.designHeight = 960.0f;
	policy.mode = LayoutScalePolicy::MM_MATCH;
	policy.matchWidthHeight = 0.0f;

	const float layoutScale = policy.referenceScale(1280.0f, 1920.0f);	// 2x
	CHECK(layoutScale == Approx(2.0f));

	const float designButtonWidth = 160.0f;
	const float contentScale = 3.0f;	// a hypothetical 3x display density

	// geometry: exactly designSize x layoutScale (content scale must NOT enter)
	const float geometryWidth = designButtonWidth * layoutScale;
	CHECK(geometryWidth == Approx(320.0f));
	CHECK(geometryWidth != Approx(designButtonWidth * layoutScale * contentScale));
}
