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

using Orkige::optr;
using Orkige::woptr;

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

TEST_CASE("A prefab-provided child property override round-trips and re-applies "
	"over the prefab default", "[prefab]")
{
	Orkige::GameObjectManager & manager = freshPrefabWorld();
	TempFile prefab("orkige_test_tile_override.oprefab");
	TempFile scene("orkige_test_prefab_override_scene.oscene");

	buildTilePrototype(manager);
	REQUIRE(Orkige::PrefabSerializer::savePrefab(prefab.path, manager,
		"TileProto"));
	manager.clear();

	// instantiate and OVERRIDE a single provided child's component (the tile
	// whose one wall carries a different value); the other children stay
	// pristine so the diff must store NOTHING for them
	REQUIRE(Orkige::PrefabSerializer::instantiatePrefab(prefab.path, manager,
		"Tile1", Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_OK);
	optr<Orkige::GameObject> root = manager.getGameObject("Tile1").lock();
	REQUIRE(root);
	root->setPrefabRef(prefab.fileName, "");
	REQUIRE(healthOf(manager, "Tile1/A") == 1);		// the prefab default
	manager.getGameObject("Tile1/A").lock()
		->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(99);

	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));

	// the save-time diff records the modified child ONLY (unmodified B / B_Sub
	// store nothing) - the honest per-component override under opaque state
	Orkige::GameObject::ChildOverrideMap const & saved =
		root->getPrefabChildOverrides();
	CHECK(saved.size() == 1);
	REQUIRE(saved.count("A") == 1);
	CHECK(saved.count("B") == 0);
	CHECK(saved.count("B_Sub") == 0);

	manager.clear();
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));

	// the override re-applies OVER the freshly instantiated prefab default,
	// the untouched children keep the prefab values
	CHECK(healthOf(manager, "Tile1/A") == 99);		// the child override
	CHECK(healthOf(manager, "Tile1/B") == 2);		// the prefab default
	CHECK(healthOf(manager, "Tile1/B_Sub") == 3);	// the prefab default
	CHECK(healthOf(manager, "Tile1") == 10);		// no root override here

	// a second round-trip stays stable (the deterministic remap + baseline
	// diff re-match; the override neither vanishes nor multiplies)
	optr<Orkige::GameObject> loadedRoot = manager.getGameObject("Tile1").lock();
	REQUIRE(loadedRoot);
	CHECK(loadedRoot->getPrefabChildOverrides().size() == 1);
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));
	CHECK(loadedRoot->getPrefabChildOverrides().size() == 1);
	manager.clear();
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	CHECK(healthOf(manager, "Tile1/A") == 99);
	CHECK(healthOf(manager, "Tile1/B") == 2);

	// reverting the child back to the prefab default drops the override (the
	// diff is honest both ways - no phantom override lingers)
	manager.getGameObject("Tile1/A").lock()
		->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(1);
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));
	CHECK(manager.getGameObject("Tile1").lock()
		->getPrefabChildOverrides().empty());
	manager.clear();
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	CHECK(healthOf(manager, "Tile1/A") == 1);

	manager.clear();
}

TEST_CASE("A prefab child override stores only the CHANGED reflected properties",
	"[prefab]")
{
	// a per-PROPERTY override. Overriding ONE field of a
	// multi-property reflected child must store ONLY that field (not the whole
	// component block), re-apply it over the prefab default, and leave every
	// unchanged field storing nothing.
	Orkige::GameObjectManager & manager = freshPrefabWorld();
	TempFile prefab("orkige_test_prop_override.oprefab");
	TempFile scene("orkige_test_prop_override_scene.oscene");

	// a prototype whose one child carries a multi-property reflected component
	// with authored, NON-default values (so the prefab default is meaningful)
	{
		optr<Orkige::GameObject> root = manager.createGameObject("Proto").lock();
		REQUIRE(root);
		REQUIRE(root->addComponent<Orkige::TestHealthComponent>());
		optr<Orkige::GameObject> child = manager.createGameObject("Proto_Body").lock();
		REQUIRE(child);
		REQUIRE(child->addComponent<Orkige::TestTweenTargetComponent>());
		REQUIRE(child->setParent("Proto", false));
		Orkige::TestTweenTargetComponent * body =
			child->getComponentPtr<Orkige::TestTweenTargetComponent>();
		body->setScalar(2.0f);
		Orkige::PropVec3 offset; offset.x = 1.0f; offset.y = 2.0f; offset.z = 3.0f;
		body->setOffset(offset);
	}
	REQUIRE(Orkige::PrefabSerializer::savePrefab(prefab.path, manager, "Proto"));
	manager.clear();

	REQUIRE(Orkige::PrefabSerializer::instantiatePrefab(prefab.path, manager,
		"Inst", Orkige::StringVector()) == Orkige::PrefabSerializer::INSTANTIATE_OK);
	optr<Orkige::GameObject> root = manager.getGameObject("Inst").lock();
	REQUIRE(root);
	root->setPrefabRef(prefab.fileName, "");
	Orkige::TestTweenTargetComponent * body =
		manager.getGameObject("Inst/Body").lock()
			->getComponentPtr<Orkige::TestTweenTargetComponent>();
	REQUIRE(body);
	// the prefab default rode through reflection-driven save/load
	CHECK(body->getScalar() == 1.0f * 2.0f);
	CHECK(body->getOffset().y == 2.0f);
	// override EXACTLY ONE property (scalar); leave offset/color/name pristine
	body->setScalar(9.0f);

	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));

	// only "scalar" was stored for the child's component - not the whole block
	Orkige::GameObject::ChildOverrideMap const & saved =
		root->getPrefabChildOverrides();
	REQUIRE(saved.count("Body") == 1);
	Orkige::GameObject::ComponentStateMap const & bodyOverride = saved.at("Body");
	REQUIRE(bodyOverride.count("TestTweenTargetComponent") == 1);
	Orkige::GameObject::ComponentPropertyMap const & props =
		bodyOverride.at("TestTweenTargetComponent");
	CHECK(props.size() == 1);					// ONLY the changed property
	CHECK(props.count("scalar") == 1);
	CHECK(props.count("offset") == 0);			// unchanged -> stores nothing
	CHECK(props.count("color") == 0);
	CHECK(props.count("name") == 0);

	// the override re-applies over the prefab default; unchanged fields keep it
	manager.clear();
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	body = manager.getGameObject("Inst/Body").lock()
		->getComponentPtr<Orkige::TestTweenTargetComponent>();
	REQUIRE(body);
	CHECK(body->getScalar() == 9.0f);			// the override
	CHECK(body->getOffset().x == 1.0f);			// the prefab default, untouched
	CHECK(body->getOffset().z == 3.0f);

	// reverting scalar to the prefab default drops the override entirely
	body->setScalar(2.0f);
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));
	CHECK(manager.getGameObject("Inst").lock()
		->getPrefabChildOverrides().empty());

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

TEST_CASE("Pre-cutover (legacy positional) scene versions are rejected", "[prefab]")
{
	// the scene format is a CLEAN CUTOVER (no back-compat): the positional readers and
	// the per-version field gates were removed, so scene files written in an
	// older format version no longer load - only the single current version does.
	Orkige::GameObjectManager & manager = freshPrefabWorld();

	SECTION("a version-4 prefab-instance scene")
	{
		TempFile scene("orkige_test_v4_prefab_scene.oscene");
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(scene.path));
		ar << Orkige::SceneSerializer::SCENE_FORMAT_MAGIC;
		int version = 4;
		ar << version;
		unsigned int objectCount = 1;
		ar << objectCount;
		Orkige::String id = "Tile1";
		ar << id;
		Orkige::String parentId = "";
		ar << parentId;
		bool activeSelf = true;
		ar << activeSelf;
		unsigned int tagCount = 0;
		ar << tagCount;
		ar->writeAttributed("some.oprefab",
			Orkige::AssetDatabase::REFERENCE_ID_ATTRIBUTE, "");
		unsigned int suppressedCount = 0;
		ar << suppressedCount;
		unsigned int componentCount = 0;
		ar << componentCount;
		REQUIRE(ar->stopWriting());

		REQUIRE_FALSE(Orkige::SceneSerializer::loadScene(scene.path, manager));
		CHECK_FALSE(manager.objectExists("Tile1"));
	}

	SECTION("a version-2 hierarchy scene")
	{
		TempFile scene("orkige_test_v2_scene.oscene");
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(scene.path));
		ar << Orkige::SceneSerializer::SCENE_FORMAT_MAGIC;
		int version = 2;
		ar << version;
		unsigned int objectCount = 1;
		ar << objectCount;
		Orkige::String id = "OldRoot";
		ar << id;
		Orkige::String parentId = "";
		ar << parentId;
		bool activeSelf = false;
		ar << activeSelf;
		unsigned int componentCount = 0;
		ar << componentCount;
		REQUIRE(ar->stopWriting());

		REQUIRE_FALSE(Orkige::SceneSerializer::loadScene(scene.path, manager));
		CHECK_FALSE(manager.objectExists("OldRoot"));
	}

	manager.clear();
}
