/**************************************************************
	created:	2026/07/22 at 15:00
	filename: 	GradeMathTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless proof of the shared output-grade curve (GradeMath) and the
	GradeDesc sanitiser: identity at the neutral setting, a monotonic
	contrast S-curve with a 0.5 fixed point, saturation about luma,
	clamp-safety on the un-tonemapped clip, and the desc's honest
	contrast/saturation ceilings. The rendered proof (a scene's saturation +
	contrast measurably changing, matched across flavors) is the
	render_facade_selfcheck grade leg + the grade_look_parity probe.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <core_util/GradeMath.h>
#include <core_util/GradeDesc.h>

using namespace Orkige;
using Catch::Matchers::WithinAbs;

namespace
{
	//! grade one colour by value (the in-place helper is what the shaders mirror)
	struct Rgb { float r, g, b; };
	Rgb graded(Rgb in, float contrast, float saturation)
	{
		GradeMath::apply(in.r, in.g, in.b, contrast, saturation);
		return in;
	}
}

TEST_CASE("GradeMath: neutral is the identity", "[grademath]")
{
	// contrast 0 + saturation 1 leaves every in-range colour untouched
	for(float v = 0.0f; v <= 1.0f; v += 0.1f)
	{
		Rgb out = graded({ v, v * 0.5f, 1.0f - v }, 0.0f, 1.0f);
		CHECK_THAT(out.r, WithinAbs(v, 1e-6f));
		CHECK_THAT(out.g, WithinAbs(v * 0.5f, 1e-6f));
		CHECK_THAT(out.b, WithinAbs(1.0f - v, 1e-6f));
	}
}

TEST_CASE("GradeMath: contrast keeps the 0.5 pivot and pushes the ends",
	"[grademath]")
{
	// the smoothstep S-curve has an exact 0.5 fixed point (mid stays mid)
	Rgb mid = graded({ 0.5f, 0.5f, 0.5f }, 1.0f, 1.0f);
	CHECK_THAT(mid.r, WithinAbs(0.5f, 1e-6f));
	// a dark below 0.5 darkens, a bright above 0.5 brightens
	Rgb dark = graded({ 0.25f, 0.25f, 0.25f }, 1.0f, 1.0f);
	Rgb bright = graded({ 0.75f, 0.75f, 0.75f }, 1.0f, 1.0f);
	CHECK(dark.r < 0.25f);
	CHECK(bright.r > 0.75f);
	// endpoints are fixed points of smoothstep too
	CHECK_THAT(graded({ 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f).r, WithinAbs(0.0f, 1e-6f));
	CHECK_THAT(graded({ 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f).r, WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("GradeMath: the contrast curve stays monotonic for contrast in [0,1]",
	"[grademath]")
{
	// sample a rising ramp through the strongest curve: the output must rise too
	for(float contrast = 0.0f; contrast <= 1.0f; contrast += 0.25f)
	{
		float previous = -1.0f;
		for(float v = 0.0f; v <= 1.0f + 1e-4f; v += 0.02f)
		{
			const float value = graded({ v, v, v }, contrast, 1.0f).r;
			CHECK(value >= previous - 1e-6f);
			previous = value;
		}
	}
}

TEST_CASE("GradeMath: saturation moves toward and away from luma", "[grademath]")
{
	const Rgb colour = { 0.8f, 0.2f, 0.2f };
	const float l = GradeMath::luma(colour.r, colour.g, colour.b);
	// saturation 0 collapses every channel to the luma (greyscale)
	Rgb grey = graded(colour, 0.0f, 0.0f);
	CHECK_THAT(grey.r, WithinAbs(l, 1e-6f));
	CHECK_THAT(grey.g, WithinAbs(l, 1e-6f));
	CHECK_THAT(grey.b, WithinAbs(l, 1e-6f));
	// saturation > 1 pushes the channels further from the luma than the input
	Rgb punchy = graded(colour, 0.0f, 1.5f);
	CHECK(punchy.r > colour.r);			// the above-luma channel rises
	CHECK(punchy.g < colour.g);			// a below-luma channel falls
}

TEST_CASE("GradeMath: results are clamp-safe on the un-tonemapped clip",
	"[grademath]")
{
	// out-of-range inputs and heavy over-saturation stay within [0;1]
	Rgb a = graded({ 1.4f, -0.2f, 0.5f }, 1.0f, 3.0f);
	for(float channel : { a.r, a.g, a.b })
	{
		CHECK(channel >= 0.0f);
		CHECK(channel <= 1.0f);
	}
}

TEST_CASE("GradeDesc: defaults are the neutral OFF state", "[gradedesc]")
{
	GradeDesc desc;
	CHECK_FALSE(desc.enabled);
	CHECK_THAT(desc.contrast, WithinAbs(0.0f, 1e-6f));
	CHECK_THAT(desc.saturation, WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("GradeDesc: the sanitiser holds the honest ceilings", "[gradedesc]")
{
	GradeDesc desc;
	desc.contrast = 2.5f;		// beyond the monotonic ceiling
	desc.saturation = -1.0f;	// negative
	GradeDesc clean = desc.sanitised();
	CHECK_THAT(clean.contrast, WithinAbs(1.0f, 1e-6f));
	CHECK_THAT(clean.saturation, WithinAbs(0.0f, 1e-6f));

	desc.contrast = -0.5f;
	desc.saturation = 9.0f;
	clean = desc.sanitised();
	CHECK_THAT(clean.contrast, WithinAbs(0.0f, 1e-6f));
	CHECK_THAT(clean.saturation, WithinAbs(4.0f, 1e-6f));
}
