/**************************************************************
	created:	2026/07/11 at 22:30
	filename: 	SoundVariationTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the pure per-play pitch/volume randomization math a
	SoundSource applies on each play (core_util/SoundVariation). Fixed [0,1)
	samples drive it, so the endpoints/midpoint are exact and the RNG stays out
	of the assertions.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core_util/SoundVariation.h"

using namespace Orkige;
using Catch::Matchers::WithinAbs;

TEST_CASE("variedPitch: a zero range never moves the pitch", "[unit][sound][variation]")
{
	CHECK_THAT(variedPitch(0.0f, 0.0f), WithinAbs(1.0f, 1e-6f));
	CHECK_THAT(variedPitch(0.0f, 0.5f), WithinAbs(1.0f, 1e-6f));
	CHECK_THAT(variedPitch(0.0f, 1.0f), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("variedPitch: the sample walks the symmetric range", "[unit][sound][variation]")
{
	// 0 -> low edge, 0.5 -> centre (1.0), 1 -> high edge, for +/-20%
	CHECK_THAT(variedPitch(0.2f, 0.0f), WithinAbs(0.8f, 1e-6f));
	CHECK_THAT(variedPitch(0.2f, 0.5f), WithinAbs(1.0f, 1e-6f));
	CHECK_THAT(variedPitch(0.2f, 1.0f), WithinAbs(1.2f, 1e-6f));
	// a negative range is treated as its magnitude
	CHECK_THAT(variedPitch(-0.2f, 1.0f), WithinAbs(1.2f, 1e-6f));
}

TEST_CASE("variedPitch: stays strictly positive at large ranges", "[unit][sound][variation]")
{
	// range 2.0 at the low edge would be -1.0; it clamps above zero so AL_PITCH
	// never goes non-positive
	CHECK(variedPitch(2.0f, 0.0f) > 0.0f);
}

TEST_CASE("variedGain: varies around the base and clamps to 0..1",
	"[unit][sound][variation]")
{
	// base 0.5, +/-40%: low 0.3, centre 0.5, high 0.7
	CHECK_THAT(variedGain(0.5f, 0.4f, 0.0f), WithinAbs(0.3f, 1e-6f));
	CHECK_THAT(variedGain(0.5f, 0.4f, 0.5f), WithinAbs(0.5f, 1e-6f));
	CHECK_THAT(variedGain(0.5f, 0.4f, 1.0f), WithinAbs(0.7f, 1e-6f));

	// a base near the ceiling clamps to the 0..1 mixer window on the high edge
	CHECK_THAT(variedGain(1.0f, 0.5f, 1.0f), WithinAbs(1.0f, 1e-6f));
	// and never below zero on the low edge
	CHECK_THAT(variedGain(0.1f, 2.0f, 0.0f), WithinAbs(0.0f, 1e-6f));
}
