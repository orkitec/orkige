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
