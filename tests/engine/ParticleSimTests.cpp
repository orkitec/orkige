/**************************************************************
	created:	2026/07/09 at 14:00
	filename: 	ParticleSimTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests of the pure 2D particle simulation:
	emission rate, gravity integration, lifetime culling, over-life
	size/colour lerps (through the EaseLibrary), the hard capacity cap,
	burst(n) exactness and PRNG reproducibility. ParticleSim is renderer-
	free (no Ogre::Root, no scene) - the rendered end-to-end proof is the
	demo_particles / player_roller integration runs.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <engine_gocomponent/ParticleSim.h>
#include <core_debug/MemoryManager.h>

using Catch::Approx;
using Orkige::ParticleSim;
using Orkige::Vec2;
using Orkige::Vec3;

namespace
{
	//! a burst-only, motionless emitter (no rng spread) - deterministic pool
	//! bookkeeping for the capacity / burst / lifetime tests
	ParticleSim::EmitterParams staticEmitter(int maxParticles)
	{
		ParticleSim::EmitterParams params;
		params.emissionRate = 0.0f;			// burst-only
		params.burstCount = 8;
		params.lifetimeMin = 1000.0f;		// effectively immortal
		params.lifetimeMax = 1000.0f;
		params.spreadAngle = 0.0f;
		params.speedMin = 0.0f;
		params.speedMax = 0.0f;
		params.gravity = Vec2(0.0f, 0.0f);
		params.spinMin = 0.0f;
		params.spinMax = 0.0f;
		params.maxParticles = maxParticles;
		return params;
	}

	//! a burst-only, motionless 3D emitter (no spread, no speed, no gravity) -
	//! the deterministic base for the 3D containment / motion / cap tests
	ParticleSim::EmitterParams staticEmitter3D(int maxParticles)
	{
		ParticleSim::EmitterParams params = staticEmitter(maxParticles);
		params.space3D = true;
		params.worldSpace = true;
		params.direction3D = Vec3(0.0f, 1.0f, 0.0f);
		params.gravity3D = Vec3(0.0f, 0.0f, 0.0f);
		params.wind = Vec3(0.0f, 0.0f, 0.0f);
		params.flutterAmplitude = 0.0f;
		return params;
	}
}

TEST_CASE("ParticleSim: 3D sphere/box volumes contain their spawns",
	"[particles][unit]")
{
	// SPHERE (radius 2): every spawn lands within the radius of the origin
	{
		ParticleSim sim(11u);
		ParticleSim::EmitterParams params = staticEmitter3D(200);
		params.emissionVolume = ParticleSim::EmitterParams::VOLUME_SPHERE;
		params.volumeExtents = Vec3(2.0f, 0.0f, 0.0f);
		sim.setParams(params);
		REQUIRE(sim.burst3D(200, Vec3(0.0f, 0.0f, 0.0f)) == 200);
		for (int index = 0; index < sim.liveCount(); ++index)
		{
			REQUIRE(sim.particleAt(index).position3.length() <= 2.0f + 1e-4f);
		}
	}
	// BOX (half-extents 1,2,3): each axis stays within its half-extent
	{
		ParticleSim sim(12u);
		ParticleSim::EmitterParams params = staticEmitter3D(200);
		params.emissionVolume = ParticleSim::EmitterParams::VOLUME_BOX;
		params.volumeExtents = Vec3(1.0f, 2.0f, 3.0f);
		sim.setParams(params);
		REQUIRE(sim.burst3D(200, Vec3(0.0f, 0.0f, 0.0f)) == 200);
		for (int index = 0; index < sim.liveCount(); ++index)
		{
			Vec3 const & p = sim.particleAt(index).position3;
			REQUIRE(std::fabs(p.x) <= 1.0f + 1e-4f);
			REQUIRE(std::fabs(p.y) <= 2.0f + 1e-4f);
			REQUIRE(std::fabs(p.z) <= 3.0f + 1e-4f);
		}
	}
}

TEST_CASE("ParticleSim: world-space particles ignore a moving emitter, "
	"local-space ones follow it", "[particles][unit]")
{
	// WORLD space: a spawned particle's render position does NOT move when the
	// emitter later moves (weather must not drag with the camera rig)
	{
		ParticleSim sim(21u);
		sim.setParams(staticEmitter3D(4));	// worldSpace = true, motionless
		sim.burst3D(1, Vec3(0.0f, 0.0f, 0.0f));
		ParticleSim::Particle const & p = sim.particleAt(0);
		const Vec3 moved = sim.worldPosition3D(p, Vec3(5.0f, 0.0f, 0.0f));
		REQUIRE(moved.x == Approx(0.0f).margin(1e-5));
		REQUIRE(moved.y == Approx(0.0f).margin(1e-5));
		REQUIRE(moved.z == Approx(0.0f).margin(1e-5));
	}
	// LOCAL space: the same particle's render position tracks the emitter origin
	{
		ParticleSim sim(21u);
		ParticleSim::EmitterParams params = staticEmitter3D(4);
		params.worldSpace = false;
		sim.setParams(params);
		sim.burst3D(1, Vec3(0.0f, 0.0f, 0.0f));	// local offset = 0 (point volume)
		ParticleSim::Particle const & p = sim.particleAt(0);
		const Vec3 moved = sim.worldPosition3D(p, Vec3(5.0f, 0.0f, 0.0f));
		REQUIRE(moved.x == Approx(5.0f).margin(1e-5));
	}
}

TEST_CASE("ParticleSim: 3D gravity + wind integrate analytically",
	"[particles][unit]")
{
	ParticleSim sim(31u);
	ParticleSim::EmitterParams params = staticEmitter3D(4);
	params.gravity3D = Vec3(0.0f, -10.0f, 0.0f);
	params.wind = Vec3(2.0f, 0.0f, 0.0f);
	sim.setParams(params);
	sim.burst3D(1, Vec3(0.0f, 0.0f, 0.0f));	// v0 = 0 (speed range 0)
	const float dt = 0.1f;
	sim.update3D(dt, Vec3(0.0f, 0.0f, 0.0f));
	// one semi-implicit Euler step: v1 = (g + wind)*dt ; x1 = v1*dt
	ParticleSim::Particle const & p = sim.particleAt(0);
	REQUIRE(p.velocity3.x == Approx(2.0f * dt).margin(1e-4));
	REQUIRE(p.velocity3.y == Approx(-10.0f * dt).margin(1e-4));
	REQUIRE(p.position3.x == Approx(2.0f * dt * dt).margin(1e-4));
	REQUIRE(p.position3.y == Approx(-10.0f * dt * dt).margin(1e-4));
}

TEST_CASE("ParticleSim: a 3D particle dies at its lifetime", "[particles][unit]")
{
	ParticleSim sim(32u);
	ParticleSim::EmitterParams params = staticEmitter3D(4);
	params.lifetimeMin = 0.5f;
	params.lifetimeMax = 0.5f;
	sim.setParams(params);
	REQUIRE(sim.burst3D(1, Vec3(0.0f, 0.0f, 0.0f)) == 1);
	sim.update3D(0.4f, Vec3(0.0f, 0.0f, 0.0f));	// age 0.4 < 0.5
	REQUIRE(sim.liveCount() == 1);
	sim.update3D(0.2f, Vec3(0.0f, 0.0f, 0.0f));	// age 0.6 >= 0.5 -> culled
	REQUIRE(sim.liveCount() == 0);
}

TEST_CASE("ParticleSim: the 3D emitter never exceeds its capacity",
	"[particles][unit]")
{
	ParticleSim sim(33u);
	ParticleSim::EmitterParams params = staticEmitter3D(16);
	params.emissionRate = 100000.0f;	// wildly over-emit
	sim.setParams(params);
	sim.start();
	for (int step = 0; step < 40; ++step)
	{
		sim.update3D(0.1f, Vec3(0.0f, 0.0f, 0.0f));
		REQUIRE(sim.liveCount() <= 16);
	}
	REQUIRE(sim.liveCount() == 16);
}

TEST_CASE("ParticleSim: a reserved 3D pool ticks without allocating",
	"[particles][unit][perf]")
{
	ParticleSim sim(34u);
	ParticleSim::EmitterParams params = staticEmitter3D(64);
	params.emissionRate = 500.0f;
	params.lifetimeMin = 0.05f;		// fast turnover: constant spawn + expire
	params.lifetimeMax = 0.10f;
	params.gravity3D = Vec3(0.0f, -9.8f, 0.0f);
	params.flutterAmplitude = 1.0f;	// exercise the flutter branch too
	params.flutterFrequency = 2.0f;
	sim.setParams(params);
	sim.start();
	Orkige::MemoryManager::reset();
	for (int step = 0; step < 200; ++step)
	{
		sim.update3D(0.01f, Vec3(0.0f, 0.0f, 0.0f));
	}
	Orkige::MemoryManager::endFrame();
	REQUIRE(sim.liveCount() <= sim.capacity());
	REQUIRE(Orkige::MemoryManager::lastFrameCount(
		Orkige::MemoryManager::TAG_PARTICLES) == 0);
}

TEST_CASE("ParticleSim: billboard corners face the camera axes",
	"[particles][unit]")
{
	// known orthonormal camera axes: right = +X, up = +Y (looking down -Z)
	const Vec3 right(1.0f, 0.0f, 0.0f);
	const Vec3 up(0.0f, 1.0f, 0.0f);
	const Vec3 center(0.0f, 0.0f, 5.0f);
	Vec3 corners[4];
	ParticleSim::billboardCorners(center, right, up, 2.0f, corners);
	// TL, TR, BR, BL (the SpriteBatch winding)
	REQUIRE(corners[0] == Vec3(-2.0f, 2.0f, 5.0f));	// TL
	REQUIRE(corners[1] == Vec3(2.0f, 2.0f, 5.0f));	// TR
	REQUIRE(corners[2] == Vec3(2.0f, -2.0f, 5.0f));	// BR
	REQUIRE(corners[3] == Vec3(-2.0f, -2.0f, 5.0f));	// BL
}

TEST_CASE("ParticleSim: velocity streaks stretch along the on-screen motion",
	"[particles][unit]")
{
	const Vec3 right(1.0f, 0.0f, 0.0f);
	const Vec3 up(0.0f, 1.0f, 0.0f);
	const Vec3 center(0.0f, 0.0f, 5.0f);
	// a downward-falling rain drop: velocity along -Y, width 1, length 3
	Vec3 corners[4];
	ParticleSim::streakCorners(center, right, up, Vec3(0.0f, -10.0f, 0.0f),
		1.0f, 3.0f, corners);
	// the long (3) axis runs along Y, the cross (1) axis along X
	REQUIRE(corners[0].x == Approx(-1.0f).margin(1e-4));	// TL
	REQUIRE(corners[0].y == Approx(-3.0f).margin(1e-4));
	REQUIRE(corners[1].x == Approx(1.0f).margin(1e-4));	// TR
	REQUIRE(corners[1].y == Approx(-3.0f).margin(1e-4));
	REQUIRE(corners[2].y == Approx(3.0f).margin(1e-4));	// BR
	// a particle moving straight at the camera (no on-screen motion) falls back
	// to a plain camera-facing quad of the width extent
	ParticleSim::streakCorners(center, right, up, Vec3(0.0f, 0.0f, -10.0f),
		1.0f, 3.0f, corners);
	REQUIRE(corners[0] == Vec3(-1.0f, 1.0f, 5.0f));
	REQUIRE(corners[2] == Vec3(1.0f, -1.0f, 5.0f));
}

TEST_CASE("ParticleSim: continuous rate produces N particles over T seconds",
	"[particles][unit]")
{
	ParticleSim sim(1234u);
	ParticleSim::EmitterParams params = staticEmitter(1000);
	params.emissionRate = 100.0f;	// 100 / second
	sim.setParams(params);
	sim.start();

	// 100 fixed steps of 0.01s = 1.0s of emission at 100/s -> 100 particles
	for (int step = 0; step < 100; ++step)
	{
		sim.update(0.01f, Vec2(0.0f, 0.0f));
	}
	REQUIRE(sim.liveCount() == 100);
	REQUIRE(sim.liveCount() <= sim.capacity());
}

TEST_CASE("ParticleSim: never exceeds its capacity", "[particles][unit]")
{
	ParticleSim sim(7u);
	ParticleSim::EmitterParams params = staticEmitter(10);
	params.emissionRate = 10000.0f;	// wildly over-emit
	sim.setParams(params);
	sim.start();
	for (int step = 0; step < 50; ++step)
	{
		sim.update(0.1f, Vec2(0.0f, 0.0f));
		REQUIRE(sim.liveCount() <= 10);
	}
	REQUIRE(sim.liveCount() == 10);
}

TEST_CASE("ParticleSim: a reserved pool spawns and simulates without "
	"allocating", "[particles][unit][perf]")
{
	// the direct allocation contract behind the capacity cap: the pool is
	// reserved up front, so churning particles through spawn/expire/update
	// fires the TAG_PARTICLES growth seam ZERO times
	ParticleSim sim(99u);
	ParticleSim::EmitterParams params = staticEmitter(64);
	params.emissionRate = 500.0f;
	params.lifetimeMin = 0.05f;		// fast turnover: constant spawn+expire
	params.lifetimeMax = 0.10f;
	sim.setParams(params);
	sim.start();
	Orkige::MemoryManager::reset();
	for (int step = 0; step < 200; ++step)
	{
		sim.update(0.01f, Vec2(0.0f, 0.0f));
	}
	Orkige::MemoryManager::endFrame();
	REQUIRE(sim.liveCount() <= sim.capacity());
	REQUIRE(Orkige::MemoryManager::lastFrameCount(
		Orkige::MemoryManager::TAG_PARTICLES) == 0);
}

TEST_CASE("ParticleSim: burst spawns exactly min(n, capacity)",
	"[particles][unit]")
{
	ParticleSim sim(42u);
	sim.setParams(staticEmitter(50));

	REQUIRE(sim.burst(30, Vec2(0.0f, 0.0f)) == 30);
	REQUIRE(sim.liveCount() == 30);
	// only 20 slots remain
	REQUIRE(sim.burst(30, Vec2(0.0f, 0.0f)) == 20);
	REQUIRE(sim.liveCount() == 50);
	// pool full -> zero spawned
	REQUIRE(sim.burst(5, Vec2(0.0f, 0.0f)) == 0);

	// burst(0) falls back to the configured burstCount (8)
	sim.reset();
	sim.setParams(staticEmitter(50));
	REQUIRE(sim.burst(0, Vec2(0.0f, 0.0f)) == 8);
}

TEST_CASE("ParticleSim: a particle dies at its lifetime", "[particles][unit]")
{
	ParticleSim sim(99u);
	ParticleSim::EmitterParams params = staticEmitter(4);
	params.lifetimeMin = 0.5f;
	params.lifetimeMax = 0.5f;
	sim.setParams(params);

	REQUIRE(sim.burst(1, Vec2(0.0f, 0.0f)) == 1);
	sim.update(0.4f, Vec2(0.0f, 0.0f));	// age 0.4 < 0.5
	REQUIRE(sim.liveCount() == 1);
	sim.update(0.2f, Vec2(0.0f, 0.0f));	// age 0.6 >= 0.5 -> culled
	REQUIRE(sim.liveCount() == 0);
}

TEST_CASE("ParticleSim: motion integrates analytically", "[particles][unit]")
{
	// constant velocity (gravity off): position = origin + v*t EXACTLY
	{
		ParticleSim sim(5u);
		ParticleSim::EmitterParams params = staticEmitter(4);
		params.directionAngle = 0.0f;	// +X
		params.spreadAngle = 0.0f;
		params.speedMin = 3.0f;
		params.speedMax = 3.0f;			// v = (3, 0)
		sim.setParams(params);
		sim.burst(1, Vec2(1.0f, 2.0f));
		for (int step = 0; step < 10; ++step)
		{
			sim.update(0.1f, Vec2(0.0f, 0.0f));	// origin ignored (burst-only)
		}
		ParticleSim::Particle const & p = sim.particleAt(0);
		REQUIRE(p.position.x == Approx(1.0f + 3.0f * 1.0f).margin(1e-4));
		REQUIRE(p.position.y == Approx(2.0f).margin(1e-4));
	}
	// gravity, no initial velocity: one semi-implicit Euler step is exact
	{
		ParticleSim sim(6u);
		ParticleSim::EmitterParams params = staticEmitter(4);
		params.speedMin = 0.0f;
		params.speedMax = 0.0f;			// v0 = 0
		params.gravity = Vec2(0.0f, -10.0f);
		sim.setParams(params);
		sim.burst(1, Vec2(0.0f, 0.0f));
		const float dt = 0.1f;
		sim.update(dt, Vec2(0.0f, 0.0f));
		// v1 = g*dt = -1.0 ; y1 = 0 + v1*dt = -0.1
		ParticleSim::Particle const & p = sim.particleAt(0);
		REQUIRE(p.velocity.y == Approx(-10.0f * dt).margin(1e-4));
		REQUIRE(p.position.y == Approx(-10.0f * dt * dt).margin(1e-4));
	}
}

TEST_CASE("ParticleSim: size and colour lerp hit their endpoints through the "
	"eases", "[particles][unit]")
{
	ParticleSim sim(1u);
	ParticleSim::EmitterParams params = staticEmitter(4);
	params.startSize = 1.0f;
	params.endSize = 0.0f;
	params.startColor = Orkige::Color(1.0f, 1.0f, 1.0f, 1.0f);
	params.endColor = Orkige::Color(0.0f, 0.0f, 0.0f, 0.0f);
	params.sizeEase = "linear";
	params.colorEase = "linear";
	sim.setParams(params);

	ParticleSim::Particle particle;
	particle.lifetime = 1.0f;

	particle.age = 0.0f;
	REQUIRE(sim.sizeAt(particle) == Approx(1.0f));
	REQUIRE(sim.colorAt(particle).a == Approx(1.0f));

	particle.age = 0.5f;
	REQUIRE(sim.sizeAt(particle) == Approx(0.5f));
	REQUIRE(sim.colorAt(particle).a == Approx(0.5f));

	particle.age = 1.0f;
	REQUIRE(sim.sizeAt(particle) == Approx(0.0f).margin(1e-6));
	REQUIRE(sim.colorAt(particle).a == Approx(0.0f).margin(1e-6));

	// a non-linear ease still respects the endpoints f(0)=0, f(1)=1
	params.sizeEase = "quadOut";
	sim.setParams(params);
	particle.age = 0.0f;
	REQUIRE(sim.sizeAt(particle) == Approx(1.0f));
	particle.age = 1.0f;
	REQUIRE(sim.sizeAt(particle) == Approx(0.0f).margin(1e-6));
}

TEST_CASE("ParticleSim: the seeded PRNG is reproducible", "[particles][unit]")
{
	ParticleSim::EmitterParams params;	// full defaults: spread + speed range
	params.maxParticles = 128;

	auto runSequence = [&params](std::uint32_t seed)
	{
		ParticleSim sim(seed);
		sim.setParams(params);
		sim.start();
		for (int step = 0; step < 20; ++step)
		{
			sim.update(0.05f, Vec2(0.0f, 0.0f));
		}
		return sim;
	};

	ParticleSim a = runSequence(2026u);
	ParticleSim b = runSequence(2026u);
	REQUIRE(a.liveCount() == b.liveCount());
	REQUIRE(a.liveCount() > 0);
	for (int index = 0; index < a.liveCount(); ++index)
	{
		REQUIRE(a.particleAt(index).position.x ==
			Approx(b.particleAt(index).position.x));
		REQUIRE(a.particleAt(index).position.y ==
			Approx(b.particleAt(index).position.y));
		REQUIRE(a.particleAt(index).velocity.x ==
			Approx(b.particleAt(index).velocity.x));
	}

	// a different seed must diverge (not bit-identical positions)
	ParticleSim c = runSequence(777u);
	bool anyDifference = (c.liveCount() != a.liveCount());
	for (int index = 0; !anyDifference &&
		index < a.liveCount() && index < c.liveCount(); ++index)
	{
		if (a.particleAt(index).position.x != c.particleAt(index).position.x ||
			a.particleAt(index).position.y != c.particleAt(index).position.y)
		{
			anyDifference = true;
		}
	}
	REQUIRE(anyDifference);
}
