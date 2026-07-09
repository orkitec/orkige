/**************************************************************
	created:	2026/07/09 at 18:00
	filename: 	PhysicsContactTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for physics CONTACT events + SENSORS/TRIGGERS
	(WP #88), driving PhysicsWorld directly (the roller-selfcheck /
	physics-test pattern - no GameObjects, no renderer). They lock in the
	load-bearing correctness contract: the worker-thread contact callbacks
	are queued and DRAINED on the main thread into getFrameContacts,
	coalesced per frame; a sensor detects overlaps with NO collision
	response; a body->owner tag round-trips and clears on destroy; while
	PAUSED no contacts fire (a teleport into a sensor waits for resume);
	and a destroyed body in contact never resolves to a live owner. The
	end-to-end worker->queue->drain->Lua path (onContactBegin flipping a
	shared flag) is proven by the player_roller_selfcheck integration run.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "EngineTestEnvironment.h"

#include <engine_physic/PhysicsWorld.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <core_serialization/XMLArchive.h>

#include <filesystem>

namespace
{
	using Orkige::PhysicsWorld;

	//! save/load are protected on components - widen for the round-trip test
	//! (same detached-component pattern as PhysicsLayerTests)
	struct TestRigidBody : Orkige::RigidBodyComponent
	{
		using Orkige::RigidBodyComponent::save;
		using Orkige::RigidBodyComponent::load;
	};

	//! RAII temp file below std::filesystem::temp_directory_path()
	struct TempFile
	{
		Orkige::String path;
		explicit TempFile(std::string const & name)
			: path((std::filesystem::temp_directory_path() / name).string())
		{
			std::error_code ignored;
			std::filesystem::remove(this->path, ignored);
		}
		~TempFile()
		{
			std::error_code ignored;
			std::filesystem::remove(this->path, ignored);
		}
	};

	//! does the frame's drained contacts contain a began/ended event touching
	//! BOTH of the given bodies (order-agnostic)
	bool hasContact(PhysicsWorld & world, PhysicsWorld::BodyId a,
		PhysicsWorld::BodyId b, bool began)
	{
		for (PhysicsWorld::ContactEvent const & event : world.getFrameContacts())
		{
			if (event.began != began)
			{
				continue;
			}
			if ((event.bodyA == a && event.bodyB == b) ||
				(event.bodyA == b && event.bodyB == a))
			{
				return true;
			}
		}
		return false;
	}

	//! step until the frame reports the wanted began/ended contact between a and
	//! b, or the step budget runs out (returns whether it was seen)
	bool stepUntilContact(PhysicsWorld & world, PhysicsWorld::BodyId a,
		PhysicsWorld::BodyId b, bool began, int maxSteps = 30)
	{
		for (int step = 0; step < maxSteps; ++step)
		{
			world.update(1.0f / 60.0f);
			if (hasContact(world, a, b, began))
			{
				return true;
			}
		}
		return false;
	}

	float bodyDistance(PhysicsWorld & world, PhysicsWorld::BodyId a,
		PhysicsWorld::BodyId b)
	{
		Orkige::Vec3 posA, posB;
		Orkige::Quat q;
		world.getBodyTransform(a, posA, q);
		world.getBodyTransform(b, posB, q);
		return (posA - posB).length();
	}
}

TEST_CASE("a static sensor detects a dynamic body: begin then end through the drain", "[physics]")
{
	// default layer config (a single collide-with-all "Default" layer)
	PhysicsWorld world;
	REQUIRE(world.init());
	world.setGravity(Orkige::Vec3::ZERO);

	// a STATIC sensor box at the origin - the roller-goal shape
	PhysicsWorld::BodyDesc sensorDesc;
	sensorDesc.bodyType = PhysicsWorld::BT_STATIC;
	sensorDesc.shapeType = PhysicsWorld::ST_BOX;
	sensorDesc.halfExtents = Orkige::Vec3(0.6f, 0.6f, 0.6f);
	sensorDesc.isSensor = true;
	const PhysicsWorld::BodyUserData sensorTag = 0xA11CE;
	const PhysicsWorld::BodyId sensor = world.createBody(sensorDesc,
		Orkige::Vec3::ZERO, Orkige::Quat::IDENTITY, sensorTag);
	REQUIRE(sensor != PhysicsWorld::INVALID_BODY_ID);

	// a DYNAMIC sphere overlapping the sensor
	PhysicsWorld::BodyDesc ballDesc;
	ballDesc.shapeType = PhysicsWorld::ST_SPHERE;
	ballDesc.radius = 0.5f;
	const PhysicsWorld::BodyUserData ballTag = 0xB0B;
	const PhysicsWorld::BodyId ball = world.createBody(ballDesc,
		Orkige::Vec3(0.2f, 0.0f, 0.0f), Orkige::Quat::IDENTITY, ballTag);
	REQUIRE(ball != PhysicsWorld::INVALID_BODY_ID);

	// the owner tags round-trip (the body->GameObject mapping the drain uses)
	CHECK(world.getBodyUserData(sensor) == sensorTag);
	CHECK(world.getBodyUserData(ball) == ballTag);

	// a begin is drained on the main thread (worker callbacks were queued
	// during Update and coalesced here)
	REQUIRE(stepUntilContact(world, sensor, ball, true));

	// the sensor applied NO collision response: the dynamic body was not
	// pushed out of the overlap (no gravity, no other force -> it stays put)
	CHECK(bodyDistance(world, sensor, ball) < 0.5f);

	// separate them: the contact ends (OnContactRemoved -> drained end)
	world.setBodyTransform(ball, Orkige::Vec3(10.0f, 0.0f, 0.0f),
		Orkige::Quat::IDENTITY);
	REQUIRE(stepUntilContact(world, sensor, ball, false));
}

TEST_CASE("contact begin is coalesced to once per pair per frame", "[physics]")
{
	PhysicsWorld world;
	REQUIRE(world.init());
	world.setGravity(Orkige::Vec3::ZERO);

	PhysicsWorld::BodyDesc sensorDesc;
	sensorDesc.bodyType = PhysicsWorld::BT_STATIC;
	sensorDesc.isSensor = true;
	const PhysicsWorld::BodyId sensor = world.createBody(sensorDesc,
		Orkige::Vec3::ZERO, Orkige::Quat::IDENTITY, 1);
	PhysicsWorld::BodyDesc ballDesc;
	ballDesc.shapeType = PhysicsWorld::ST_SPHERE;
	ballDesc.radius = 0.5f;
	const PhysicsWorld::BodyId ball = world.createBody(ballDesc,
		Orkige::Vec3(0.2f, 0.0f, 0.0f), Orkige::Quat::IDENTITY, 2);

	// a single update() with several sub-steps must still yield at most ONE
	// begin for the pair (dedupe so onContactBegin fires once per frame)
	REQUIRE(stepUntilContact(world, sensor, ball, true));
	int begins = 0;
	for (PhysicsWorld::ContactEvent const & event : world.getFrameContacts())
	{
		if (event.began)
		{
			++begins;
		}
	}
	CHECK(begins == 1);
}

TEST_CASE("a sensor applies no collision response", "[physics]")
{
	// two overlapping DYNAMIC spheres, gravity off - only contact resolution
	// could push them apart. With one a SENSOR, there is none.
	auto runSensorOverlap = [](bool sensor) -> float
	{
		PhysicsWorld world;
		REQUIRE(world.init());
		world.setGravity(Orkige::Vec3::ZERO);

		PhysicsWorld::BodyDesc a;
		a.shapeType = PhysicsWorld::ST_SPHERE;
		a.radius = 0.5f;
		a.isSensor = sensor;
		PhysicsWorld::BodyDesc b;
		b.shapeType = PhysicsWorld::ST_SPHERE;
		b.radius = 0.5f;

		const PhysicsWorld::BodyId bodyA = world.createBody(a,
			Orkige::Vec3(-0.3f, 0.0f, 0.0f), Orkige::Quat::IDENTITY, 1);
		const PhysicsWorld::BodyId bodyB = world.createBody(b,
			Orkige::Vec3(0.3f, 0.0f, 0.0f), Orkige::Quat::IDENTITY, 2);
		for (int step = 0; step < 180; ++step)
		{
			world.update(1.0f / 60.0f);
		}
		return bodyDistance(world, bodyA, bodyB);
	};

	// non-sensor: contact resolution separates the pair past the overlap
	const float solid = runSensorOverlap(false);
	CHECK(solid > 0.9f);
	// sensor: no response, the pair stays interpenetrating
	const float sensed = runSensorOverlap(true);
	CHECK(sensed < 0.7f);
	CHECK(solid > sensed + 0.2f);
}

TEST_CASE("while paused no contacts fire; a teleport into a sensor waits for resume", "[physics]")
{
	PhysicsWorld world;
	REQUIRE(world.init());
	world.setGravity(Orkige::Vec3::ZERO);

	PhysicsWorld::BodyDesc sensorDesc;
	sensorDesc.bodyType = PhysicsWorld::BT_STATIC;
	sensorDesc.isSensor = true;
	const PhysicsWorld::BodyId sensor = world.createBody(sensorDesc,
		Orkige::Vec3::ZERO, Orkige::Quat::IDENTITY, 1);
	PhysicsWorld::BodyDesc ballDesc;
	ballDesc.shapeType = PhysicsWorld::ST_SPHERE;
	ballDesc.radius = 0.5f;
	// spawn the ball WELL AWAY from the sensor
	const PhysicsWorld::BodyId ball = world.createBody(ballDesc,
		Orkige::Vec3(10.0f, 0.0f, 0.0f), Orkige::Quat::IDENTITY, 2);

	// pause, then teleport the ball INTO the sensor. Paused update() is a
	// no-op: Update never runs, so NO contact callback fires.
	world.setPaused(true);
	world.setBodyTransform(ball, Orkige::Vec3::ZERO, Orkige::Quat::IDENTITY);
	for (int step = 0; step < 10; ++step)
	{
		world.update(1.0f / 60.0f);
	}
	CHECK(world.getFrameContacts().empty());

	// resume: the very next step detects the overlap that was set up while paused
	world.setPaused(false);
	CHECK(stepUntilContact(world, sensor, ball, true));
}

TEST_CASE("a destroyed body in contact does not deliver a stale contact", "[physics]")
{
	PhysicsWorld world;
	REQUIRE(world.init());
	world.setGravity(Orkige::Vec3::ZERO);

	PhysicsWorld::BodyDesc sensorDesc;
	sensorDesc.bodyType = PhysicsWorld::BT_STATIC;
	sensorDesc.isSensor = true;
	const PhysicsWorld::BodyId sensor = world.createBody(sensorDesc,
		Orkige::Vec3::ZERO, Orkige::Quat::IDENTITY, 0x5EED);
	PhysicsWorld::BodyDesc ballDesc;
	ballDesc.shapeType = PhysicsWorld::ST_SPHERE;
	ballDesc.radius = 0.5f;
	const PhysicsWorld::BodyId ball = world.createBody(ballDesc,
		Orkige::Vec3(0.2f, 0.0f, 0.0f), Orkige::Quat::IDENTITY, 0xDEAD);

	REQUIRE(stepUntilContact(world, sensor, ball, true));

	// destroy the dynamic body WHILE it is in contact - OnContactRemoved will
	// fire for a body that no longer exists on the next step
	world.destroyBody(ball);
	// the tag is cleared immediately: a stale contact now resolves to "no owner"
	CHECK(world.getBodyUserData(ball) == 0);

	// draining the removal must not crash and must never resolve the dead body
	// to a live owner (the tolerance the main-thread dispatcher relies on)
	for (int step = 0; step < 5; ++step)
	{
		world.update(1.0f / 60.0f);
		for (PhysicsWorld::ContactEvent const & event : world.getFrameContacts())
		{
			if (event.bodyA == ball)
			{
				CHECK(world.getBodyUserData(event.bodyA) == 0);
			}
			if (event.bodyB == ball)
			{
				CHECK(world.getBodyUserData(event.bodyB) == 0);
			}
		}
	}
	// the surviving sensor still resolves to its owner
	CHECK(world.getBodyUserData(sensor) == 0x5EED);
	SUCCEED("the drain tolerated the destroyed body");
}

TEST_CASE("RigidBodyComponent serializes its sensor flag and migrates old scenes", "[physics]")
{
	Orkige::EngineTestEnvironment::get();
	TempFile file("orkige_test_rigidbody_sensor.xml");

	// round-trip: a static sensor on a named layer
	{
		TestRigidBody body;
		body.setBodyType(PhysicsWorld::BT_STATIC);
		body.setLayer("Trigger");
		body.setIsSensor(true);
		CHECK(body.isSensor());
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(file.path));
		optr<Orkige::IArchive> archive = ar;
		body.save(archive);
		REQUIRE(ar->stopWriting());
	}
	{
		TestRigidBody loaded;
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startReading(file.path));
		optr<Orkige::IArchive> archive = ar;
		loaded.load(archive);
		REQUIRE(ar->stopReading());
		CHECK(loaded.isSensor());
		CHECK(loaded.getLayer() == "Trigger");
		CHECK(loaded.getBodyType() == PhysicsWorld::BT_STATIC);
	}

	// migration: a PRE-SENSOR scene ends at the layer STRING field. Loading the
	// missing sensor bool must NOT throw (the layer string is not a bool) and
	// must default to false - old scenes behave identically.
	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(file.path));
		optr<Orkige::IArchive> archive = ar;
		// the GameObjectComponent base fields a real serialized component
		// carries (version uint + empty string) - without them the reads shift
		unsigned int baseVersion = 0;
		Orkige::String baseName = "";
		archive << baseVersion << baseName;
		int bodyType = static_cast<int>(PhysicsWorld::BT_STATIC);
		int shapeType = static_cast<int>(PhysicsWorld::ST_BOX);
		archive << bodyType << shapeType;
		float half = 0.5f;
		archive << half << half << half;			// halfExtents
		archive << half << half;					// radius / halfHeight
		float mass = 1.0f, friction = 0.5f, restitution = 0.0f;
		archive << mass << friction << restitution;
		bool planar = false;
		archive << planar;
		Orkige::String layer = "obstacle";
		archive << layer;							// LAST field of the pre-sensor format
		REQUIRE(ar->stopWriting());
	}
	{
		TestRigidBody loaded;
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startReading(file.path));
		optr<Orkige::IArchive> archive = ar;
		loaded.load(archive);						// must not throw
		REQUIRE(ar->stopReading());
		CHECK(loaded.getLayer() == "obstacle");
		CHECK_FALSE(loaded.isSensor());				// missing field -> not a sensor
	}
}
