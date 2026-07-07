/**************************************************************
	created:	2026/07/07 at 20:00
	filename: 	EditorCoreTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the UI-independent editor layer
	(tools/editor/EditorCore.{h,cpp}): command stack semantics,
	interactive-drag merging, name generation / rename validation and
	the snapshot-based delete/rename/duplicate commands (full component
	state restore) - all with core-only test components.
	Engine-component behaviour (TransformComponent apply, gizmo drags,
	mesh restore) needs live scene nodes and is covered by the
	editor_edittest integration run instead.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"
#include "TestComponents.h"

#include <EditorCore.h>

namespace
{
	//! command probe: records execute/unexecute calls, optionally merges
	class ProbeCommand : public Orkige::EditorCommand
	{
	public:
		ProbeCommand(std::vector<std::string>& log, std::string const& name,
			bool mergeable = false)
			: mLog(log), mName(name), mMergeable(mergeable)
		{
		}
		virtual bool execute(Orkige::EditorCore&) override
		{
			mLog.push_back(mName + ":do");
			return true;
		}
		virtual bool unexecute(Orkige::EditorCore&) override
		{
			mLog.push_back(mName + ":undo");
			return true;
		}
		virtual Orkige::String getDescription() const override
		{
			return mName;
		}
		virtual bool mergeWith(Orkige::EditorCommand const& next) override
		{
			if (!mMergeable)
			{
				return false;
			}
			mName += "+" + next.getDescription();
			return true;
		}

	private:
		std::vector<std::string>& mLog;
		std::string mName;
		bool mMergeable;
	};

	//! command that refuses to execute (must never enter the stack)
	class FailingCommand : public Orkige::EditorCommand
	{
	public:
		virtual bool execute(Orkige::EditorCore&) override { return false; }
		virtual bool unexecute(Orkige::EditorCore&) override { return false; }
		virtual Orkige::String getDescription() const override
		{
			return "Failing";
		}
	};

	//! shared headless environment + a fresh world per test
	Orkige::GameObjectManager& freshWorld()
	{
		Orkige::CoreTestEnvironment& env = Orkige::CoreTestEnvironment::get();
		Orkige::registerOrkigeTestComponents();
		env.gameObjectManager.clear();
		return env.gameObjectManager;
	}

	//! create a bare GameObject with a TestHealthComponent at given health
	void makeHealthObject(Orkige::GameObjectManager& manager,
		Orkige::String const& id, int health)
	{
		optr<Orkige::GameObject> gameObject =
			manager.createGameObject(id).lock();
		REQUIRE(gameObject);
		REQUIRE(gameObject->addComponent<Orkige::TestHealthComponent>());
		gameObject->getComponentPtr<Orkige::TestHealthComponent>()
			->setHealth(health);
	}
}

TEST_CASE("EditorCore command stack does, undoes and redoes in order",
	"[editor]")
{
	Orkige::EditorCore core(freshWorld());
	std::vector<std::string> log;

	REQUIRE_FALSE(core.canUndo());
	REQUIRE_FALSE(core.canRedo());
	REQUIRE_FALSE(core.undo());
	REQUIRE_FALSE(core.redo());

	REQUIRE(core.executeCommand(Orkige::onew(new ProbeCommand(log, "A"))));
	REQUIRE(core.executeCommand(Orkige::onew(new ProbeCommand(log, "B"))));
	CHECK(log == std::vector<std::string>{ "A:do", "B:do" });
	CHECK(core.getUndoStackSize() == 2);
	CHECK(core.getUndoDescription() == "B");
	CHECK(core.getRedoDescription().empty());

	// undo pops LIFO
	REQUIRE(core.undo());
	CHECK(log.back() == "B:undo");
	CHECK(core.getUndoDescription() == "A");
	CHECK(core.getRedoDescription() == "B");

	// redo re-executes
	REQUIRE(core.redo());
	CHECK(log.back() == "B:do");
	CHECK(core.canUndo());
	CHECK_FALSE(core.canRedo());

	REQUIRE(core.undo());
	REQUIRE(core.undo());
	CHECK(log.back() == "A:undo");
	CHECK_FALSE(core.canUndo());
	CHECK(core.getRedoStackSize() == 2);
}

TEST_CASE("EditorCore truncates the redo stack on a new command after undo",
	"[editor]")
{
	Orkige::EditorCore core(freshWorld());
	std::vector<std::string> log;

	REQUIRE(core.executeCommand(Orkige::onew(new ProbeCommand(log, "A"))));
	REQUIRE(core.executeCommand(Orkige::onew(new ProbeCommand(log, "B"))));
	REQUIRE(core.undo());
	REQUIRE(core.canRedo());

	// a new command invalidates the redo branch
	REQUIRE(core.executeCommand(Orkige::onew(new ProbeCommand(log, "C"))));
	CHECK_FALSE(core.canRedo());
	CHECK(core.getUndoStackSize() == 2);
	CHECK(core.getUndoDescription() == "C");

	// remaining undo order is C then A
	REQUIRE(core.undo());
	CHECK(log.back() == "C:undo");
	REQUIRE(core.undo());
	CHECK(log.back() == "A:undo");
}

TEST_CASE("EditorCore rejects commands whose execute fails", "[editor]")
{
	Orkige::EditorCore core(freshWorld());
	core.clearSceneDirty();
	REQUIRE_FALSE(core.executeCommand(Orkige::onew(new FailingCommand())));
	CHECK_FALSE(core.canUndo());
	CHECK_FALSE(core.isSceneDirty());
}

TEST_CASE("EditorCore merges commands of one interactive session into one "
	"undo step", "[editor]")
{
	Orkige::EditorCore core(freshWorld());
	std::vector<std::string> log;

	const unsigned int session = core.beginMergeSession();
	for (char const* name : { "S1", "S2", "S3" })
	{
		optr<Orkige::EditorCommand> command =
			Orkige::onew(new ProbeCommand(log, name, true));
		command->setMergeSession(session);
		REQUIRE(core.executeCommand(command));
	}
	// three executes, ONE stack entry
	CHECK(log == std::vector<std::string>{ "S1:do", "S2:do", "S3:do" });
	CHECK(core.getUndoStackSize() == 1);
	CHECK(core.getUndoDescription() == "S1+S2+S3");

	// a new session does NOT merge into the previous one
	const unsigned int nextSession = core.beginMergeSession();
	optr<Orkige::EditorCommand> next =
		Orkige::onew(new ProbeCommand(log, "T1", true));
	next->setMergeSession(nextSession);
	REQUIRE(core.executeCommand(next));
	CHECK(core.getUndoStackSize() == 2);

	// session 0 (the default) never merges
	optr<Orkige::EditorCommand> plain =
		Orkige::onew(new ProbeCommand(log, "T2", true));
	REQUIRE(core.executeCommand(plain));
	optr<Orkige::EditorCommand> plain2 =
		Orkige::onew(new ProbeCommand(log, "T3", true));
	REQUIRE(core.executeCommand(plain2));
	CHECK(core.getUndoStackSize() == 4);
}

TEST_CASE("TransformChangeCommand merges before/after states per object",
	"[editor]")
{
	Orkige::EditorTransform t0;
	Orkige::EditorTransform t1;
	t1.position = Ogre::Vector3(1.0f, 0.0f, 0.0f);
	Orkige::EditorTransform t2;
	t2.position = Ogre::Vector3(2.0f, 0.0f, 0.0f);
	t2.scale = Ogre::Vector3(3.0f, 3.0f, 3.0f);

	Orkige::TransformChangeCommand drag("Obj", t0, t1);
	Orkige::TransformChangeCommand dragStep("Obj", t1, t2);
	// same object: absorb the newest "after", keep the drag-start "before"
	REQUIRE(drag.mergeWith(dragStep));
	CHECK(drag.getBefore().position.positionEquals(t0.position, 1e-6f));
	CHECK(drag.getAfter().position.positionEquals(t2.position, 1e-6f));
	CHECK(drag.getAfter().scale.positionEquals(t2.scale, 1e-6f));

	// another object never merges
	Orkige::TransformChangeCommand other("Other", t0, t1);
	CHECK_FALSE(drag.mergeWith(other));
	CHECK(drag.getDescription() == "Transform Obj");
}

TEST_CASE("EditorCore generates unique object and duplicate names",
	"[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);

	// counter-based ids skip taken names
	manager.createGameObject("Cube1");
	manager.createGameObject("Cube2");
	CHECK(core.generateObjectId("Cube") == "Cube3");
	manager.createGameObject("Cube4");
	CHECK(core.generateObjectId("Cube") == "Cube5"); // 4 is taken
	CHECK(core.generateObjectId("TestMesh") == "TestMesh1");

	// duplicate names: "<id> Copy", then "<id> Copy 2", ...
	manager.createGameObject("Alpha");
	CHECK(core.makeDuplicateId("Alpha") == "Alpha Copy");
	manager.createGameObject("Alpha Copy");
	CHECK(core.makeDuplicateId("Alpha") == "Alpha Copy 2");
	manager.createGameObject("Alpha Copy 2");
	CHECK(core.makeDuplicateId("Alpha") == "Alpha Copy 3");

	manager.clear();
}

TEST_CASE("EditorCore validates renames (empty/duplicate/unchanged rejected)",
	"[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	manager.createGameObject("Alpha");
	manager.createGameObject("Beta");

	CHECK(core.validateRename("Alpha", "") ==
		Orkige::EditorCore::NameValidation::Empty);
	CHECK(core.validateRename("Alpha", "Alpha") ==
		Orkige::EditorCore::NameValidation::Unchanged);
	CHECK(core.validateRename("Alpha", "Beta") ==
		Orkige::EditorCore::NameValidation::Exists);
	CHECK(core.validateRename("Alpha", "Gamma") ==
		Orkige::EditorCore::NameValidation::Ok);

	// the undoable operation obeys the same rules
	CHECK_FALSE(core.renameObject("Alpha", ""));
	CHECK_FALSE(core.renameObject("Alpha", "Beta"));
	CHECK_FALSE(core.renameObject("Alpha", "Alpha"));
	CHECK_FALSE(core.renameObject("Missing", "Gamma"));
	CHECK_FALSE(core.canUndo());

	manager.clear();
}

TEST_CASE("EditorCore delete + undo restores the full serialized component "
	"state", "[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);

	// armor pulls health in as a dependency - the restore path must cope
	// with components that dependencies already added
	optr<Orkige::GameObject> victim =
		manager.createGameObject("Victim").lock();
	REQUIRE(victim);
	REQUIRE(victim->addComponent<Orkige::TestArmorComponent>());
	victim->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(42);
	victim->getComponentPtr<Orkige::TestArmorComponent>()->setArmor(7);
	victim.reset();

	core.selectObject("Victim");
	core.clearSceneDirty();
	REQUIRE(core.deleteSelected());
	CHECK_FALSE(manager.objectExists("Victim"));
	CHECK_FALSE(core.hasSelection());
	CHECK(core.isSceneDirty());
	CHECK(core.getUndoDescription() == "Delete Victim");

	REQUIRE(core.undo());
	optr<Orkige::GameObject> restored =
		manager.getGameObject("Victim").lock();
	REQUIRE(restored);
	REQUIRE(restored->hasComponent<Orkige::TestHealthComponent>());
	REQUIRE(restored->hasComponent<Orkige::TestArmorComponent>());
	CHECK(restored->getComponentPtr<Orkige::TestHealthComponent>()
		->getHealth() == 42);
	CHECK(restored->getComponentPtr<Orkige::TestArmorComponent>()
		->getArmor() == 7);
	CHECK(core.getSelectedObjectId() == "Victim");

	// redo deletes again
	REQUIRE(core.redo());
	CHECK_FALSE(manager.objectExists("Victim"));

	manager.clear();
}

TEST_CASE("EditorCore rename command moves the component state and undoes",
	"[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "Old", 13);
	core.selectObject("Old");

	REQUIRE(core.renameObject("Old", "New"));
	CHECK_FALSE(manager.objectExists("Old"));
	optr<Orkige::GameObject> renamed = manager.getGameObject("New").lock();
	REQUIRE(renamed);
	CHECK(renamed->getComponentPtr<Orkige::TestHealthComponent>()
		->getHealth() == 13);
	// selection follows the rename
	CHECK(core.getSelectedObjectId() == "New");
	CHECK(core.getUndoDescription() == "Rename Old to New");

	REQUIRE(core.undo());
	CHECK_FALSE(manager.objectExists("New"));
	optr<Orkige::GameObject> back = manager.getGameObject("Old").lock();
	REQUIRE(back);
	CHECK(back->getComponentPtr<Orkige::TestHealthComponent>()
		->getHealth() == 13);
	CHECK(core.getSelectedObjectId() == "Old");

	manager.clear();
}

TEST_CASE("EditorCore duplicate clones component state and undoes",
	"[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "Source", 21);
	core.selectObject("Source");

	REQUIRE(core.duplicateSelected());
	// the copy is selected (no TransformComponent here, so no offset - the
	// engine-level offset is asserted by the editor_edittest run)
	CHECK(core.getSelectedObjectId() == "Source Copy");
	optr<Orkige::GameObject> copy =
		manager.getGameObject("Source Copy").lock();
	REQUIRE(copy);
	CHECK(copy->getComponentPtr<Orkige::TestHealthComponent>()
		->getHealth() == 21);

	REQUIRE(core.undo());
	CHECK_FALSE(manager.objectExists("Source Copy"));
	CHECK(manager.objectExists("Source"));
	CHECK(core.getSelectedObjectId() == "Source");

	manager.clear();
}

TEST_CASE("EditorCore marks the scene dirty on every stack operation and "
	"resets cleanly", "[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "Thing", 1);

	CHECK_FALSE(core.isSceneDirty());
	core.selectObject("Thing");
	REQUIRE(core.deleteSelected());
	CHECK(core.isSceneDirty());

	core.clearSceneDirty();
	REQUIRE(core.undo());
	CHECK(core.isSceneDirty()); // undo changes the scene too

	core.clearSceneDirty();
	REQUIRE(core.redo());
	CHECK(core.isSceneDirty());

	// resetForScene: selection, history and dirty flag all go
	core.resetForScene();
	CHECK_FALSE(core.hasSelection());
	CHECK_FALSE(core.canUndo());
	CHECK_FALSE(core.canRedo());
	CHECK_FALSE(core.isSceneDirty());

	manager.clear();
}

TEST_CASE("EditorObjectSnapshot round-trips a single object", "[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	makeHealthObject(manager, "Snap", 77);

	Orkige::EditorObjectSnapshot snapshot;
	CHECK(snapshot.empty());
	REQUIRE(snapshot.capture(manager, "Snap"));
	CHECK_FALSE(snapshot.empty());

	// restore refuses a taken id
	CHECK_FALSE(snapshot.restore(manager, "Snap"));

	REQUIRE(snapshot.restore(manager, "Snap2"));
	optr<Orkige::GameObject> clone = manager.getGameObject("Snap2").lock();
	REQUIRE(clone);
	CHECK(clone->getComponentPtr<Orkige::TestHealthComponent>()
		->getHealth() == 77);

	// capture of a missing object fails
	Orkige::EditorObjectSnapshot missing;
	CHECK_FALSE(missing.capture(manager, "NoSuchObject"));

	manager.clear();
}
