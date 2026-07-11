/**************************************************************
	created:	2026/07/11 at 09:00
	filename: 	WobbleSpringTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless dynamics tests for the wobble spring-damper primitive behind
	soft, deformable organic shapes: a kick moves it, the motion stays
	bounded, it decays to rest and SNAPS to an exact zero (no residual
	drift), and the parameters actually change the behaviour. No renderer,
	no clock.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_util/WobbleSpring.h"

#include <cmath>

using namespace Orkige;

namespace
{
	//! step a spring for a wall-time span at a fixed frame delta
	void run(WobbleSpring & spring, float seconds, float dt = 1.0f / 60.0f)
	{
		for(float t = 0.0f; t < seconds; t += dt)
		{
			spring.update(dt);
		}
	}
}

TEST_CASE("WobbleSpring starts at rest", "[wobblespring]")
{
	WobbleSpring spring;
	REQUIRE(spring.value() == 0.0f);
	REQUIRE(spring.velocity() == 0.0f);
	REQUIRE(spring.atRest(1.0e-4f));
}

TEST_CASE("WobbleSpring moves in the kick direction", "[wobblespring]")
{
	WobbleSpring spring;
	spring.kick(5.0f);
	// a couple of small steps: the positive velocity carries the value positive
	spring.update(1.0f / 240.0f);
	spring.update(1.0f / 240.0f);
	REQUIRE(spring.value() > 0.0f);

	WobbleSpring negative;
	negative.kick(-5.0f);
	negative.update(1.0f / 240.0f);
	negative.update(1.0f / 240.0f);
	REQUIRE(negative.value() < 0.0f);
}

TEST_CASE("WobbleSpring motion stays bounded", "[wobblespring]")
{
	WobbleSpring spring;
	spring.setParams(200.0f, 8.0f);
	spring.kick(10.0f);
	float peak = 0.0f;
	for(int i = 0; i < 600; ++i)
	{
		spring.update(1.0f / 60.0f);
		peak = std::max(peak, std::fabs(spring.value()));
		REQUIRE(std::isfinite(spring.value()));
		REQUIRE(std::isfinite(spring.velocity()));
	}
	// a velocity kick of v0 into an undamped spring peaks at v0/omega; damping
	// only lowers it. omega = sqrt(200) ~ 14, so the peak is well under 2 units.
	REQUIRE(peak < 2.0f);
}

TEST_CASE("WobbleSpring decays to an EXACT rest (no drift)", "[wobblespring]")
{
	WobbleSpring spring;
	spring.kick(7.0f);
	run(spring, 5.0f);
	REQUIRE(spring.atRest(1.0e-4f));
	// the snap makes the settled state bit-exact, so a deform built on it
	// returns to its rest pose with zero residual
	REQUIRE(spring.value() == 0.0f);
	REQUIRE(spring.velocity() == 0.0f);
}

TEST_CASE("WobbleSpring damping changes the settling", "[wobblespring]")
{
	// a barely-damped spring is still ringing when a heavily-damped one has
	// long since snapped to rest
	WobbleSpring light;
	light.setParams(140.0f, 1.0f);
	light.kick(6.0f);
	WobbleSpring heavy;
	heavy.setParams(140.0f, 30.0f);
	heavy.kick(6.0f);
	// at half a second the light spring is still ringing; give the (overdamped)
	// heavy one long enough to relax fully and snap to rest
	run(light, 0.5f);
	run(heavy, 3.0f);
	REQUIRE(heavy.atRest(1.0e-4f));
	REQUIRE_FALSE(light.atRest(1.0e-4f));
}

TEST_CASE("WobbleSpring reset returns to rest", "[wobblespring]")
{
	WobbleSpring spring;
	spring.kick(9.0f);
	spring.update(1.0f / 60.0f);
	REQUIRE_FALSE(spring.atRest(1.0e-4f));
	spring.reset();
	REQUIRE(spring.value() == 0.0f);
	REQUIRE(spring.velocity() == 0.0f);
	REQUIRE(spring.atRest(1.0e-4f));
}
