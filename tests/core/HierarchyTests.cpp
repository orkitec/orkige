/**************************************************************
	created:	2026/07/09 at 10:00
	filename: 	HierarchyTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Unit tests for the GameObject parent/child tree (parenting, children
	queries, cycle guard) and the active state (activeSelf /
	activeInHierarchy propagation, onSetActive dispatch, update gating)
	plus the version-2 scene serialization of both.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"
#include "TestComponents.h"

#include <core_game/SceneSerializer.h>
#include <core_serialization/XMLArchive.h>

#include <algorithm>
#include <filesystem>

using Orkige::optr;
using Orkige::woptr;

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

	Orkige::GameObjectManager & bootHierarchyWorld()
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

TEST_CASE("GameObject parenting keeps the child index in sync", "[hierarchy]")
{
	Orkige::GameObjectManager & manager = bootHierarchyWorld();
	optr<Orkige::GameObject> root = manager.createGameObject("Root").lock();
	optr<Orkige::GameObject> childA = manager.createGameObject("ChildA").lock();
	optr<Orkige::GameObject> childB = manager.createGameObject("ChildB").lock();
	REQUIRE(root);
	REQUIRE(childA);
	REQUIRE(childB);

	// fresh objects are roots
	CHECK(root->getParentId().empty());
	CHECK(manager.getRootObjectIds().size() == 3);

	REQUIRE(childA->setParent("Root"));
	REQUIRE(childB->setParent("Root"));
	CHECK(childA->getParentId() == "Root");
	CHECK(childA->getParent().lock() == root);
	REQUIRE(manager.getChildren("Root").size() == 2);
	CHECK(containsId(manager.getChildren("Root"), "ChildA"));
	CHECK(containsId(manager.getChildren("Root"), "ChildB"));
	CHECK(manager.getRootObjectIds().size() == 1);

	// re-parent A under B
	REQUIRE(childA->setParent("ChildB"));
	CHECK(manager.getChildren("Root").size() == 1);
	REQUIRE(manager.getChildren("ChildB").size() == 1);
	CHECK(manager.getChildren("ChildB").front() == "ChildA");
	CHECK(manager.isDescendantOf("ChildA", "Root"));
	CHECK(manager.isDescendantOf("ChildA", "ChildB"));
	CHECK_FALSE(manager.isDescendantOf("ChildB", "ChildA"));

	// back to root
	REQUIRE(childA->setParent(""));
	CHECK(childA->getParentId().empty());
	CHECK(manager.getChildren("ChildB").empty());

	manager.clear();
}

TEST_CASE("reorderChild moves a sibling before/after an anchor and refuses "
	"cross-parent, unknown and self moves", "[hierarchy]")
{
	Orkige::GameObjectManager & manager = bootHierarchyWorld();
	manager.createGameObject("Root");
	manager.createGameObject("Elsewhere");
	optr<Orkige::GameObject> a = manager.createGameObject("A").lock();
	optr<Orkige::GameObject> b = manager.createGameObject("B").lock();
	optr<Orkige::GameObject> c = manager.createGameObject("C").lock();
	optr<Orkige::GameObject> outsider = manager.createGameObject("Outsider").lock();
	REQUIRE(a->setParent("Root"));
	REQUIRE(b->setParent("Root"));
	REQUIRE(c->setParent("Root"));
	REQUIRE(outsider->setParent("Elsewhere"));
	// registration order IS the child order
	REQUIRE(manager.getChildren("Root") == Orkige::StringVector({ "A", "B", "C" }));
	CHECK(manager.getChildIndex("B") == 1);

	// move the last one to the front
	REQUIRE(manager.reorderChild("C", "A", false));
	CHECK(manager.getChildren("Root") == Orkige::StringVector({ "C", "A", "B" }));
	// and back behind B
	REQUIRE(manager.reorderChild("C", "B", true));
	CHECK(manager.getChildren("Root") == Orkige::StringVector({ "A", "B", "C" }));
	// a forward move: A after B (the anchor shifts left by the erase)
	REQUIRE(manager.reorderChild("A", "B", true));
	CHECK(manager.getChildren("Root") == Orkige::StringVector({ "B", "A", "C" }));

	// refusals leave the order untouched
	CHECK_FALSE(manager.reorderChild("A", "A", false));			// self
	CHECK_FALSE(manager.reorderChild("A", "Outsider", false));	// other parent
	CHECK_FALSE(manager.reorderChild("A", "NoSuchObject", true));
	CHECK_FALSE(manager.reorderChild("NoSuchObject", "A", true));
	CHECK_FALSE(manager.reorderChild("", "A", true));
	CHECK(manager.getChildren("Root") == Orkige::StringVector({ "B", "A", "C" }));

	// moveChildToIndex restores an exact captured position
	const int oldIndex = manager.getChildIndex("C");
	REQUIRE(manager.reorderChild("C", "B", false));
	CHECK(manager.getChildren("Root") == Orkige::StringVector({ "C", "B", "A" }));
	REQUIRE(manager.moveChildToIndex("C", oldIndex));
	CHECK(manager.getChildren("Root") == Orkige::StringVector({ "B", "A", "C" }));
	CHECK(manager.getChildIndex("NoSuchObject") == -1);

	manager.clear();
}

TEST_CASE("The root sequence is an ordered child-index entry", "[hierarchy]")
{
	Orkige::GameObjectManager & manager = bootHierarchyWorld();
	manager.createGameObject("Zulu");
	manager.createGameObject("Alpha");
	manager.createGameObject("Mike");
	// creation order, NOT alphabetical
	CHECK(manager.getRootObjectIds() ==
		Orkige::StringVector({ "Zulu", "Alpha", "Mike" }));
	CHECK(manager.getChildren("") == manager.getRootObjectIds());

	// roots reorder like any other sibling list
	REQUIRE(manager.reorderChild("Mike", "Zulu", false));
	CHECK(manager.getRootObjectIds() ==
		Orkige::StringVector({ "Mike", "Zulu", "Alpha" }));

	// parenting leaves the root sequence, un-parenting rejoins it at the end
	REQUIRE(manager.getGameObject("Zulu").lock()->setParent("Alpha"));
	CHECK(manager.getRootObjectIds() == Orkige::StringVector({ "Mike", "Alpha" }));
	REQUIRE(manager.getGameObject("Zulu").lock()->setParent(""));
	CHECK(manager.getRootObjectIds() ==
		Orkige::StringVector({ "Mike", "Alpha", "Zulu" }));

	// a deleted root leaves the sequence entirely (it does not re-register)
	REQUIRE(manager.delGameObject("Alpha"));
	CHECK(manager.getRootObjectIds() == Orkige::StringVector({ "Mike", "Zulu" }));

	manager.clear();
}

TEST_CASE("GameObject parenting refuses self, unknown parents and cycles", "[hierarchy]")
{
	Orkige::GameObjectManager & manager = bootHierarchyWorld();
	optr<Orkige::GameObject> a = manager.createGameObject("A").lock();
	optr<Orkige::GameObject> b = manager.createGameObject("B").lock();
	optr<Orkige::GameObject> c = manager.createGameObject("C").lock();
	REQUIRE(b->setParent("A"));
	REQUIRE(c->setParent("B"));

	CHECK_FALSE(a->setParent("A"));				// self
	CHECK_FALSE(a->setParent("NoSuchObject"));	// unknown
	CHECK_FALSE(a->setParent("C"));				// onto own descendant (cycle)
	CHECK_FALSE(a->setParent("B"));				// dito, one level up
	CHECK(a->getParentId().empty());				// refused = unchanged
	CHECK(manager.getChildren("A").size() == 1);

	manager.clear();
}

TEST_CASE("Deleting a parent re-parents its children to the grandparent", "[hierarchy]")
{
	Orkige::GameObjectManager & manager = bootHierarchyWorld();
	manager.createGameObject("Grandparent");
	optr<Orkige::GameObject> parent = manager.createGameObject("Parent").lock();
	optr<Orkige::GameObject> child = manager.createGameObject("Child").lock();
	REQUIRE(parent->setParent("Grandparent"));
	REQUIRE(child->setParent("Parent"));

	REQUIRE(manager.delGameObject("Parent"));
	CHECK_FALSE(manager.objectExists("Parent"));
	CHECK(child->getParentId() == "Grandparent");
	REQUIRE(manager.getChildren("Grandparent").size() == 1);
	CHECK(manager.getChildren("Grandparent").front() == "Child");
	CHECK(manager.getChildren("Parent").empty());

	// deleting a root parent makes the children roots
	REQUIRE(manager.delGameObject("Grandparent"));
	CHECK(child->getParentId().empty());

	manager.clear();
}

TEST_CASE("setActive propagates activeInHierarchy through the tree", "[hierarchy]")
{
	Orkige::GameObjectManager & manager = bootHierarchyWorld();
	optr<Orkige::GameObject> parent = manager.createGameObject("Parent").lock();
	optr<Orkige::GameObject> child = manager.createGameObject("Child").lock();
	optr<Orkige::GameObject> grandchild = manager.createGameObject("Grandchild").lock();
	REQUIRE(child->setParent("Parent"));
	REQUIRE(grandchild->setParent("Child"));
	REQUIRE(child->addComponent<Orkige::TestActivationProbeComponent>());
	Orkige::TestActivationProbeComponent* probe =
		child->getComponentPtr<Orkige::TestActivationProbeComponent>();

	// defaults
	CHECK(parent->isActiveSelf());
	CHECK(child->isActiveInHierarchy());

	// deactivating the parent deactivates the whole subtree
	parent->setActive(false);
	CHECK_FALSE(parent->isActiveSelf());
	CHECK_FALSE(parent->isActiveInHierarchy());
	CHECK(child->isActiveSelf());				// own flag untouched
	CHECK_FALSE(child->isActiveInHierarchy());
	CHECK_FALSE(grandchild->isActiveInHierarchy());
	CHECK(probe->setActiveCalls == 1);
	CHECK_FALSE(probe->lastActiveState);

	// toggling the child's own flag while the parent is off changes nothing
	child->setActive(false);
	child->setActive(true);
	CHECK(probe->setActiveCalls == 1);
	CHECK_FALSE(child->isActiveInHierarchy());

	// reactivating the parent restores the subtree
	parent->setActive(true);
	CHECK(child->isActiveInHierarchy());
	CHECK(grandchild->isActiveInHierarchy());
	CHECK(probe->setActiveCalls == 2);
	CHECK(probe->lastActiveState);

	// but not children that are inactive themselves
	child->setActive(false);
	parent->setActive(false);
	parent->setActive(true);
	CHECK_FALSE(child->isActiveInHierarchy());
	CHECK_FALSE(grandchild->isActiveInHierarchy());

	manager.clear();
}

TEST_CASE("Re-parenting under an inactive parent dispatches onSetActive", "[hierarchy]")
{
	Orkige::GameObjectManager & manager = bootHierarchyWorld();
	optr<Orkige::GameObject> off = manager.createGameObject("Off").lock();
	optr<Orkige::GameObject> mover = manager.createGameObject("Mover").lock();
	REQUIRE(mover->addComponent<Orkige::TestActivationProbeComponent>());
	Orkige::TestActivationProbeComponent* probe =
		mover->getComponentPtr<Orkige::TestActivationProbeComponent>();
	off->setActive(false);

	REQUIRE(mover->setParent("Off"));
	CHECK(probe->parentChangedCalls == 1);
	CHECK(probe->lastParent == off.get());
	CHECK(probe->lastKeepWorld);
	CHECK_FALSE(mover->isActiveInHierarchy());
	CHECK(probe->setActiveCalls == 1);
	CHECK_FALSE(probe->lastActiveState);

	// leaving the inactive parent reactivates
	REQUIRE(mover->setParent(""));
	CHECK(probe->parentChangedCalls == 2);
	CHECK(probe->lastParent == NULL);
	CHECK(mover->isActiveInHierarchy());
	CHECK(probe->setActiveCalls == 2);
	CHECK(probe->lastActiveState);

	manager.clear();
}

TEST_CASE("Components joining an inactive object start deactivated", "[hierarchy]")
{
	Orkige::GameObjectManager & manager = bootHierarchyWorld();
	optr<Orkige::GameObject> obj = manager.createGameObject("Sleeper").lock();
	obj->setActive(false);
	REQUIRE(obj->addComponent<Orkige::TestActivationProbeComponent>());
	Orkige::TestActivationProbeComponent* probe =
		obj->getComponentPtr<Orkige::TestActivationProbeComponent>();
	CHECK(probe->setActiveCalls == 1);
	CHECK_FALSE(probe->lastActiveState);
	manager.clear();
}

TEST_CASE("Inactive objects stop ticking, reactivated objects resume", "[hierarchy]")
{
	Orkige::GameObjectManager & manager = bootHierarchyWorld();
	optr<Orkige::GameObject> parent = manager.createGameObject("Parent").lock();
	optr<Orkige::GameObject> child = manager.createGameObject("Child").lock();
	REQUIRE(child->setParent("Parent"));
	REQUIRE(child->addComponent<Orkige::TestActivationProbeComponent>());
	Orkige::TestActivationProbeComponent* probe =
		child->getComponentPtr<Orkige::TestActivationProbeComponent>();

	manager.update(0.016f);
	CHECK(probe->updateCalls == 1);

	parent->setActive(false);		// ancestor off gates the child's ticks
	manager.update(0.016f);
	manager.update(0.016f);
	CHECK(probe->updateCalls == 1);

	parent->setActive(true);
	manager.update(0.016f);
	CHECK(probe->updateCalls == 2);

	manager.clear();
}

TEST_CASE("SceneSerializer round-trips parented and inactive objects", "[hierarchy][scene]")
{
	Orkige::GameObjectManager & manager = bootHierarchyWorld();
	TempScene scene("orkige_test_hierarchy_scene.oscene");

	{
		manager.createGameObject("Group");
		optr<Orkige::GameObject> tile = manager.createGameObject("Tile").lock();
		optr<Orkige::GameObject> decal = manager.createGameObject("Decal").lock();
		REQUIRE(tile->addComponent<Orkige::TestHealthComponent>());
		tile->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(11);
		REQUIRE(tile->setParent("Group"));
		REQUIRE(decal->setParent("Tile"));
		decal->setActive(false);
	}

	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));
	manager.clear();
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));

	REQUIRE(manager.getGameObjects().size() == 3);
	optr<Orkige::GameObject> tile = manager.getGameObject("Tile").lock();
	optr<Orkige::GameObject> decal = manager.getGameObject("Decal").lock();
	REQUIRE(tile);
	REQUIRE(decal);
	CHECK(tile->getParentId() == "Group");
	CHECK(decal->getParentId() == "Tile");
	REQUIRE(manager.getChildren("Group").size() == 1);
	CHECK(manager.getChildren("Group").front() == "Tile");
	CHECK(tile->getComponentPtr<Orkige::TestHealthComponent>()->getHealth() == 11);

	// active state: the decal is inactive by itself, the tile only serialized
	// its (true) own flag
	CHECK(tile->isActiveSelf());
	CHECK(tile->isActiveInHierarchy());
	CHECK_FALSE(decal->isActiveSelf());
	CHECK_FALSE(decal->isActiveInHierarchy());

	manager.clear();
}

TEST_CASE("SceneSerializer round-trips the SIBLING ORDER through the file's "
	"document order", "[hierarchy][scene]")
{
	Orkige::GameObjectManager & manager = bootHierarchyWorld();
	TempScene scene("orkige_test_sibling_order.oscene");

	// a deliberately NON-alphabetical arrangement at both tiers: the roots run
	// Zulu, Alpha and Zulu's children run Mike, Bravo - neither survives a
	// map-ordered save
	{
		manager.createGameObject("Zulu");
		manager.createGameObject("Alpha");
		optr<Orkige::GameObject> mike = manager.createGameObject("Mike").lock();
		optr<Orkige::GameObject> bravo = manager.createGameObject("Bravo").lock();
		optr<Orkige::GameObject> deep = manager.createGameObject("Deep").lock();
		REQUIRE(mike->setParent("Zulu"));
		REQUIRE(bravo->setParent("Zulu"));
		REQUIRE(deep->setParent("Mike"));
	}
	REQUIRE(manager.getRootObjectIds() ==
		Orkige::StringVector({ "Zulu", "Alpha" }));
	REQUIRE(manager.getChildren("Zulu") ==
		Orkige::StringVector({ "Mike", "Bravo" }));

	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));
	manager.clear();
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));

	CHECK(manager.getRootObjectIds() ==
		Orkige::StringVector({ "Zulu", "Alpha" }));
	CHECK(manager.getChildren("Zulu") ==
		Orkige::StringVector({ "Mike", "Bravo" }));
	CHECK(manager.getChildren("Mike") == Orkige::StringVector({ "Deep" }));

	// a reorder survives the next save/load just as well
	REQUIRE(manager.reorderChild("Bravo", "Mike", false));
	REQUIRE(manager.reorderChild("Alpha", "Zulu", false));
	REQUIRE(Orkige::SceneSerializer::saveScene(scene.path, manager));
	manager.clear();
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	CHECK(manager.getRootObjectIds() ==
		Orkige::StringVector({ "Alpha", "Zulu" }));
	CHECK(manager.getChildren("Zulu") ==
		Orkige::StringVector({ "Bravo", "Mike" }));

	manager.clear();
}

TEST_CASE("SceneSerializer rejects pre-cutover (legacy positional) scene versions", "[hierarchy][scene]")
{
	// the scene format is a CLEAN CUTOVER: the positional readers and the per-version
	// field gates are gone, so an old-version scene file no longer loads (there
	// is a single current format). Assert the honest rejection.
	Orkige::GameObjectManager & manager = bootHierarchyWorld();
	TempScene scene("orkige_test_legacy_v1.oscene");

	// hand-craft a version 1 scene (the historical all-root shape)
	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(scene.path));
		ar << Orkige::SceneSerializer::SCENE_FORMAT_MAGIC;
		int version = 1;
		ar << version;
		unsigned int objectCount = 1;
		ar << objectCount;
		Orkige::String id = "Legacy";
		ar << id;
		unsigned int componentCount = 1;
		ar << componentCount;
		Orkige::String componentTypeName = "TestHealthComponent";
		ar << componentTypeName;
		optr<Orkige::GameObject> proto = Orkige::onew(new Orkige::GameObject("proto"));
		REQUIRE(proto->addComponent<Orkige::TestHealthComponent>());
		proto->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(42);
		ar->write(static_cast<Orkige::ISerializeable&>(
			*proto->getComponentPtr<Orkige::TestHealthComponent>()));
		REQUIRE(ar->stopWriting());
	}

	// the old version is refused (unsupported version) - nothing is loaded
	REQUIRE_FALSE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	CHECK_FALSE(manager.getGameObject("Legacy").lock());

	manager.clear();
}

TEST_CASE("SceneSerializer survives a scene referencing a missing parent", "[hierarchy][scene]")
{
	Orkige::GameObjectManager & manager = bootHierarchyWorld();
	TempScene scene("orkige_test_missing_parent.oscene");

	// hand-craft a current-version scene whose parent reference points nowhere
	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(scene.path));
		ar << Orkige::SceneSerializer::SCENE_FORMAT_MAGIC;
		int version = Orkige::SceneSerializer::SCENE_FORMAT_VERSION;
		ar << version;
		unsigned int objectCount = 1;
		ar << objectCount;
		Orkige::String id = "Orphan";
		ar << id;
		Orkige::String parentId = "GhostParent";
		ar << parentId;
		bool activeSelf = true;
		ar << activeSelf;
		// the v4 per-object tag list (empty)
		unsigned int tagCount = 0;
		ar << tagCount;
		// the v3 prefabRef slot ("" = a plain GameObject)
		Orkige::String prefabRef = "";
		ar << prefabRef;
		unsigned int componentCount = 0;
		ar << componentCount;
		REQUIRE(ar->stopWriting());
	}

	// the broken link is logged and dropped - the scene still loads
	REQUIRE(Orkige::SceneSerializer::loadScene(scene.path, manager));
	optr<Orkige::GameObject> orphan = manager.getGameObject("Orphan").lock();
	REQUIRE(orphan);
	CHECK(orphan->getParentId().empty());

	manager.clear();
}
