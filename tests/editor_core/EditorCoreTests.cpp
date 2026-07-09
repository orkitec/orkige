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
#include <core_game/PrefabSerializer.h>
#include <engine_gocomponent/ScriptComponent.h>

#include <algorithm>
#include <filesystem>

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

	//! register the (headless-safe) ScriptComponent once per test process -
	//! the same registration init_module_orkige_engine performs
	void registerScriptComponent()
	{
		static bool registered = false;
		if (!registered)
		{
			registered = true;
			Orkige::ScriptComponent::OrkigeMetaExport(
				"orkige_editor_core_tests");
		}
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

TEST_CASE("EditorCore SetTagsCommand applies and undoes; tags survive "
	"rename and duplicate", "[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "Old", 7);
	core.selectObject("Old");

	// SetTagsCommand: one undoable step, feeds the manager tag index
	REQUIRE(core.setObjectTags("Old", { "enemy", "boss" }));
	CHECK(manager.findByTag("enemy").size() == 1);
	Orkige::StringVector tags;
	REQUIRE(core.getObjectTags("Old", tags));
	CHECK(tags.size() == 2);
	REQUIRE(core.undo());
	CHECK(manager.findByTag("enemy").empty());
	REQUIRE(core.redo());
	CHECK(manager.findByTag("enemy").size() == 1);

	// rename = serialize + recreate: the tags travel with the object and
	// re-register in the index under the new id
	REQUIRE(core.renameObject("Old", "New"));
	CHECK(manager.findByTag("enemy").empty() == false);
	Orkige::StringVector afterRename = manager.findByTag("enemy");
	REQUIRE(afterRename.size() == 1);
	CHECK(afterRename.front() == "New");
	optr<Orkige::GameObject> renamed = manager.getGameObject("New").lock();
	REQUIRE(renamed);
	CHECK(renamed->hasTag("boss"));

	// duplicate clones the tag set too (both objects carry the tag)
	core.selectObject("New");
	REQUIRE(core.duplicateSelected());
	CHECK(manager.findByTag("enemy").size() == 2);

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

TEST_CASE("EditorCore snap settings default to the constants, are editable "
	"and clamp to positive steps", "[editor]")
{
	Orkige::EditorCore core(freshWorld());

	// defaults: snap off, steps = the historical SNAP_* constants
	CHECK_FALSE(core.isSnapEnabled());
	CHECK(core.getSnapTranslate() == Orkige::EditorCore::SNAP_TRANSLATE);
	CHECK(core.getSnapRotateDegrees() ==
		Orkige::EditorCore::SNAP_ROTATE_DEGREES);
	CHECK(core.getSnapScale() == Orkige::EditorCore::SNAP_SCALE);

	// the toolbar popover path: values are taken as given
	core.setSnapValues(2.0f, 45.0f, 0.25f);
	CHECK(core.getSnapTranslate() == 2.0f);
	CHECK(core.getSnapRotateDegrees() == 45.0f);
	CHECK(core.getSnapScale() == 0.25f);

	// zero/negative steps would freeze the gizmo mid-drag - they clamp to a
	// positive minimum instead
	core.setSnapValues(0.0f, -15.0f, -1.0f);
	CHECK(core.getSnapTranslate() > 0.0f);
	CHECK(core.getSnapRotateDegrees() > 0.0f);
	CHECK(core.getSnapScale() > 0.0f);

	core.setSnapEnabled(true);
	CHECK(core.isSnapEnabled());
}

TEST_CASE("EditorCore selection set: primary rules, toggle, clear",
	"[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "A", 1);
	makeHealthObject(manager, "B", 2);
	makeHealthObject(manager, "C", 3);

	CHECK_FALSE(core.hasSelection());
	CHECK(core.getSelectedObjectId().empty());
	CHECK(core.getSelectionCount() == 0);

	core.selectObject("A");
	CHECK(core.hasSelection());
	CHECK(core.getSelectedObjectId() == "A");
	CHECK(core.getSelectionCount() == 1);

	// toggle adds - the newest member is the primary
	core.toggleSelection("B");
	CHECK(core.getSelectionCount() == 2);
	CHECK(core.getSelectedObjectId() == "B");
	CHECK(core.isSelected("A"));
	CHECK(core.isSelected("B"));
	CHECK_FALSE(core.isSelected("C"));

	// toggling a member removes it - the primary falls back
	core.toggleSelection("B");
	CHECK(core.getSelectionCount() == 1);
	CHECK(core.getSelectedObjectId() == "A");

	// a plain select replaces the whole set
	core.toggleSelection("C");
	core.selectObject("B");
	CHECK(core.getSelectionCount() == 1);
	CHECK(core.getSelectedObjectId() == "B");

	// addToSelection is idempotent
	core.addToSelection("A");
	core.addToSelection("A");
	CHECK(core.getSelectionCount() == 2);
	CHECK(core.getSelectedObjectId() == "A");

	// deselect removes exactly one id, clear removes all
	core.deselectObject("A");
	CHECK(core.getSelectionCount() == 1);
	CHECK(core.getSelectedObjectId() == "B");
	core.clearSelection();
	CHECK_FALSE(core.hasSelection());
	CHECK(core.getSelectionCount() == 0);

	// hasSelection requires the primary to actually exist
	core.selectObject("Missing");
	CHECK_FALSE(core.hasSelection());
	CHECK(core.getSelectionCount() == 1);

	manager.clear();
}

TEST_CASE("CompositeCommand executes in order, undoes in reverse and rolls "
	"back a refused batch", "[editor]")
{
	Orkige::EditorCore core(freshWorld());
	std::vector<std::string> log;

	// an empty composite refuses (nothing enters the stack)
	CHECK_FALSE(core.executeCommand(
		Orkige::onew(new Orkige::CompositeCommand("Empty"))));
	CHECK_FALSE(core.canUndo());

	optr<Orkige::CompositeCommand> batch =
		Orkige::onew(new Orkige::CompositeCommand("Batch"));
	batch->addCommand(Orkige::onew(new ProbeCommand(log, "A")));
	batch->addCommand(Orkige::onew(new ProbeCommand(log, "B")));
	CHECK(batch->size() == 2);
	REQUIRE(core.executeCommand(batch));
	CHECK(log == std::vector<std::string>{ "A:do", "B:do" });
	CHECK(core.getUndoStackSize() == 1); // the batch is ONE undo step
	CHECK(core.getUndoDescription() == "Batch");

	REQUIRE(core.undo());
	CHECK(log == std::vector<std::string>{
		"A:do", "B:do", "B:undo", "A:undo" }); // reverse order
	REQUIRE(core.redo());
	CHECK(log == std::vector<std::string>{
		"A:do", "B:do", "B:undo", "A:undo", "A:do", "B:do" });
	REQUIRE(core.undo());
	log.clear();

	// a failing member rolls the executed prefix back; the batch is refused
	optr<Orkige::CompositeCommand> failing =
		Orkige::onew(new Orkige::CompositeCommand("Failing"));
	failing->addCommand(Orkige::onew(new ProbeCommand(log, "A")));
	failing->addCommand(Orkige::onew(new FailingCommand()));
	failing->addCommand(Orkige::onew(new ProbeCommand(log, "C")));
	core.clearSceneDirty();
	CHECK_FALSE(core.executeCommand(failing));
	CHECK(log == std::vector<std::string>{ "A:do", "A:undo" });
	CHECK_FALSE(core.canUndo());
	CHECK_FALSE(core.isSceneDirty());
}

TEST_CASE("EditorCore add component pulls dependencies in and undo removes "
	"exactly what was added", "[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	optr<Orkige::GameObject> object =
		manager.createGameObject("Obj").lock();
	REQUIRE(object);

	// refusals: unknown type, missing object - nothing enters the stack
	CHECK_FALSE(core.addComponentToObject("Obj", "NoSuchComponent"));
	CHECK_FALSE(core.addComponentToObject("Missing", "TestHealthComponent"));
	CHECK_FALSE(core.canUndo());

	// the registry lists the test components
	Orkige::StringVector addable = core.getAddableComponentTypes();
	CHECK(std::find(addable.begin(), addable.end(),
		"TestArmorComponent") != addable.end());
	CHECK(std::find(addable.begin(), addable.end(),
		"TestHealthComponent") != addable.end());

	// armor depends on health: BOTH arrive through one command
	REQUIRE(core.addComponentToObject("Obj", "TestArmorComponent"));
	CHECK(object->hasComponent<Orkige::TestArmorComponent>());
	CHECK(object->hasComponent<Orkige::TestHealthComponent>());
	CHECK(core.getUndoDescription() == "Add TestArmorComponent to Obj");

	// already attached refuses
	CHECK_FALSE(core.addComponentToObject("Obj", "TestArmorComponent"));

	// undo removes the component AND the dependency it brought in
	REQUIRE(core.undo());
	CHECK_FALSE(object->hasComponent<Orkige::TestArmorComponent>());
	CHECK_FALSE(object->hasComponent<Orkige::TestHealthComponent>());

	REQUIRE(core.redo());
	CHECK(object->hasComponent<Orkige::TestArmorComponent>());
	CHECK(object->hasComponent<Orkige::TestHealthComponent>());

	manager.clear();
}

TEST_CASE("EditorCore add-component undo keeps pre-existing dependencies",
	"[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "Obj", 55); // health exists BEFORE the command

	REQUIRE(core.addComponentToObject("Obj", "TestArmorComponent"));
	REQUIRE(core.undo());
	// health was not added by the command - it survives with its state
	optr<Orkige::GameObject> object = manager.getGameObject("Obj").lock();
	REQUIRE(object);
	CHECK_FALSE(object->hasComponent<Orkige::TestArmorComponent>());
	REQUIRE(object->hasComponent<Orkige::TestHealthComponent>());
	CHECK(object->getComponentPtr<Orkige::TestHealthComponent>()
		->getHealth() == 55);

	manager.clear();
}

TEST_CASE("EditorCore remove component honours the dependency rule and "
	"restores the exact state on undo", "[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	optr<Orkige::GameObject> object =
		manager.createGameObject("Obj").lock();
	REQUIRE(object);
	REQUIRE(object->addComponent<Orkige::TestArmorComponent>());
	object->getComponentPtr<Orkige::TestHealthComponent>()->setHealth(42);
	object->getComponentPtr<Orkige::TestArmorComponent>()->setArmor(7);

	// health is a dependency of the attached armor - removal is BLOCKED
	// (the holder would cascade-remove armor; the editor refuses instead)
	Orkige::String blockedBy;
	CHECK_FALSE(core.canRemoveComponent("Obj", "TestHealthComponent",
		&blockedBy));
	CHECK(blockedBy == "TestArmorComponent");
	CHECK_FALSE(core.removeComponentFromObject("Obj", "TestHealthComponent"));
	CHECK_FALSE(core.canUndo());
	CHECK(object->hasComponent<Orkige::TestHealthComponent>());
	CHECK(object->hasComponent<Orkige::TestArmorComponent>());

	// the dependent itself is removable; the dependency stays untouched
	REQUIRE(core.canRemoveComponent("Obj", "TestArmorComponent"));
	REQUIRE(core.removeComponentFromObject("Obj", "TestArmorComponent"));
	CHECK_FALSE(object->hasComponent<Orkige::TestArmorComponent>());
	REQUIRE(object->hasComponent<Orkige::TestHealthComponent>());
	CHECK(object->getComponentPtr<Orkige::TestHealthComponent>()
		->getHealth() == 42);
	CHECK(core.getUndoDescription() ==
		"Remove TestArmorComponent from Obj");

	// undo restores the component WITH its serialized state
	REQUIRE(core.undo());
	REQUIRE(object->hasComponent<Orkige::TestArmorComponent>());
	CHECK(object->getComponentPtr<Orkige::TestArmorComponent>()
		->getArmor() == 7);
	CHECK(object->getComponentPtr<Orkige::TestHealthComponent>()
		->getHealth() == 42);

	// redo removes again
	REQUIRE(core.redo());
	CHECK_FALSE(object->hasComponent<Orkige::TestArmorComponent>());

	// with the dependent gone, health becomes removable
	REQUIRE(core.canRemoveComponent("Obj", "TestHealthComponent"));
	REQUIRE(core.removeComponentFromObject("Obj", "TestHealthComponent"));
	CHECK_FALSE(object->hasComponent<Orkige::TestHealthComponent>());
	REQUIRE(core.undo());
	CHECK(object->getComponentPtr<Orkige::TestHealthComponent>()
		->getHealth() == 42);

	// refusals: unknown component / missing object
	CHECK_FALSE(core.canRemoveComponent("Obj", "NoSuchComponent"));
	CHECK_FALSE(core.canRemoveComponent("Missing", "TestHealthComponent"));

	manager.clear();
}

TEST_CASE("EditorCore multi-select delete and duplicate batch into ONE undo "
	"step", "[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "A", 1);
	makeHealthObject(manager, "B", 2);
	makeHealthObject(manager, "C", 3);

	// delete both selected objects; C stays
	core.selectObject("A");
	core.toggleSelection("B");
	REQUIRE(core.deleteSelected());
	CHECK_FALSE(manager.objectExists("A"));
	CHECK_FALSE(manager.objectExists("B"));
	CHECK(manager.objectExists("C"));
	CHECK(core.getUndoStackSize() == 1);
	CHECK(core.getUndoDescription() == "Delete 2 Objects");
	CHECK(core.getSelectionCount() == 0);

	// ONE undo restores the whole batch (state included) and the selection
	REQUIRE(core.undo());
	REQUIRE(manager.objectExists("A"));
	REQUIRE(manager.objectExists("B"));
	CHECK(manager.getGameObject("A").lock()
		->getComponentPtr<Orkige::TestHealthComponent>()->getHealth() == 1);
	CHECK(manager.getGameObject("B").lock()
		->getComponentPtr<Orkige::TestHealthComponent>()->getHealth() == 2);
	CHECK(core.isSelected("A"));
	CHECK(core.isSelected("B"));

	// duplicate both: all copies exist, are selected, ONE undo step
	core.selectObject("A");
	core.toggleSelection("B");
	const std::size_t depthBefore = core.getUndoStackSize();
	REQUIRE(core.duplicateSelected());
	REQUIRE(manager.objectExists("A Copy"));
	REQUIRE(manager.objectExists("B Copy"));
	CHECK(manager.getGameObject("A Copy").lock()
		->getComponentPtr<Orkige::TestHealthComponent>()->getHealth() == 1);
	CHECK(core.getSelectionCount() == 2);
	CHECK(core.isSelected("A Copy"));
	CHECK(core.isSelected("B Copy"));
	CHECK(core.getUndoStackSize() == depthBefore + 1);
	CHECK(core.getUndoDescription() == "Duplicate 2 Objects");

	REQUIRE(core.undo());
	CHECK_FALSE(manager.objectExists("A Copy"));
	CHECK_FALSE(manager.objectExists("B Copy"));
	CHECK(core.isSelected("A"));
	CHECK(core.isSelected("B"));

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

TEST_CASE("EditorCore changes ScriptComponent path + enabled undoably",
	"[editor]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	registerScriptComponent();
	Orkige::EditorCore core(manager);
	optr<Orkige::GameObject> object =
		manager.createGameObject("Scripted").lock();
	REQUIRE(object);

	// refusals: no ScriptComponent attached / missing object
	CHECK_FALSE(core.changeObjectScript("Scripted", "scripts/a.lua", true));
	CHECK_FALSE(core.changeObjectScript("Missing", "scripts/a.lua", true));
	CHECK_FALSE(core.canUndo());

	REQUIRE(object->addComponent<Orkige::ScriptComponent>());
	Orkige::ScriptComponent* script =
		object->getComponentPtr<Orkige::ScriptComponent>();

	// a no-op change must not enter the undo stack
	CHECK_FALSE(core.changeObjectScript("Scripted", "", true));
	CHECK_FALSE(core.canUndo());

	// path change (one undo step)
	REQUIRE(core.changeObjectScript("Scripted", "scripts/player.lua", true));
	CHECK(script->getScriptFile() == "scripts/player.lua");
	CHECK(core.getUndoDescription() == "Change Script of Scripted");

	// enabled change (a second undo step, same command type)
	REQUIRE(core.changeObjectScript("Scripted", "scripts/player.lua", false));
	CHECK_FALSE(script->isScriptEnabled());

	REQUIRE(core.undo());
	CHECK(script->isScriptEnabled());
	CHECK(script->getScriptFile() == "scripts/player.lua");
	REQUIRE(core.undo());
	CHECK(script->getScriptFile().empty());
	REQUIRE(core.redo());
	CHECK(script->getScriptFile() == "scripts/player.lua");
	CHECK(script->isScriptEnabled());

	manager.clear();
}

TEST_CASE("EditorCore reparent command re-parents, guards cycles and undoes",
	"[editor][hierarchy]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "Parent", 1);
	makeHealthObject(manager, "Child", 2);
	makeHealthObject(manager, "Grandchild", 3);
	REQUIRE(manager.getGameObject("Grandchild").lock()->setParent("Child"));

	// validation surface for the Hierarchy's drop targets
	CHECK(core.canReparent("Child", "Parent"));
	CHECK(core.canReparent("Child", ""));
	CHECK_FALSE(core.canReparent("Child", "Child"));		// self
	CHECK_FALSE(core.canReparent("Child", "Grandchild"));	// own descendant
	CHECK_FALSE(core.canReparent("Child", "Missing"));
	CHECK_FALSE(core.canReparent("Missing", "Parent"));

	// re-parent (undoable)
	REQUIRE(core.reparentObject("Child", "Parent"));
	CHECK(manager.getGameObject("Child").lock()->getParentId() == "Parent");
	CHECK(core.getUndoDescription() == "Parent Child to Parent");
	CHECK(core.isSceneDirty());

	// a no-op re-parent must not enter the undo stack
	CHECK_FALSE(core.reparentObject("Child", "Parent"));
	CHECK(core.getUndoStackSize() == 1);

	REQUIRE(core.undo());
	CHECK(manager.getGameObject("Child").lock()->getParentId().empty());
	REQUIRE(core.redo());
	CHECK(manager.getGameObject("Child").lock()->getParentId() == "Parent");

	// a refused reparent (cycle) never enters the stack
	CHECK_FALSE(core.reparentObject("Parent", "Grandchild"));
	CHECK(core.getUndoStackSize() == 1);

	manager.clear();
}

TEST_CASE("EditorCore multi-select reparent moves only the topmost objects "
	"as one undo step", "[editor][hierarchy]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "Target", 0);
	makeHealthObject(manager, "A", 1);
	makeHealthObject(manager, "AChild", 2);
	makeHealthObject(manager, "B", 3);
	REQUIRE(manager.getGameObject("AChild").lock()->setParent("A"));

	// selection contains A, its child AND B - dragging A onto Target moves
	// A and B; AChild follows A instead of being flattened
	core.selectObject("A");
	core.addToSelection("AChild");
	core.addToSelection("B");
	REQUIRE(core.reparentObject("A", "Target"));
	CHECK(manager.getGameObject("A").lock()->getParentId() == "Target");
	CHECK(manager.getGameObject("B").lock()->getParentId() == "Target");
	CHECK(manager.getGameObject("AChild").lock()->getParentId() == "A");
	CHECK(core.getUndoStackSize() == 1);

	REQUIRE(core.undo());
	CHECK(manager.getGameObject("A").lock()->getParentId().empty());
	CHECK(manager.getGameObject("B").lock()->getParentId().empty());
	CHECK(manager.getGameObject("AChild").lock()->getParentId() == "A");

	manager.clear();
}

TEST_CASE("EditorCore set-active command toggles activeSelf and undoes",
	"[editor][hierarchy]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "Parent", 1);
	makeHealthObject(manager, "Child", 2);
	REQUIRE(manager.getGameObject("Child").lock()->setParent("Parent"));

	CHECK_FALSE(core.setObjectActive("Missing", false));
	// a no-op toggle must not enter the undo stack
	CHECK_FALSE(core.setObjectActive("Parent", true));
	CHECK_FALSE(core.canUndo());

	REQUIRE(core.setObjectActive("Parent", false));
	CHECK(core.getUndoDescription() == "Deactivate Parent");
	CHECK_FALSE(manager.getGameObject("Parent").lock()->isActiveSelf());
	// the effective state propagates into the subtree
	CHECK_FALSE(manager.getGameObject("Child").lock()->isActiveInHierarchy());
	CHECK(manager.getGameObject("Child").lock()->isActiveSelf());

	REQUIRE(core.undo());
	CHECK(manager.getGameObject("Parent").lock()->isActiveSelf());
	CHECK(manager.getGameObject("Child").lock()->isActiveInHierarchy());
	REQUIRE(core.redo());
	CHECK_FALSE(manager.getGameObject("Child").lock()->isActiveInHierarchy());

	manager.clear();
}

TEST_CASE("EditorCore group command creates the parent, re-parents the "
	"members and undoes cleanly", "[editor][hierarchy]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "TileA", 1);
	makeHealthObject(manager, "TileB", 2);
	makeHealthObject(manager, "TileBChild", 3);
	REQUIRE(manager.getGameObject("TileBChild").lock()->setParent("TileB"));

	// nothing selected: refused
	CHECK_FALSE(core.groupSelected());

	// group TileA + TileB (TileBChild is selected too but rides along
	// under TileB - only topmost objects become members)
	core.selectObject("TileA");
	core.addToSelection("TileB");
	core.addToSelection("TileBChild");
	REQUIRE(core.groupSelected());
	REQUIRE(manager.objectExists("Group1"));
	CHECK(manager.getGameObject("TileA").lock()->getParentId() == "Group1");
	CHECK(manager.getGameObject("TileB").lock()->getParentId() == "Group1");
	CHECK(manager.getGameObject("TileBChild").lock()->getParentId() == "TileB");
	CHECK(core.getUndoDescription() == "Group 2 Objects");
	// the fresh group becomes the selection
	CHECK(core.getSelectedObjectId() == "Group1");
	CHECK(core.getSelectionCount() == 1);

	REQUIRE(core.undo());
	CHECK_FALSE(manager.objectExists("Group1"));
	CHECK(manager.getGameObject("TileA").lock()->getParentId().empty());
	CHECK(manager.getGameObject("TileB").lock()->getParentId().empty());
	CHECK(manager.getGameObject("TileBChild").lock()->getParentId() == "TileB");
	// undo restores the member selection
	CHECK(core.isSelected("TileA"));
	CHECK(core.isSelected("TileB"));

	REQUIRE(core.redo());
	CHECK(manager.objectExists("Group1"));
	CHECK(manager.getGameObject("TileA").lock()->getParentId() == "Group1");

	manager.clear();
}

TEST_CASE("EditorCore delete + undo restores the parent link, the active "
	"flag and the children", "[editor][hierarchy]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "Root", 1);
	makeHealthObject(manager, "Middle", 2);
	makeHealthObject(manager, "Leaf", 3);
	REQUIRE(manager.getGameObject("Middle").lock()->setParent("Root"));
	REQUIRE(manager.getGameObject("Leaf").lock()->setParent("Middle"));
	manager.getGameObject("Middle").lock()->setActive(false);

	core.selectObject("Middle");
	REQUIRE(core.deleteSelected());
	CHECK_FALSE(manager.objectExists("Middle"));
	// the child moved up to the grandparent, keeping the scene intact
	CHECK(manager.getGameObject("Leaf").lock()->getParentId() == "Root");
	// deactivation died with the object
	CHECK(manager.getGameObject("Leaf").lock()->isActiveInHierarchy());

	REQUIRE(core.undo());
	optr<Orkige::GameObject> middle = manager.getGameObject("Middle").lock();
	REQUIRE(middle);
	CHECK(middle->getParentId() == "Root");
	CHECK_FALSE(middle->isActiveSelf());
	// the child is re-attached and inactive again through its parent
	CHECK(manager.getGameObject("Leaf").lock()->getParentId() == "Middle");
	CHECK_FALSE(manager.getGameObject("Leaf").lock()->isActiveInHierarchy());
	CHECK(middle->getComponentPtr<Orkige::TestHealthComponent>()
		->getHealth() == 2);

	manager.clear();
}

TEST_CASE("EditorCore rename re-points the children and keeps the parent "
	"link", "[editor][hierarchy]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "Root", 1);
	makeHealthObject(manager, "OldName", 2);
	makeHealthObject(manager, "Kid", 3);
	REQUIRE(manager.getGameObject("OldName").lock()->setParent("Root"));
	REQUIRE(manager.getGameObject("Kid").lock()->setParent("OldName"));

	REQUIRE(core.renameObject("OldName", "NewName"));
	CHECK_FALSE(manager.objectExists("OldName"));
	CHECK(manager.getGameObject("NewName").lock()->getParentId() == "Root");
	CHECK(manager.getGameObject("Kid").lock()->getParentId() == "NewName");

	REQUIRE(core.undo());
	CHECK(manager.getGameObject("OldName").lock()->getParentId() == "Root");
	CHECK(manager.getGameObject("Kid").lock()->getParentId() == "OldName");

	manager.clear();
}

TEST_CASE("EditorCore duplicate keeps the source's parent and active flag",
	"[editor][hierarchy]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	makeHealthObject(manager, "Holder", 1);
	makeHealthObject(manager, "Original", 2);
	REQUIRE(manager.getGameObject("Original").lock()->setParent("Holder"));
	manager.getGameObject("Original").lock()->setActive(false);

	core.selectObject("Original");
	REQUIRE(core.duplicateSelected());
	optr<Orkige::GameObject> copy =
		manager.getGameObject("Original Copy").lock();
	REQUIRE(copy);
	CHECK(copy->getParentId() == "Holder");
	CHECK_FALSE(copy->isActiveSelf());

	manager.clear();
}

TEST_CASE("EditorCore MakePrefab converts a subtree to an instance and undo "
	"reverts it", "[editor][prefab]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	const std::string prefabPath = (std::filesystem::temp_directory_path() /
		"orkige_editor_test_tile.oprefab").string();
	std::filesystem::remove(prefabPath);

	// the pre-conversion subtree: root + child + grandchild
	makeHealthObject(manager, "Tile", 10);
	makeHealthObject(manager, "Tile_Frame", 1);
	makeHealthObject(manager, "Tile_Frame_Bolt", 2);
	REQUIRE(manager.getGameObject("Tile_Frame").lock()->setParent("Tile"));
	REQUIRE(manager.getGameObject("Tile_Frame_Bolt").lock()
		->setParent("Tile_Frame"));

	// nested-prefab guard: a marked instance below the root refuses
	manager.getGameObject("Tile_Frame").lock()
		->setPrefabRef("other.oprefab", "");
	Orkige::String reason;
	CHECK_FALSE(core.canMakePrefab("Tile", &reason));
	CHECK_FALSE(reason.empty());
	manager.getGameObject("Tile_Frame").lock()->setPrefabRef("", "");
	REQUIRE(core.canMakePrefab("Tile"));

	// the fs half (what createPrefabFromSelection does), then the command
	REQUIRE(Orkige::PrefabSerializer::savePrefab(prefabPath, manager, "Tile"));
	REQUIRE(core.makePrefabInstance("Tile", prefabPath,
		"assets/tile.oprefab", "cafe0123"));

	// converted: children re-created in the deterministic instance namespace
	CHECK_FALSE(manager.objectExists("Tile_Frame"));
	CHECK_FALSE(manager.objectExists("Tile_Frame_Bolt"));
	REQUIRE(manager.objectExists("Tile/Frame"));
	REQUIRE(manager.objectExists("Tile/Frame_Bolt"));
	CHECK(manager.getGameObject("Tile/Frame").lock()->getParentId() == "Tile");
	CHECK(manager.getGameObject("Tile/Frame_Bolt").lock()->getParentId() ==
		"Tile/Frame");
	CHECK(manager.getGameObject("Tile/Frame").lock()
		->getComponentPtr<Orkige::TestHealthComponent>()->getHealth() == 1);
	optr<Orkige::GameObject> root = manager.getGameObject("Tile").lock();
	CHECK(root->getPrefabRef() == "assets/tile.oprefab");
	CHECK(root->getPrefabAssetId() == "cafe0123");
	CHECK(core.isSceneDirty());
	CHECK(core.getUndoStackSize() == 1);
	CHECK(core.getUndoDescription() == "Create Prefab from Tile");

	// prefab children may not become their own prefab (v1)
	CHECK_FALSE(core.canMakePrefab("Tile/Frame"));

	// undo: the original children come back, the mark goes away (the file
	// stays on disk - a fs side effect like an imported mesh)
	REQUIRE(core.undo());
	CHECK_FALSE(manager.objectExists("Tile/Frame"));
	CHECK_FALSE(manager.objectExists("Tile/Frame_Bolt"));
	REQUIRE(manager.objectExists("Tile_Frame"));
	REQUIRE(manager.objectExists("Tile_Frame_Bolt"));
	CHECK(manager.getGameObject("Tile_Frame").lock()->getParentId() == "Tile");
	CHECK(manager.getGameObject("Tile_Frame_Bolt").lock()->getParentId() ==
		"Tile_Frame");
	CHECK(manager.getGameObject("Tile_Frame").lock()
		->getComponentPtr<Orkige::TestHealthComponent>()->getHealth() == 1);
	CHECK(manager.getGameObject("Tile").lock()->getPrefabRef().empty());
	CHECK(std::filesystem::exists(prefabPath));

	// redo converts again
	REQUIRE(core.redo());
	CHECK(manager.objectExists("Tile/Frame"));
	CHECK_FALSE(manager.objectExists("Tile_Frame"));
	CHECK(manager.getGameObject("Tile").lock()->getPrefabRef() ==
		"assets/tile.oprefab");

	std::filesystem::remove(prefabPath);
	manager.clear();
}

TEST_CASE("EditorCore MakePrefab refuses cleanly when the prefab file is "
	"gone and restores the subtree", "[editor][prefab]")
{
	Orkige::GameObjectManager& manager = freshWorld();
	Orkige::EditorCore core(manager);
	const std::string prefabPath = (std::filesystem::temp_directory_path() /
		"orkige_editor_test_missing.oprefab").string();
	std::filesystem::remove(prefabPath);

	makeHealthObject(manager, "Tile", 10);
	makeHealthObject(manager, "Tile_Frame", 1);
	REQUIRE(manager.getGameObject("Tile_Frame").lock()->setParent("Tile"));

	// the command's instantiate step fails (no file) - the refused command
	// must leave the world exactly as it was and enter no undo stack
	CHECK_FALSE(core.makePrefabInstance("Tile", prefabPath,
		"assets/missing.oprefab", ""));
	CHECK(core.getUndoStackSize() == 0);
	REQUIRE(manager.objectExists("Tile_Frame"));
	CHECK(manager.getGameObject("Tile_Frame").lock()->getParentId() == "Tile");
	CHECK(manager.getGameObject("Tile").lock()->getPrefabRef().empty());

	manager.clear();
}
