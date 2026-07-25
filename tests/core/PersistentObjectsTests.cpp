/**************************************************************
	created:	2026/07/25
	filename: 	PersistentObjectsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The persistence-aware scene teardown: a GameObject marked persistent
	survives the level system's mid-play scene switch with its whole live
	state. These headless tests exercise the two pure pieces of that contract
	at the core level - GameObjectManager::clearExceptPersistent (the
	clear-with-filter) and the SceneSerializer duplicate-id rule through a real
	loadScenePreservingPersistent round-trip. Engine-level survival of render
	nodes / physics bodies is proven by the player_persistent_selfcheck ctest.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"
#include "TestComponents.h"

#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>
#include <core_game/SceneSerializer.h>

#include <algorithm>
#include <filesystem>

using Orkige::optr;

namespace
{
	//! a self-cleaning temp scene file path
	struct TempScene
	{
		Orkige::String path;
		explicit TempScene(std::string const & name)
			: path((std::filesystem::temp_directory_path() / name).string())
		{
			std::error_code ignored;
			std::filesystem::remove(this->path, ignored);
		}
		~TempScene()
		{
			std::error_code ignored;
			std::filesystem::remove(this->path, ignored);
		}
	};

	bool objectAlive(Orkige::GameObjectManager & manager, Orkige::String const & id)
	{
		return manager.getGameObject(id).lock() != nullptr;
	}
}

TEST_CASE("clearExceptPersistent spares a persistent root and its whole subtree",
	"[unit][persistent][teardown]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	Orkige::GameObjectManager & manager = env.gameObjectManager;
	manager.clear();

	// a persistent root with a NON-persistent child + grandchild: the whole
	// subtree survives because a persistent parent keeps it. A separate
	// non-persistent object is doomed.
	optr<Orkige::GameObject> hero = manager.createGameObject("Hero").lock();
	REQUIRE(hero);
	hero->setPersistent(true);
	optr<Orkige::GameObject> sword = manager.createGameObject("Sword").lock();
	REQUIRE(sword);
	sword->setParent("Hero", false);
	optr<Orkige::GameObject> gem = manager.createGameObject("Gem").lock();
	REQUIRE(gem);
	gem->setParent("Sword", false);
	optr<Orkige::GameObject> scenery = manager.createGameObject("Scenery").lock();
	REQUIRE(scenery);

	REQUIRE(manager.isPersistentInHierarchy("Hero"));
	REQUIRE(manager.isPersistentInHierarchy("Sword"));	// via persistent parent
	REQUIRE(manager.isPersistentInHierarchy("Gem"));	// via persistent ancestor
	REQUIRE_FALSE(manager.isPersistentInHierarchy("Scenery"));

	manager.clearExceptPersistent();

	CHECK(objectAlive(manager, "Hero"));
	CHECK(objectAlive(manager, "Sword"));
	CHECK(objectAlive(manager, "Gem"));
	CHECK_FALSE(objectAlive(manager, "Scenery"));
	// the surviving subtree keeps its parent links
	CHECK(manager.getGameObject("Sword").lock()->getParentId() == "Hero");
	CHECK(manager.getGameObject("Gem").lock()->getParentId() == "Sword");

	manager.clear();
}

TEST_CASE("clearExceptPersistent re-roots a persistent child of a dying parent",
	"[unit][persistent][teardown]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	Orkige::GameObjectManager & manager = env.gameObjectManager;
	manager.clear();

	// a NON-persistent parent with a persistent child: the parent dies, the
	// child survives and re-roots to the scene root
	optr<Orkige::GameObject> parent = manager.createGameObject("Platform").lock();
	REQUIRE(parent);
	optr<Orkige::GameObject> child = manager.createGameObject("Coin").lock();
	REQUIRE(child);
	child->setParent("Platform", false);
	child->setPersistent(true);
	REQUIRE(child->getParentId() == "Platform");

	manager.clearExceptPersistent();

	CHECK_FALSE(objectAlive(manager, "Platform"));
	REQUIRE(objectAlive(manager, "Coin"));
	// re-rooted to the scene root (its dying parent is gone)
	CHECK(manager.getGameObject("Coin").lock()->getParentId().empty());
	// and it is a root in the child index
	Orkige::StringVector roots = manager.getRootObjectIds();
	CHECK(std::find(roots.begin(), roots.end(), "Coin") != roots.end());

	manager.clear();
}

TEST_CASE("clearExceptPersistent preserves surviving child order",
	"[unit][persistent][teardown]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	Orkige::GameObjectManager & manager = env.gameObjectManager;
	manager.clear();

	// a persistent parent with three children added in a deliberate order;
	// clearExceptPersistent must keep that order in the child index
	optr<Orkige::GameObject> root = manager.createGameObject("Rig").lock();
	REQUIRE(root);
	root->setPersistent(true);
	for (const char* name : { "PartA", "PartB", "PartC" })
	{
		optr<Orkige::GameObject> part = manager.createGameObject(name).lock();
		REQUIRE(part);
		part->setParent("Rig", false);
	}
	Orkige::StringVector before = manager.getChildren("Rig");
	REQUIRE(before.size() == 3);

	manager.clearExceptPersistent();

	Orkige::StringVector after = manager.getChildren("Rig");
	CHECK(after == before);

	manager.clear();
}

TEST_CASE("loadScenePreservingPersistent keeps the survivor and skips the duplicate id",
	"[unit][persistent][scene]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	Orkige::registerOrkigeTestComponents();
	Orkige::GameObjectManager & manager = env.gameObjectManager;
	manager.clear();
	TempScene sceneB("orkige_test_persistent_sceneB.oscene");

	// author "scene B": it carries an object with the SAME id "Hero" the
	// survivor will own, plus a fresh object. Hero-in-B has health 99.
	{
		optr<Orkige::GameObject> heroInB = manager.createGameObject("Hero").lock();
		REQUIRE(heroInB);
		REQUIRE(heroInB->addComponent<Orkige::TestHealthComponent>());
		heroInB->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(99);

		optr<Orkige::GameObject> fresh = manager.createGameObject("BravoOnly").lock();
		REQUIRE(fresh);
		REQUIRE(fresh->addComponent<Orkige::TestHealthComponent>());
		fresh->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(5);
	}
	REQUIRE(Orkige::SceneSerializer::saveScene(sceneB.path, manager));

	// now build the RUNNING world (scene A): a PERSISTENT Hero with its own
	// distinct health 42, and a non-persistent sibling that must not survive
	manager.clear();
	{
		optr<Orkige::GameObject> hero = manager.createGameObject("Hero").lock();
		REQUIRE(hero);
		hero->setPersistent(true);
		REQUIRE(hero->addComponent<Orkige::TestHealthComponent>());
		hero->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(42);

		optr<Orkige::GameObject> sibling = manager.createGameObject("AlphaOnly").lock();
		REQUIRE(sibling);
	}

	// the level-system switch: survivors kept, scene B loaded beside them
	REQUIRE(Orkige::SceneSerializer::loadScenePreservingPersistent(
		sceneB.path, manager));

	// the survivor won the id: Hero is still persistent and keeps ITS OWN
	// health (42), NOT scene B's 99 - the incoming duplicate was skipped
	REQUIRE(objectAlive(manager, "Hero"));
	optr<Orkige::GameObject> hero = manager.getGameObject("Hero").lock();
	CHECK(hero->isPersistent());
	CHECK(hero->getComponentPtr<Orkige::TestHealthComponent>()->getHealth() == 42);
	// scene A's non-persistent sibling is gone; scene B's fresh object loaded
	CHECK_FALSE(objectAlive(manager, "AlphaOnly"));
	CHECK(objectAlive(manager, "BravoOnly"));
	// no throwaway duplicate-skip object leaked into the world
	for (auto const& [id, object] : manager.getGameObjects())
	{
		CHECK(id.find("dupskip") == Orkige::String::npos);
	}

	manager.clear();
}
