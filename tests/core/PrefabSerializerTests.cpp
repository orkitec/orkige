/**************************************************************
	created:	2026/07/09 at 12:30
	filename: 	PrefabSerializerTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless prefab round-trips with core-only components: subtree save
	(.oprefab), deterministic instance id remapping, the scene-side
	instance state (prefabRef + suppressed children + root overrides +
	extra children), the missing-prefab placeholder policy and the
	nested-prefab refusals. The engine-level (rendered) prefab flow is
	covered by the roller project's player selfcheck.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"
#include "TestComponents.h"

#include <core_game/PrefabSerializer.h>
#include <core_game/SceneSerializer.h>
#include <core_project/AssetDatabase.h>
#include <core_serialization/XMLArchive.h>

#include <algorithm>
#include <filesystem>
#include <unistd.h> // getpid - unique temp fixture names (parallel ctest!)

namespace
{
	//! PID-suffixed temp file: every TEST_CASE runs as its own ctest process
	//! in parallel, so fixed names collide across cases sharing a file name
	//! (seen in the wild as a one-in-many flake of the round-trip case)
	struct TempFile
	{
		Orkige::String fileName;	//!< the suffixed name (for prefabRef)
		Orkige::String path;
		explicit TempFile(std::string const & name)
		{
			const std::filesystem::path base(name);
			this->fileName = base.stem().string() + "_" +
				std::to_string(::getpid()) + base.extension().string();
			this->path = (std::filesystem::temp_directory_path() /
				this->fileName).string();
			std::filesystem::remove(this->path);
		}
		~TempFile()
		{
			std::error_code ignored;
			std::filesystem::remove(this->path, ignored);
		}
	};

	//! shared headless world; prefab path resolution must not depend on
	//! whatever AssetDatabase another test case left active
	Orkige::GameObjectManager & freshPrefabWorld()
	{
		Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
		Orkige::registerOrkigeTestComponents();
		Orkige::AssetDatabase::setActive(optr<Orkige::AssetDatabase>());
		env.gameObjectManager.clear();
		return env.gameObjectManager;
	}

	//! GameObject with a TestHealthComponent at the given health
	optr<Orkige::GameObject> makeHealthObject(
		Orkige::GameObjectManager & manager, Orkige::String const & id,
		int health, Orkige::String const & parentId = "")
	{
		optr<Orkige::GameObject> gameObject =
			manager.createGameObject(id).lock();
		REQUIRE(gameObject);
		REQUIRE(gameObject->addComponent<Orkige::TestHealthComponent>());
		gameObject->getComponentPtr<Orkige::TestHealthComponent>()
			->setHealth(health);
		if(!parentId.empty())
		{
			REQUIRE(gameObject->setParent(parentId, false));
		}
		return gameObject;
	}

	int healthOf(Orkige::GameObjectManager & manager, Orkige::String const & id)
	{
		optr<Orkige::GameObject> gameObject = manager.getGameObject(id).lock();
		REQUIRE(gameObject);
		REQUIRE(gameObject->hasComponent<Orkige::TestHealthComponent>());
		return gameObject->getComponentPtr<Orkige::TestHealthComponent>()
			->getHealth();
	}

	//! the reference prefab every case builds on: root + two children, one
	//! grandchild, one deactivated child
	void buildTilePrototype(Orkige::GameObjectManager & manager)
	{
		makeHealthObject(manager, "TileProto", 10);
		makeHealthObject(manager, "TileProto_A", 1, "TileProto");
		makeHealthObject(manager, "TileProto_B", 2, "TileProto");
		makeHealthObject(manager, "TileProto_B_Sub", 3, "TileProto_B");
		manager.getGameObject("TileProto_A").lock()->setActive(false);
	}
}

TEST_CASE("PrefabSerializer round-trips a subtree with deterministic instance ids",
	"[prefab]")
{
	Orkige::GameObjectManager & manager = freshPrefabWorld();
	TempFile prefab("orkige_test_tile.oprefab");

	buildTilePrototype(manager);
	REQUIRE(Orkige::PrefabSerializer::savePrefab(prefab.path, manager,
		"TileProto"));
	manager.clear();

	REQUIRE(Orkige::PrefabSerializer::instantiatePrefab(prefab.path, manager,
		"Inst1", Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_OK);

	// structure: deterministic "<instanceRoot>/<localId>" remap (locals are
	// the object ids with the root prefix stripped), parents rebuilt
	REQUIRE(manager.getGameObjects().size() == 4);
	optr<Orkige::GameObject> root = manager.getGameObject("Inst1").lock();
	REQUIRE(root);
	CHECK(root->getParentId().empty());
	optr<Orkige::GameObject> childA = manager.getGameObject("Inst1/A").lock();
	optr<Orkige::GameObject> childB = manager.getGameObject("Inst1/B").lock();
	optr<Orkige::GameObject> grandchild =
		manager.getGameObject("Inst1/B_Sub").lock();
	REQUIRE(childA);
	REQUIRE(childB);
	REQUIRE(grandchild);
	CHECK(childA->getParentId() == "Inst1");
	CHECK(childB->getParentId() == "Inst1");
	CHECK(grandchild->getParentId() == "Inst1/B");

	// component state and the per-object active flags
	CHECK(healthOf(manager, "Inst1") == 10);
	CHECK(healthOf(manager, "Inst1/A") == 1);
	CHECK(healthOf(manager, "Inst1/B") == 2);
	CHECK(healthOf(manager, "Inst1/B_Sub") == 3);
	CHECK_FALSE(childA->isActiveSelf());
	CHECK_FALSE(childA->isActiveInHierarchy());
	CHECK(childB->isActiveSelf());

	// prefab-provided vs extra children: the instance root's mark plus the
	// "<root>/" id namespace decide
	root->setPrefabRef(prefab.fileName, "");
	CHECK(Orkige::PrefabSerializer::isPrefabProvided(manager, *childA));
	CHECK(Orkige::PrefabSerializer::isPrefabProvided(manager, *grandchild));
	CHECK_FALSE(Orkige::PrefabSerializer::isPrefabProvided(manager, *root));
	makeHealthObject(manager, "Extra", 7, "Inst1");
	CHECK_FALSE(Orkige::PrefabSerializer::isPrefabProvided(manager,
		*manager.getGameObject("Extra").lock()));

	// the RE-MAKE loop: saving the live instance again derives the SAME
	// locals (root's own prefabRef is allowed and ignored), so a second
	// instance matches structurally
	TempFile remade("orkige_test_tile_remade.oprefab");
	REQUIRE(Orkige::PrefabSerializer::savePrefab(remade.path, manager,
		"Inst1"));
	REQUIRE(Orkige::PrefabSerializer::instantiatePrefab(remade.path, manager,
		"Inst2", Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_OK);
	CHECK(manager.objectExists("Inst2/A"));
	CHECK(manager.objectExists("Inst2/B"));
	CHECK(manager.objectExists("Inst2/B_Sub"));
	CHECK(manager.objectExists("Inst2/Extra"));	// extras ride into a re-make

	manager.clear();
}

TEST_CASE("Scene round-trips a prefab instance with structural + root overrides",
	"[prefab]")
{
	Orkige::GameObjectManager & manager = freshPrefabWorld();
	TempFile prefab("orkige_test_tile.oprefab");
	TempFile scene("orkige_test_prefab_scene.oscene");

	buildTilePrototype(manager);
	REQUIRE(Orkige::PrefabSerializer::savePrefab(prefab.path, manager,
		"TileProto"));
	manager.clear();

	// author an instance: suppressed child "B" (subtree-deep), a root
	// override (health) and a scene-side extra child
	REQUIRE(Orkige::PrefabSerializer::instantiatePrefab(prefab.path, manager,
		"Tile1", Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_OK);
	optr<Orkige::GameObject> root = manager.getGameObject("Tile1").lock();
	REQUIRE(root);
	// prefabRef is the bare file name: the loader resolves it relative to
	// the scene file's directory (both live in the temp directory)
	root->setPrefabRef(prefab.fileName, "");
	Orkige::StringVector suppressed;
	suppressed.push_back("B");
	root->setSuppressedPrefabChildren(suppressed);
	{
		const Orkige::StringVector doomed = manager.collectSubtreeIds("Tile1/B");
		for(auto it = doomed.rbegin(); it != doomed.rend(); ++it)
		{
			REQUIRE(manager.delGameObject(*it));
		}
	}
	root->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(55);
	makeHealthObject(manager, "Goal", 77, "Tile1");

	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));
	manager.clear();
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));

	// prefab content restored minus the suppressed subtree, plus the extra
	// child, with the root's serialized state overlaying the defaults
	CHECK(manager.getGameObjects().size() == 3);	// Tile1, Tile1/A, Goal
	REQUIRE(manager.objectExists("Tile1"));
	REQUIRE(manager.objectExists("Tile1/A"));
	CHECK_FALSE(manager.objectExists("Tile1/B"));
	CHECK_FALSE(manager.objectExists("Tile1/B_Sub"));
	REQUIRE(manager.objectExists("Goal"));
	CHECK(healthOf(manager, "Tile1") == 55);		// the root override
	CHECK(healthOf(manager, "Tile1/A") == 1);		// the prefab default
	CHECK(healthOf(manager, "Goal") == 77);
	optr<Orkige::GameObject> loadedRoot = manager.getGameObject("Tile1").lock();
	CHECK(loadedRoot->getPrefabRef() == prefab.fileName);
	REQUIRE(loadedRoot->getSuppressedPrefabChildren().size() == 1);
	CHECK(loadedRoot->getSuppressedPrefabChildren()[0] == "B");
	CHECK(manager.getGameObject("Goal").lock()->getParentId() == "Tile1");
	CHECK(manager.getGameObject("Tile1/A").lock()->getParentId() == "Tile1");
	CHECK_FALSE(manager.getGameObject("Tile1/A").lock()->isActiveSelf());

	// a SECOND round-trip must be stable (the deterministic remap re-matches)
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));
	manager.clear();
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	CHECK(manager.getGameObjects().size() == 3);
	CHECK(healthOf(manager, "Tile1") == 55);
	CHECK_FALSE(manager.objectExists("Tile1/B"));

	manager.clear();
}

TEST_CASE("A missing prefab loads as a placeholder root keeping its reference",
	"[prefab]")
{
	Orkige::GameObjectManager & manager = freshPrefabWorld();
	TempFile scene("orkige_test_missing_prefab.oscene");

	optr<Orkige::GameObject> root = makeHealthObject(manager, "Ghost", 42);
	root->setPrefabRef("orkige_no_such_prefab.oprefab", "feedbead");
	Orkige::StringVector suppressed;
	suppressed.push_back("WallLeft");
	root->setSuppressedPrefabChildren(suppressed);
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));
	manager.clear();

	// the scene still loads (loud Console error, no children) and the
	// placeholder RETAINS reference + overrides so a re-save loses nothing
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	REQUIRE(manager.getGameObjects().size() == 1);
	optr<Orkige::GameObject> placeholder = manager.getGameObject("Ghost").lock();
	REQUIRE(placeholder);
	CHECK(placeholder->getPrefabRef() == "orkige_no_such_prefab.oprefab");
	CHECK(placeholder->getPrefabAssetId() == "feedbead");
	REQUIRE(placeholder->getSuppressedPrefabChildren().size() == 1);
	CHECK(placeholder->getSuppressedPrefabChildren()[0] == "WallLeft");
	CHECK(healthOf(manager, "Ghost") == 42);

	// and the re-saved scene round-trips the placeholder unchanged
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));
	manager.clear();
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	CHECK(manager.getGameObject("Ghost").lock()->getPrefabRef() ==
		"orkige_no_such_prefab.oprefab");

	manager.clear();
}

TEST_CASE("Nested prefabs are refused on save and hard-error on load",
	"[prefab]")
{
	Orkige::GameObjectManager & manager = freshPrefabWorld();
	TempFile prefab("orkige_test_nested.oprefab");
	TempFile scene("orkige_test_nested_scene.oscene");

	// save refusal: an instance root BELOW the saved root
	makeHealthObject(manager, "Outer", 1);
	optr<Orkige::GameObject> inner = makeHealthObject(manager, "Inner", 2, "Outer");
	inner->setPrefabRef("some.oprefab", "");
	CHECK_FALSE(Orkige::PrefabSerializer::savePrefab(prefab.path, manager,
		"Outer"));
	// ...but the saved root's OWN prefabRef is allowed (the re-make loop)
	inner->setPrefabRef("", "");
	manager.getGameObject("Outer").lock()->setPrefabRef("some.oprefab", "");
	CHECK(Orkige::PrefabSerializer::savePrefab(prefab.path, manager, "Outer"));
	manager.clear();
	std::filesystem::remove(prefab.path);

	// load hard error: a hand-crafted prefab file carrying a prefabRef
	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(prefab.path));
		ar << Orkige::PrefabSerializer::PREFAB_FORMAT_MAGIC;
		int version = Orkige::PrefabSerializer::PREFAB_FORMAT_VERSION;
		ar << version;
		Orkige::String rootLocalId = "Root";
		ar << rootLocalId;
		unsigned int objectCount = 1;
		ar << objectCount;
		ar << rootLocalId;
		Orkige::String parentLocalId = "";
		ar << parentLocalId;
		bool activeSelf = true;
		ar << activeSelf;
		ar->writeAttributed("nested.oprefab",
			Orkige::AssetDatabase::REFERENCE_ID_ATTRIBUTE, "");
		unsigned int componentCount = 0;
		ar << componentCount;
		REQUIRE(ar->stopWriting());
	}
	CHECK(Orkige::PrefabSerializer::instantiatePrefab(prefab.path, manager,
		"Inst", Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_ERROR);

	// a scene referencing the poisoned prefab fails hard and leaves no
	// half-loaded world behind
	{
		optr<Orkige::GameObject> root = makeHealthObject(manager, "Bad", 1);
		root->setPrefabRef(prefab.fileName, "");
		REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));
		manager.clear();
	}
	CHECK_FALSE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	CHECK(manager.getGameObjects().empty());

	// a plainly MISSING file is not an error (the placeholder policy)
	CHECK(Orkige::PrefabSerializer::instantiatePrefab(
		(std::filesystem::temp_directory_path() /
			"orkige_no_such.oprefab").string(), manager, "Inst",
		Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_FILE_MISSING);

	manager.clear();
}

TEST_CASE("Legacy version 2 scenes load with no prefab state", "[prefab]")
{
	Orkige::GameObjectManager & manager = freshPrefabWorld();
	TempFile scene("orkige_test_v2_scene.oscene");

	// hand-craft the exact v2 per-object block (id, parent, activeSelf,
	// components - no prefab fields)
	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(scene.path));
		ar << Orkige::SceneSerializer::SCENE_FORMAT_MAGIC;
		int version = 2;
		ar << version;
		unsigned int objectCount = 2;
		ar << objectCount;
		Orkige::String id = "OldChild";
		ar << id;
		Orkige::String parentId = "OldRoot";
		ar << parentId;
		bool activeSelf = true;
		ar << activeSelf;
		unsigned int componentCount = 0;
		ar << componentCount;
		id = "OldRoot";
		ar << id;
		parentId = "";
		ar << parentId;
		activeSelf = false;
		ar << activeSelf;
		ar << componentCount;
		REQUIRE(ar->stopWriting());
	}

	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	REQUIRE(manager.getGameObjects().size() == 2);
	optr<Orkige::GameObject> root = manager.getGameObject("OldRoot").lock();
	REQUIRE(root);
	CHECK(root->getPrefabRef().empty());
	CHECK(root->getSuppressedPrefabChildren().empty());
	CHECK_FALSE(root->isActiveSelf());
	CHECK(manager.getGameObject("OldChild").lock()->getParentId() == "OldRoot");

	manager.clear();
}
