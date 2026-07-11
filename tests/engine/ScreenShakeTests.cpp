/**************************************************************
	created:	2026/07/11 at 12:00
	filename: 	ScreenShakeTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the pure screen-shake wobble sample
	(ScreenShake::sampleOffset): a decaying wobble that is bounded by the
	amplitude, is nonzero mid-shake and lands at EXACTLY (0,0) at/after the
	duration (so the live camera restores to its rest pose). The rendered
	proof (a shaking camera returns exactly to rest) is the player
	shake/time-scale selfcheck.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <engine_graphic/ScreenShake.h>

#include <cmath>

using Catch::Approx;
using Orkige::ScreenShake;

TEST_CASE("ScreenShake sample decays to exactly zero at the end", "[unit][juice]")
{
	const float amplitude = 2.0f;
	const float duration = 1.0f;
	const float frequency = 20.0f;
	// at/after the duration the offset is EXACTLY zero on both axes
	float x = 1.0f, y = 1.0f;
	ScreenShake::sampleOffset(amplitude, duration, frequency, duration, x, y);
	REQUIRE(x == Approx(0.0f));
	REQUIRE(y == Approx(0.0f));
	ScreenShake::sampleOffset(amplitude, duration, frequency, duration + 0.5f,
		x, y);
	REQUIRE(x == Approx(0.0f));
	REQUIRE(y == Approx(0.0f));
}

TEST_CASE("ScreenShake sample stays within the amplitude and decays",
	"[unit][juice]")
{
	const float amplitude = 3.0f;
	const float duration = 2.0f;
	const float frequency = 15.0f;
	float maxEarly = 0.0f;
	float maxLate = 0.0f;
	for(float t = 0.0f; t < duration; t += 0.01f)
	{
		float x = 0.0f, y = 0.0f;
		ScreenShake::sampleOffset(amplitude, duration, frequency, t, x, y);
		// never exceeds the amplitude on either axis
		REQUIRE(std::abs(x) <= amplitude + 1.0e-4f);
		REQUIRE(std::abs(y) <= amplitude + 1.0e-4f);
		const float magnitude = std::sqrt(x * x + y * y);
		if(t < duration * 0.5f)
		{
			maxEarly = std::max(maxEarly, magnitude);
		}
		else
		{
			maxLate = std::max(maxLate, magnitude);
		}
	}
	// the wobble is a real shake (nonzero) and clearly decays over time
	REQUIRE(maxEarly > 0.0f);
	REQUIRE(maxLate < maxEarly);
}

TEST_CASE("ScreenShake sample is a no-op for degenerate parameters",
	"[unit][juice]")
{
	float x = 5.0f, y = 5.0f;
	ScreenShake::sampleOffset(0.0f, 1.0f, 20.0f, 0.3f, x, y);	// zero amplitude
	REQUIRE(x == Approx(0.0f));
	REQUIRE(y == Approx(0.0f));
	ScreenShake::sampleOffset(2.0f, 0.0f, 20.0f, 0.3f, x, y);	// zero duration
	REQUIRE(x == Approx(0.0f));
	REQUIRE(y == Approx(0.0f));
}
