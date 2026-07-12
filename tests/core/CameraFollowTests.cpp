/**************************************************************
	created:	2026/07/12 at 16:00
	filename: 	CameraFollowTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless smooth-follow math: the approach factor snaps at zero damping,
	stays in [0,1], is monotone in damping and dt, and iterating approach()
	converges onto a fixed target. The rendered proof (a CameraComponent ortho
	camera tracking a moving object, composing with the fit policy) is the
	player_gameplay_selfcheck integration run.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <core_util/CameraFollow.h>

#include <cmath>

using Catch::Approx;
using namespace Orkige::CameraFollow;

TEST_CASE("CameraFollow smoothFactor snaps at zero damping", "[unit][camera]")
{
	// zero (or negative) damping = instant snap onto the target this frame
	REQUIRE(smoothFactor(0.0f, 0.016f) == Approx(1.0f));
	REQUIRE(smoothFactor(-1.0f, 0.016f) == Approx(1.0f));
}

TEST_CASE("CameraFollow smoothFactor is zero when no time passes",
	"[unit][camera]")
{
	REQUIRE(smoothFactor(0.2f, 0.0f) == Approx(0.0f));
}

TEST_CASE("CameraFollow smoothFactor stays in [0,1] and eases with damping",
	"[unit][camera]")
{
	const float dt = 0.016f;
	float previous = 2.0f;	// larger than any factor
	// as damping grows the per-frame factor shrinks (softer follow), always in range
	for(float damping : { 0.05f, 0.1f, 0.25f, 0.5f, 1.0f })
	{
		const float factor = smoothFactor(damping, dt);
		REQUIRE(factor >= 0.0f);
		REQUIRE(factor <= 1.0f);
		REQUIRE(factor < previous);	// strictly decreasing in damping
		previous = factor;
	}
}

TEST_CASE("CameraFollow smoothFactor covers more ground over a longer frame",
	"[unit][camera]")
{
	const float damping = 0.2f;
	REQUIRE(smoothFactor(damping, 0.032f) > smoothFactor(damping, 0.016f));
}

TEST_CASE("CameraFollow approach converges onto a fixed target", "[unit][camera]")
{
	const float goal = 10.0f;
	const float damping = 0.15f;
	float value = 0.0f;
	float previousGap = std::abs(goal - value);
	for(int i = 0; i < 300; ++i)	// ~5s at 60fps
	{
		value = approach(value, goal, damping, 1.0f / 60.0f);
		const float gap = std::abs(goal - value);
		REQUIRE(gap <= previousGap + 1.0e-5f);	// never diverges
		previousGap = gap;
	}
	REQUIRE(value == Approx(goal).margin(0.01f));	// essentially arrived
}

TEST_CASE("CameraFollow approach snaps in one step at zero damping",
	"[unit][camera]")
{
	REQUIRE(approach(0.0f, 7.5f, 0.0f, 0.016f) == Approx(7.5f));
}
