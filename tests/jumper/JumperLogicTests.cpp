// Unit tests for the jumper sample's pure gameplay math (JumperLogic.h) -
// the grounded-probe geometry, the kill plane, the exponential approach the
// movement/camera use, and the goal radius check. Headless: no engine boot.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <JumperLogic.h>

using Catch::Approx;
using namespace Orkige::JumperLogic;

TEST_CASE("ground probe starts below the capsule and points down",
	"[jumper][logic]")
{
	// capsule center at (2, 5, -1), halfHeight 0.25, radius 0.35: the feet
	// are at y = 5 - 0.6 = 4.4; the probe must start just OUTSIDE (below)
	// the capsule surface so Jolt's solid-convex ray cast cannot self-hit
	const GroundProbe probe = makeGroundProbe(
		Ogre::Vector3(2.0f, 5.0f, -1.0f), 0.25f, 0.35f, 0.02f, 0.2f);
	CHECK(probe.origin.x == Approx(2.0f));
	CHECK(probe.origin.z == Approx(-1.0f));
	CHECK(probe.origin.y == Approx(4.38f));		// feet 4.4 minus skin 0.02
	CHECK(probe.origin.y < 5.0f - 0.25f - 0.35f);	// strictly outside
	CHECK(probe.direction == Ogre::Vector3::NEGATIVE_UNIT_Y);
	CHECK(probe.maxDistance == Approx(0.2f));
}

TEST_CASE("kill plane respawn bounds", "[jumper][logic]")
{
	CHECK(isBelowKillPlane(-10.001f, -10.0f));
	CHECK_FALSE(isBelowKillPlane(-10.0f, -10.0f));	// exactly on it: not yet
	CHECK_FALSE(isBelowKillPlane(0.0f, -10.0f));
	CHECK(isBelowKillPlane(-0.5f, 0.0f));			// custom kill height
}

TEST_CASE("approach converges without overshoot", "[jumper][logic]")
{
	// single big step must never pass the target (frame-rate independence)
	CHECK(approach(0.0f, 4.5f, 12.0f, 10.0f) <= 4.5f);
	CHECK(approach(0.0f, 4.5f, 12.0f, 10.0f) == Approx(4.5f).margin(1e-3f));
	// dt = 0 keeps the current value
	CHECK(approach(1.25f, 4.5f, 12.0f, 0.0f) == Approx(1.25f));
	// stepping 60 times at 1/60s reaches ~target speed within a second
	float velocity = 0.0f;
	for (int i = 0; i < 60; ++i)
	{
		const float next = approach(velocity, 4.5f, 12.0f, 1.0f / 60.0f);
		CHECK(next >= velocity);	// monotone toward the target
		velocity = next;
	}
	CHECK(velocity == Approx(4.5f).margin(0.01f));
	// works in both directions (decelerating toward zero)
	CHECK(approach(4.5f, 0.0f, 12.0f, 1.0f) == Approx(0.0f).margin(0.01f));
}

TEST_CASE("goal radius check", "[jumper][logic]")
{
	const Ogre::Vector3 goal(37.0f, 1.6f, 0.0f);
	CHECK(reachedGoal(Ogre::Vector3(37.0f, 1.6f, 0.0f), goal, 1.5f));
	CHECK(reachedGoal(Ogre::Vector3(36.0f, 1.6f, 0.0f), goal, 1.5f));
	CHECK(reachedGoal(Ogre::Vector3(37.0f, 1.6f, 1.5f), goal, 1.5f));	// on the rim
	CHECK_FALSE(reachedGoal(Ogre::Vector3(35.0f, 1.6f, 0.0f), goal, 1.5f));
	CHECK_FALSE(reachedGoal(Ogre::Vector3(37.0f, 4.0f, 0.0f), goal, 1.5f));
}
