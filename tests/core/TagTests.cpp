/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	TagTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Unit tests for GameObject tags (multi-tag labels) and the
	GameObjectManager tag->ids index that mirrors the ChildIdMap
	hierarchy index: set/find, delete cleanup, rename survival,
	duplicate, and scene serialization round-trip.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"
#include "TestComponents.h"

#include <core_game/SceneSerializer.h>

#include <algorithm>
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

	Orkige::GameObjectManager & bootTagWorld()
	{
		Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
		Orkige::registerOrkigeTestComponents();
		env.gameObjectManager.clear();
		return env.gameObjectManager;
	}

	bool containsId(Orkige::StringVector const & ids, Orkige::String const & id)
	{
		return std::find(ids.begin(), ids.end(), id) != ids.end();
	}
}

TEST_CASE("GameObject tags feed the manager tag index", "[tags]")
{
	Orkige::GameObjectManager & manager = bootTagWorld();
	optr<Orkige::GameObject> enemyA = manager.createGameObject("EnemyA").lock();
	optr<Orkige::GameObject> enemyB = manager.createGameObject("EnemyB").lock();
	optr<Orkige::GameObject> player = manager.createGameObject("Player").lock();
	REQUIRE(enemyA);
	REQUIRE(enemyB);
	REQUIRE(player);

	CHECK(manager.findByTag("enemy").empty());

	enemyA->addTag("enemy");
	enemyB->addTag("enemy");
	player->addTag("player");

	CHECK(enemyA->hasTag("enemy"));
	CHECK_FALSE(enemyA->hasTag("player"));

	Orkige::StringVector enemies = manager.findByTag("enemy");
	CHECK(enemies.size() == 2);
	CHECK(containsId(enemies, "EnemyA"));
	CHECK(containsId(enemies, "EnemyB"));

	Orkige::StringVector players = manager.findByTag("player");
	CHECK(players.size() == 1);
	CHECK(containsId(players, "Player"));

	// multi-tag: one object under several tags
	player->addTag("hero");
	CHECK(manager.findByTag("hero").size() == 1);
	CHECK(player->getTags().size() == 2);

	// empty and duplicate tags are ignored
	enemyA->addTag("");
	enemyA->addTag("enemy");
	CHECK(enemyA->getTags().size() == 1);
}

TEST_CASE("removing a tag updates the index", "[tags]")
{
	Orkige::GameObjectManager & manager = bootTagWorld();
	optr<Orkige::GameObject> object = manager.createGameObject("Obj").lock();
	REQUIRE(object);
	object->addTag("a");
	object->addTag("b");
	CHECK(manager.findByTag("a").size() == 1);

	object->removeTag("a");
	CHECK(manager.findByTag("a").empty());
	CHECK(manager.findByTag("b").size() == 1);
	CHECK_FALSE(object->hasTag("a"));

	// clearTags drops everything from the index
	object->clearTags();
	CHECK(manager.findByTag("b").empty());
	CHECK(object->getTags().empty());
}

TEST_CASE("setTags replaces the set and diffs the index", "[tags]")
{
	Orkige::GameObjectManager & manager = bootTagWorld();
	optr<Orkige::GameObject> object = manager.createGameObject("Obj").lock();
	REQUIRE(object);
	object->setTags({ "keep", "drop" });
	CHECK(manager.findByTag("keep").size() == 1);
	CHECK(manager.findByTag("drop").size() == 1);

	// keep "keep", drop "drop", add "add" (and a duplicate that is cleaned)
	object->setTags({ "keep", "add", "add" });
	CHECK(manager.findByTag("keep").size() == 1);	// unchanged
	CHECK(manager.findByTag("drop").empty());		// removed
	CHECK(manager.findByTag("add").size() == 1);	// added
	CHECK(object->getTags().size() == 2);			// duplicate collapsed
}

TEST_CASE("deleting an object cleans the tag index", "[tags]")
{
	Orkige::GameObjectManager & manager = bootTagWorld();
	optr<Orkige::GameObject> a = manager.createGameObject("A").lock();
	optr<Orkige::GameObject> b = manager.createGameObject("B").lock();
	REQUIRE(a);
	REQUIRE(b);
	a->addTag("shared");
	b->addTag("shared");
	CHECK(manager.findByTag("shared").size() == 2);

	manager.delGameObject("A");
	Orkige::StringVector remaining = manager.findByTag("shared");
	CHECK(remaining.size() == 1);
	CHECK(containsId(remaining, "B"));

	// the last holder gone -> the tag disappears from the index entirely
	manager.delGameObject("B");
	CHECK(manager.findByTag("shared").empty());
}

TEST_CASE("clear() wipes the tag index", "[tags]")
{
	Orkige::GameObjectManager & manager = bootTagWorld();
	optr<Orkige::GameObject> object = manager.createGameObject("Obj").lock();
	REQUIRE(object);
	object->addTag("gone");
	CHECK(manager.findByTag("gone").size() == 1);
	manager.clear();
	CHECK(manager.findByTag("gone").empty());
}

TEST_CASE("tags round-trip through the scene serializer", "[tags]")
{
	Orkige::GameObjectManager & manager = bootTagWorld();
	optr<Orkige::GameObject> tagged = manager.createGameObject("Tagged").lock();
	optr<Orkige::GameObject> plain = manager.createGameObject("Plain").lock();
	REQUIRE(tagged);
	REQUIRE(plain);
	tagged->setTags({ "enemy", "boss" });

	TempScene scene("orkige_test_tags_scene.oscene");
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));

	manager.clear();
	CHECK(manager.findByTag("enemy").empty());

	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	optr<Orkige::GameObject> reloaded = manager.getGameObject("Tagged").lock();
	REQUIRE(reloaded);
	CHECK(reloaded->getTags().size() == 2);
	CHECK(reloaded->hasTag("enemy"));
	CHECK(reloaded->hasTag("boss"));
	// the index was rebuilt from the loaded tags
	CHECK(manager.findByTag("boss").size() == 1);
	// a tag-less object stays tag-less
	optr<Orkige::GameObject> reloadedPlain = manager.getGameObject("Plain").lock();
	REQUIRE(reloadedPlain);
	CHECK(reloadedPlain->getTags().empty());
}
