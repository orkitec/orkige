/**************************************************************
	created:	2026/07/07 at 14:00
	filename: 	SceneSerializerTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless GameObjectManager round-trip with core-only components.
	Engine-level components (TransformComponent etc.) need a booted
	renderer and cannot run here - the engine-level scene round-trip
	is covered by the editor_selfcheck integration test.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"
#include "TestComponents.h"

#include <core_game/SceneSerializer.h>
#include <core_serialization/XMLArchive.h>

#include <filesystem>

namespace
{
	struct TempScene
	{
		Orkige::String path;
		explicit TempScene(std::string const & name)
			: path((std::filesystem::temp_directory_path() / name).string())
		{
			std::filesystem::remove(this->path);
		}
		~TempScene()
		{
			std::error_code ignored;
			std::filesystem::remove(this->path, ignored);
		}
	};
}

TEST_CASE("SceneSerializer round-trips GameObjects with component state", "[scene]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	Orkige::registerOrkigeTestComponents();
	Orkige::GameObjectManager & manager = env.gameObjectManager;
	manager.clear();
	TempScene scene("orkige_test_scene.oscene");

	{
		optr<Orkige::GameObject> alpha =
			manager.createGameObject("Alpha").lock();
		REQUIRE(alpha);
		REQUIRE(alpha->addComponent<Orkige::TestHealthComponent>());
		alpha->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(7);

		optr<Orkige::GameObject> beta =
			manager.createGameObject("Beta").lock();
		REQUIRE(beta);
		// armor pulls health in as dependency - the loader must cope with
		// components that dependencies already added
		REQUIRE(beta->addComponent<Orkige::TestArmorComponent>());
		beta->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(99);
		beta->getComponentPtr<Orkige::TestArmorComponent>()->setArmor(3);
	}

	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));

	// wipe the world and prove it is gone
	manager.clear();
	REQUIRE(manager.getGameObjects().empty());

	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));

	REQUIRE(manager.getGameObjects().size() == 2);
	optr<Orkige::GameObject> alpha = manager.getGameObject("Alpha").lock();
	REQUIRE(alpha);
	REQUIRE(alpha->hasComponent<Orkige::TestHealthComponent>());
	CHECK_FALSE(alpha->hasComponent<Orkige::TestArmorComponent>());
	CHECK(alpha->getComponentPtr<Orkige::TestHealthComponent>()
		->getHealth() == 7);

	optr<Orkige::GameObject> beta = manager.getGameObject("Beta").lock();
	REQUIRE(beta);
	REQUIRE(beta->hasComponent<Orkige::TestHealthComponent>());
	REQUIRE(beta->hasComponent<Orkige::TestArmorComponent>());
	CHECK(beta->getComponentPtr<Orkige::TestHealthComponent>()
		->getHealth() == 99);
	CHECK(beta->getComponentPtr<Orkige::TestArmorComponent>()
		->getArmor() == 3);

	manager.clear();
}

TEST_CASE("SceneSerializer rejects a missing scene file", "[scene]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	CHECK_FALSE(Orkige::SceneSerializer::loadScene(
		(std::filesystem::temp_directory_path() /
			"orkige_no_such_scene.oscene").string(),
		env.gameObjectManager));
}

TEST_CASE("SceneSerializer rejects a non-scene archive and keeps the current world", "[scene]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	Orkige::registerOrkigeTestComponents();
	Orkige::GameObjectManager & manager = env.gameObjectManager;
	manager.clear();
	TempScene scene("orkige_test_bogus.oscene");

	// a valid XMLArchive that is not a scene (wrong magic)
	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(scene.path));
		Orkige::String notMagic = "not_a_scene_file";
		ar << notMagic;
		REQUIRE(ar->stopWriting());
	}

	REQUIRE(manager.createGameObject("Survivor").lock());
	CHECK_FALSE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	// the magic check fails BEFORE the world is cleared
	CHECK(manager.objectExists("Survivor"));
	manager.clear();
}

TEST_CASE("SceneSerializer fails hard on unregistered component types", "[scene]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	Orkige::GameObjectManager & manager = env.gameObjectManager;
	manager.clear();
	TempScene scene("orkige_test_unknown_component.oscene");

	// hand-craft a scene referencing a component type nobody registered
	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(scene.path));
		ar << Orkige::SceneSerializer::SCENE_FORMAT_MAGIC;
		int version = Orkige::SceneSerializer::SCENE_FORMAT_VERSION;
		ar << version;
		unsigned int objectCount = 1;
		ar << objectCount;
		Orkige::String id = "Ghost";
		ar << id;
		unsigned int componentCount = 1;
		ar << componentCount;
		Orkige::String componentTypeName = "NoSuchComponent";
		ar << componentTypeName;
		REQUIRE(ar->stopWriting());
	}

	CHECK_FALSE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	// a failed load must not leave a half-loaded world behind
	CHECK(manager.getGameObjects().empty());
}
