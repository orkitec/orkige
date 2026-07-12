/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	PrefabOverrideMergeTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The prefab override-MERGE rules, pinned headlessly against the live
	serializers. A scene snapshot taken BEFORE a .oprefab changes must,
	on reload, rebuild every instance from the NEW prefab file and then
	re-apply the stored per-property overrides - that merge is what makes
	"edit the prefab asset, instances refresh with their overrides
	intact" work without any dedicated refresh code. Rules covered:
	1. a property the instance never overrode takes the NEW prefab value
	2. an overridden property keeps the INSTANCE value (even when the
	   prefab default for it changed too)
	3. an override targeting a child the prefab dropped is kept for a
	   later heal (and pruned naturally by the next scene save)
	4. an override targeting a component the prefab child does not carry
	   re-adds the component (editor-added component semantics)
	5. an override property no longer in the component schema is ignored
	6. a stale suppression (a local id the prefab no longer provides) is
	   a harmless no-op and stays recorded
	Plus the edit-stage round-trip: instantiate -> mutate -> savePrefab
	-> re-instantiate reproduces identical locals and values.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"
#include "TestComponents.h"

#include <core_game/PrefabSerializer.h>
#include <core_game/SceneSerializer.h>
#include <core_project/AssetDatabase.h>

#include <algorithm>
#include <filesystem>
#ifdef _WIN32
#include <process.h>	// _getpid - unique temp fixture names (parallel ctest)
#define getpid _getpid
#else
#include <unistd.h> // getpid - unique temp fixture names (parallel ctest!)
#endif

using Orkige::optr;
using Orkige::woptr;

namespace
{
	//! PID-suffixed temp file (every TEST_CASE runs as its own parallel ctest
	//! process - fixed names collide; the PrefabSerializerTests precedent)
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
	Orkige::GameObjectManager & freshMergeWorld()
	{
		Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
		Orkige::registerOrkigeTestComponents();
		Orkige::AssetDatabase::setActive(optr<Orkige::AssetDatabase>());
		env.gameObjectManager.clear();
		return env.gameObjectManager;
	}

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

	//! (re)write the prefab file from a freshly built prototype: root +
	//! child "A" (health aHealth) + optional child "B" (health bHealth).
	//! Clears the manager before AND after - the caller's scene world must
	//! not leak into the prefab and vice versa.
	void writeHealthPrefab(Orkige::GameObjectManager & manager,
		Orkige::String const & path, int rootHealth, int aHealth,
		bool withB, int bHealth)
	{
		manager.clear();
		makeHealthObject(manager, "Proto", rootHealth);
		makeHealthObject(manager, "Proto_A", aHealth, "Proto");
		if(withB)
		{
			makeHealthObject(manager, "Proto_B", bHealth, "Proto");
		}
		REQUIRE(Orkige::PrefabSerializer::savePrefab(path, manager, "Proto"));
		manager.clear();
	}
}

TEST_CASE("Prefab edits propagate to instances; overrides win over new defaults",
	"[prefab][merge]")
{
	// rules 1 + 2: the scene snapshot predates the prefab edit; reloading it
	// rebuilds the instance from the NEW file and re-applies the override
	Orkige::GameObjectManager & manager = freshMergeWorld();
	TempFile prefab("orkige_merge_tile.oprefab");
	TempFile scene("orkige_merge_scene.oscene");

	writeHealthPrefab(manager, prefab.path, 10, 1, true, 2);

	// author the instance: child A carries a per-instance override, B stays
	// pristine (stores nothing)
	REQUIRE(Orkige::PrefabSerializer::instantiatePrefab(prefab.path, manager,
		"Inst", Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_OK);
	manager.getGameObject("Inst").lock()->setPrefabRef(prefab.fileName, "");
	manager.getGameObject("Inst/A").lock()
		->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(99);
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));

	// the prefab changes AFTER the snapshot: new defaults on BOTH the
	// overridden property (A) and the non-overridden one (B)
	writeHealthPrefab(manager, prefab.path, 10, 5, true, 7);

	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	CHECK(healthOf(manager, "Inst/A") == 99);	// override wins (rule 2)
	CHECK(healthOf(manager, "Inst/B") == 7);	// new prefab value wins (rule 1)

	manager.clear();
}

TEST_CASE("The merge is per PROPERTY: unchanged fields of an overridden "
	"component take the new prefab defaults", "[prefab][merge]")
{
	// rules 1 + 2 within ONE component: only the overridden named field keeps
	// the instance value; a sibling field of the same component follows the
	// new prefab default
	Orkige::GameObjectManager & manager = freshMergeWorld();
	TempFile prefab("orkige_merge_body.oprefab");
	TempFile scene("orkige_merge_body_scene.oscene");

	auto writeBodyPrefab = [&manager, &prefab](float scalar, float offsetY)
	{
		manager.clear();
		optr<Orkige::GameObject> root = manager.createGameObject("Proto").lock();
		REQUIRE(root);
		REQUIRE(root->addComponent<Orkige::TestHealthComponent>());
		optr<Orkige::GameObject> child =
			manager.createGameObject("Proto_Body").lock();
		REQUIRE(child);
		REQUIRE(child->addComponent<Orkige::TestTweenTargetComponent>());
		REQUIRE(child->setParent("Proto", false));
		Orkige::TestTweenTargetComponent * body =
			child->getComponentPtr<Orkige::TestTweenTargetComponent>();
		body->setScalar(scalar);
		Orkige::PropVec3 offset;
		offset.y = offsetY;
		body->setOffset(offset);
		REQUIRE(Orkige::PrefabSerializer::savePrefab(prefab.path, manager,
			"Proto"));
		manager.clear();
	};

	writeBodyPrefab(2.0f, 20.0f);

	REQUIRE(Orkige::PrefabSerializer::instantiatePrefab(prefab.path, manager,
		"Inst", Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_OK);
	manager.getGameObject("Inst").lock()->setPrefabRef(prefab.fileName, "");
	// override EXACTLY ONE field (scalar); offset stays the prefab default
	manager.getGameObject("Inst/Body").lock()
		->getComponentPtr<Orkige::TestTweenTargetComponent>()->setScalar(9.0f);
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));

	// the prefab edit changes BOTH fields' defaults after the snapshot
	writeBodyPrefab(3.0f, 30.0f);

	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	Orkige::TestTweenTargetComponent * body =
		manager.getGameObject("Inst/Body").lock()
			->getComponentPtr<Orkige::TestTweenTargetComponent>();
	REQUIRE(body);
	CHECK(body->getScalar() == 9.0f);		// the overridden field: instance wins
	CHECK(body->getOffset().y == 30.0f);	// the untouched field: new default

	manager.clear();
}

TEST_CASE("An override for a child the prefab dropped is kept and heals when "
	"the child returns", "[prefab][merge]")
{
	// rule 3: the override survives the child's absence (logged, kept in the
	// instance map), re-applies once the prefab provides the child again, and
	// a scene re-save while the child is absent prunes it naturally
	Orkige::GameObjectManager & manager = freshMergeWorld();
	TempFile prefab("orkige_merge_heal.oprefab");
	TempFile scene("orkige_merge_heal_scene.oscene");

	writeHealthPrefab(manager, prefab.path, 10, 1, true, 2);
	// keep the with-B file for the heal leg (pure file copy, no live world)
	const std::string backupPath = prefab.path + ".bak";
	std::filesystem::copy_file(prefab.path, backupPath,
		std::filesystem::copy_options::overwrite_existing);

	REQUIRE(Orkige::PrefabSerializer::instantiatePrefab(prefab.path, manager,
		"Inst", Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_OK);
	manager.getGameObject("Inst").lock()->setPrefabRef(prefab.fileName, "");
	manager.getGameObject("Inst/B").lock()
		->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(42);
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));

	// the prefab DROPS child B after the snapshot
	writeHealthPrefab(manager, prefab.path, 10, 1, false, 0);

	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	CHECK_FALSE(manager.objectExists("Inst/B"));	// the prefab owns structure
	optr<Orkige::GameObject> root = manager.getGameObject("Inst").lock();
	REQUIRE(root);
	// the orphaned override is KEPT for a later heal, not dropped
	CHECK(root->getPrefabChildOverrides().count("B") == 1);

	// the heal: the prefab provides B again -> the same scene file re-applies
	// the stored override over the returned child
	std::filesystem::copy_file(backupPath, prefab.path,
		std::filesystem::copy_options::overwrite_existing);
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	REQUIRE(manager.objectExists("Inst/B"));
	CHECK(healthOf(manager, "Inst/B") == 42);

	// the natural prune: re-saving the scene while B is absent diffs only the
	// LIVE children, so the orphaned override leaves the instance map
	writeHealthPrefab(manager, prefab.path, 10, 1, false, 0);
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));
	CHECK(manager.getGameObject("Inst").lock()
		->getPrefabChildOverrides().count("B") == 0);

	std::error_code ignored;
	std::filesystem::remove(backupPath, ignored);
	manager.clear();
}

TEST_CASE("An override for a component the prefab child lacks re-adds the "
	"component", "[prefab][merge]")
{
	// rule 4: an editor-added component on a prefab child serializes as a
	// full-component override and is re-added (with its state) on reload -
	// the freshly instantiated child does not carry it
	Orkige::GameObjectManager & manager = freshMergeWorld();
	TempFile prefab("orkige_merge_addcomp.oprefab");
	TempFile scene("orkige_merge_addcomp_scene.oscene");

	writeHealthPrefab(manager, prefab.path, 10, 1, false, 0);

	REQUIRE(Orkige::PrefabSerializer::instantiatePrefab(prefab.path, manager,
		"Inst", Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_OK);
	manager.getGameObject("Inst").lock()->setPrefabRef(prefab.fileName, "");
	optr<Orkige::GameObject> child = manager.getGameObject("Inst/A").lock();
	REQUIRE(child);
	REQUIRE(child->addComponent<Orkige::TestTweenTargetComponent>());
	child->getComponentPtr<Orkige::TestTweenTargetComponent>()->setScalar(3.0f);
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));

	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	child = manager.getGameObject("Inst/A").lock();
	REQUIRE(child);
	REQUIRE(child->hasComponent<Orkige::TestTweenTargetComponent>());
	CHECK(child->getComponentPtr<Orkige::TestTweenTargetComponent>()
		->getScalar() == 3.0f);
	CHECK(healthOf(manager, "Inst/A") == 1);	// prefab-provided state intact

	manager.clear();
}

TEST_CASE("An override property gone from the component schema is ignored",
	"[prefab][merge]")
{
	// rule 5: the named-field contract - a stored override record whose
	// property name no longer exists in the schema is skipped silently; the
	// records that still resolve apply normally
	Orkige::GameObjectManager & manager = freshMergeWorld();
	TempFile prefab("orkige_merge_ghostprop.oprefab");
	TempFile scene("orkige_merge_ghostprop_scene.oscene");

	// prototype: child "Body" carries the multi-property reflected component
	manager.clear();
	{
		optr<Orkige::GameObject> root = manager.createGameObject("Proto").lock();
		REQUIRE(root);
		REQUIRE(root->addComponent<Orkige::TestHealthComponent>());
		optr<Orkige::GameObject> child =
			manager.createGameObject("Proto_Body").lock();
		REQUIRE(child);
		REQUIRE(child->addComponent<Orkige::TestTweenTargetComponent>());
		REQUIRE(child->setParent("Proto", false));
	}
	REQUIRE(Orkige::PrefabSerializer::savePrefab(prefab.path, manager, "Proto"));
	const std::string backupPath = prefab.path + ".bak";
	std::filesystem::copy_file(prefab.path, backupPath,
		std::filesystem::copy_options::overwrite_existing);
	manager.clear();

	REQUIRE(Orkige::PrefabSerializer::instantiatePrefab(prefab.path, manager,
		"Inst", Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_OK);
	optr<Orkige::GameObject> root = manager.getGameObject("Inst").lock();
	REQUIRE(root);
	root->setPrefabRef(prefab.fileName, "");

	// hand-build an override map carrying a record for a property name the
	// schema does not declare, next to a valid one. Writing it to the scene
	// file rides the placeholder branch (prefab file temporarily absent), so
	// the save keeps the map verbatim instead of re-diffing live children -
	// exactly the on-disk shape a schema that later dropped a field leaves.
	{
		Orkige::GameObject::ComponentPropertyMap properties;
		Orkige::GameObject::ComponentPropertyRecord scalarRecord;
		scalarRecord.kind = static_cast<int>(Orkige::PropertyKind::Float);
		scalarRecord.value = "9";
		properties["scalar"] = scalarRecord;
		Orkige::GameObject::ComponentPropertyRecord ghostRecord;
		ghostRecord.kind = static_cast<int>(Orkige::PropertyKind::Float);
		ghostRecord.value = "1";
		properties["vanishedProp"] = ghostRecord;
		Orkige::GameObject::ComponentStateMap components;
		components["TestTweenTargetComponent"] = properties;
		Orkige::GameObject::ChildOverrideMap overrides;
		overrides["Body"] = components;
		root->setPrefabChildOverrides(overrides);
	}
	std::filesystem::remove(prefab.path);
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));
	std::filesystem::copy_file(backupPath, prefab.path,
		std::filesystem::copy_options::overwrite_existing);

	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	Orkige::TestTweenTargetComponent * body =
		manager.getGameObject("Inst/Body").lock()
			->getComponentPtr<Orkige::TestTweenTargetComponent>();
	REQUIRE(body);
	CHECK(body->getScalar() == 9.0f);	// the resolving record applied
	// "vanishedProp" was skipped without failing the load - nothing to read
	// back; the load succeeding with the sibling record applied IS the pin

	std::error_code ignored;
	std::filesystem::remove(backupPath, ignored);
	manager.clear();
}

TEST_CASE("A stale suppression is a harmless no-op and stays recorded",
	"[prefab][merge]")
{
	// rule 6: a suppressed local id the prefab no longer (or never) provides
	// matches nothing at instantiate; the world loads normally and the entry
	// is not actively pruned
	Orkige::GameObjectManager & manager = freshMergeWorld();
	TempFile prefab("orkige_merge_stale.oprefab");
	TempFile scene("orkige_merge_stale_scene.oscene");

	writeHealthPrefab(manager, prefab.path, 10, 1, true, 2);

	REQUIRE(Orkige::PrefabSerializer::instantiatePrefab(prefab.path, manager,
		"Inst", Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_OK);
	optr<Orkige::GameObject> root = manager.getGameObject("Inst").lock();
	REQUIRE(root);
	root->setPrefabRef(prefab.fileName, "");
	Orkige::StringVector suppressed;
	suppressed.push_back("Zombie");		// never existed in the prefab
	root->setSuppressedPrefabChildren(suppressed);
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));

	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	CHECK(manager.objectExists("Inst/A"));
	CHECK(manager.objectExists("Inst/B"));
	root = manager.getGameObject("Inst").lock();
	REQUIRE(root);
	REQUIRE(root->getSuppressedPrefabChildren().size() == 1);
	CHECK(root->getSuppressedPrefabChildren()[0] == "Zombie");

	manager.clear();
}

TEST_CASE("The prefab edit stage round-trips: instantiate, mutate, re-save, "
	"re-instantiate", "[prefab][merge]")
{
	// the isolation-stage loop: a prefab instantiated as an editable subtree,
	// mutated (a child value + a NEW child under the root) and saved back
	// reproduces itself - deterministic locals, values and the new child
	Orkige::GameObjectManager & manager = freshMergeWorld();
	TempFile prefab("orkige_merge_stage.oprefab");

	writeHealthPrefab(manager, prefab.path, 10, 1, true, 2);

	// the stage: root id = the edit context id (a file stem in the editor)
	REQUIRE(Orkige::PrefabSerializer::instantiatePrefab(prefab.path, manager,
		"stage", Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_OK);
	manager.getGameObject("stage/A").lock()
		->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(55);
	makeHealthObject(manager, "Gem", 8, "stage");
	REQUIRE(Orkige::PrefabSerializer::savePrefab(prefab.path, manager,
		"stage"));
	manager.clear();

	REQUIRE(Orkige::PrefabSerializer::instantiatePrefab(prefab.path, manager,
		"Inst", Orkige::StringVector()) ==
		Orkige::PrefabSerializer::INSTANTIATE_OK);
	REQUIRE(manager.getGameObjects().size() == 4);
	CHECK(healthOf(manager, "Inst") == 10);
	CHECK(healthOf(manager, "Inst/A") == 55);	// the stage edit propagated
	CHECK(healthOf(manager, "Inst/B") == 2);
	REQUIRE(manager.objectExists("Inst/Gem"));	// the added child rides along
	CHECK(healthOf(manager, "Inst/Gem") == 8);
	CHECK(manager.getGameObject("Inst/Gem").lock()->getParentId() == "Inst");

	manager.clear();
}
