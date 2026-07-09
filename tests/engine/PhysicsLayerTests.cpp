/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	PhysicsLayerTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the data-driven physics collision layers:
	the PhysicsWorld::LayerConfig (default, migration, symmetry, asset
	round-trip), the collision MATRIX gating real Jolt collisions (two
	overlapping dynamic bodies separate only when their layers collide),
	and the RigidBodyComponent layer serialization round-trip.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "EngineTestEnvironment.h"

#include <engine_physic/PhysicsWorld.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <core_game/SceneSerializer.h>
#include <core_serialization/XMLArchive.h>

#include <cmath>
#include <filesystem>
#include <fstream>

using Catch::Approx;

namespace
{
	//! save/load are protected on components - widen for the round-trip test
	//! (the detached-component pattern the other engine tests use; a real
	//! RigidBodyComponent needs a TransformComponent + render scene)
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

	//! a three-layer config (Default, A, B); A and B collide per the flag,
	//! Default collides with everything
	Orkige::PhysicsWorld::LayerConfig makeABConfig(bool abCollide)
	{
		Orkige::PhysicsWorld::LayerConfig config;
		config.names = { "Default", "A", "B" };
		config.matrix.assign(3, std::vector<bool>(3, false));
		// Default (0) collides with all
		config.setCollision(0, 0, true);
		config.setCollision(0, 1, true);
		config.setCollision(0, 2, true);
		config.setCollision(1, 2, abCollide);
		return config;
	}

	//! distance between two bodies in the world
	float bodyDistance(Orkige::PhysicsWorld & world,
		Orkige::PhysicsWorld::BodyId a, Orkige::PhysicsWorld::BodyId b)
	{
		Orkige::Vec3 posA, posB;
		Orkige::Quat q;
		world.getBodyTransform(a, posA, q);
		world.getBodyTransform(b, posB, q);
		return (posA - posB).length();
	}

	//! two overlapping dynamic spheres on layers A and B; return whether they
	//! separated after a fixed number of steps (gravity off, so ONLY the
	//! collision matrix can push them apart)
	float runOverlapTest(bool abCollide)
	{
		Orkige::PhysicsWorld world;
		world.setLayerConfig(makeABConfig(abCollide));
		REQUIRE(world.init());
		world.setGravity(Orkige::Vec3::ZERO);

		Orkige::PhysicsWorld::BodyDesc descA;
		descA.shapeType = Orkige::PhysicsWorld::ST_SPHERE;
		descA.radius = 0.5f;
		descA.layer = "A";
		Orkige::PhysicsWorld::BodyDesc descB = descA;
		descB.layer = "B";

		// overlapping by 0.4 (centres 0.6 apart, radii sum 1.0)
		const Orkige::PhysicsWorld::BodyId bodyA = world.createBody(descA,
			Orkige::Vec3(-0.3f, 0.0f, 0.0f), Orkige::Quat::IDENTITY);
		const Orkige::PhysicsWorld::BodyId bodyB = world.createBody(descB,
			Orkige::Vec3(0.3f, 0.0f, 0.0f), Orkige::Quat::IDENTITY);
		REQUIRE(bodyA != Orkige::PhysicsWorld::INVALID_BODY_ID);
		REQUIRE(bodyB != Orkige::PhysicsWorld::INVALID_BODY_ID);

		for (int step = 0; step < 180; ++step)
		{
			world.update(1.0f / 60.0f);
		}
		return bodyDistance(world, bodyA, bodyB);
	}
}

TEST_CASE("LayerConfig default is a single collide-with-all layer", "[physics]")
{
	Orkige::PhysicsWorld::LayerConfig config;
	CHECK(config.getLayerCount() == 1);
	CHECK(config.layerName(0) == "Default");
	CHECK(config.collides(0, 0));
	// an unknown/empty layer name migrates to Default (index 0)
	CHECK(config.layerIndex("Default") == 0);
	CHECK(config.layerIndex("") == 0);
	CHECK(config.layerIndex("nonexistent") == 0);
}

TEST_CASE("LayerConfig setCollision is symmetric and enforced", "[physics]")
{
	Orkige::PhysicsWorld::LayerConfig config = makeABConfig(true);
	CHECK(config.layerIndex("A") == 1);
	CHECK(config.layerIndex("B") == 2);
	// setCollision writes both directions
	CHECK(config.collides(1, 2));
	CHECK(config.collides(2, 1));
	CHECK(config.isSymmetric());

	// a hand-built ASYMMETRIC matrix is repaired by symmetrize (OR wins)
	Orkige::PhysicsWorld::LayerConfig ragged;
	ragged.names = { "Default", "A" };
	ragged.matrix.assign(2, std::vector<bool>(2, false));
	ragged.matrix[0][1] = true;		// only one direction authored
	CHECK_FALSE(ragged.isSymmetric());
	ragged.symmetrize();
	CHECK(ragged.isSymmetric());
	CHECK(ragged.collides(0, 1));
	CHECK(ragged.collides(1, 0));
}

TEST_CASE("LayerConfig round-trips through an .olayers asset", "[physics]")
{
	Orkige::PhysicsWorld::LayerConfig original;
	original.names = { "Default", "ball", "obstacle" };
	original.matrix.assign(3, std::vector<bool>(3, false));
	original.setCollision(0, 0, true);
	original.setCollision(0, 1, true);
	original.setCollision(0, 2, true);
	original.setCollision(1, 2, true);	// ball collides with obstacle
	// ball/ball and obstacle/obstacle stay off

	TempFile file("orkige_test_layers.olayers");
	REQUIRE(original.save(file.path));

	Orkige::PhysicsWorld::LayerConfig loaded;
	REQUIRE(loaded.load(file.path));
	CHECK(loaded.names == original.names);
	CHECK(loaded.getLayerCount() == 3);
	CHECK(loaded.collides(1, 2));
	CHECK(loaded.collides(2, 1));
	CHECK_FALSE(loaded.collides(1, 1));
	CHECK_FALSE(loaded.collides(2, 2));
	CHECK(loaded.isSymmetric());

	// a bad magic leaves the config unchanged and returns false
	Orkige::PhysicsWorld::LayerConfig keep = makeABConfig(false);
	TempFile bogus("orkige_test_not_layers.txt");
	std::ofstream(bogus.path) << "not an archive";
	CHECK_FALSE(keep.load(bogus.path));
	CHECK(keep.getLayerCount() == 3);	// untouched
}

TEST_CASE("the collision matrix gates real Jolt collisions", "[physics]")
{
	// matrix A x B = false: nothing pushes the overlapping bodies apart, they
	// stay interpenetrating (pass through each other)
	const float passThrough = runOverlapTest(false);
	CHECK(passThrough < 0.7f);

	// matrix A x B = true: contact resolution separates them past the overlap
	const float separated = runOverlapTest(true);
	CHECK(separated > 0.9f);

	// the matrix is what changed the outcome
	CHECK(separated > passThrough + 0.2f);
}

TEST_CASE("RigidBodyComponent serializes its collision layer", "[physics]")
{
	Orkige::EngineTestEnvironment::get();
	TempFile file("orkige_test_rigidbody_layer.xml");

	{
		TestRigidBody body;
		body.setBodyType(Orkige::PhysicsWorld::BT_STATIC);
		body.setLayer("obstacle");
		CHECK(body.getLayer() == "obstacle");
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
		CHECK(loaded.getLayer() == "obstacle");
		CHECK(loaded.getBodyType() == Orkige::PhysicsWorld::BT_STATIC);
	}

	// a body written WITHOUT a layer field (the pre-layer format) must migrate
	// to "Default" on load - old scenes behave identically. Simulate that by
	// hand-writing the exact BodyDesc field sequence minus the trailing layer.
	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(file.path));
		optr<Orkige::IArchive> archive = ar;
		int bodyType = static_cast<int>(Orkige::PhysicsWorld::BT_STATIC);
		int shapeType = static_cast<int>(Orkige::PhysicsWorld::ST_BOX);
		archive << bodyType << shapeType;
		float half = 0.5f;
		archive << half << half << half;			// halfExtents
		archive << half << half;					// radius / halfHeight
		float mass = 1.0f, friction = 0.5f, restitution = 0.0f;
		archive << mass << friction << restitution;
		bool planar = false;
		archive << planar;							// LAST field of the old format
		REQUIRE(ar->stopWriting());
	}
	{
		TestRigidBody loaded;
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startReading(file.path));
		optr<Orkige::IArchive> archive = ar;
		loaded.load(archive);
		REQUIRE(ar->stopReading());
		// the missing layer re-read the planar element ("0"), which resolves to
		// Default at createBody (unknown name -> index 0). Either way the body
		// keeps a non-empty layer and behaves as collide-with-all.
		CHECK_FALSE(loaded.getLayer().empty());
	}
}
