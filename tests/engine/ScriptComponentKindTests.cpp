/**************************************************************
	created:	2026/07/12 at 11:00
	filename: 	ScriptComponentKindTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for SCRIPT COMPONENTS as named kinds: a behavior script
	whose file ends in ".component.lua" is an attachable component whose kind
	name is the file base name; several different scripts attach to one
	GameObject, each its own container key + sandbox, and their declared
	properties round-trip through the scene. The rendered end-to-end proof is
	the player_twoscript_selfcheck integration run.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "EngineTestEnvironment.h"

#include <engine_gocomponent/ScriptComponent.h>
#include <engine_gocomponent/ScriptComponentRegistry.h>
#include <core_game/GameObject.h>
#include <core_game/GameObjectComponent.h>
#include <core_game/SceneSerializer.h>
#include <core_game/PrefabSerializer.h>
#include <core_game/SaveStore.h>
#include <core_base/PropertySchema.h>
#include <core_base/PropertyValue.h>

#include <chrono>
#include <filesystem>
#include <fstream>

using Orkige::optr;

namespace
{
	//! a throwaway project-like directory with a scripts/ subfolder that can
	//! also hold sub-directories (the recursive-scan case)
	struct TempProjectDir
	{
		std::filesystem::path root;
		explicit TempProjectDir(std::string const & name)
			: root(std::filesystem::temp_directory_path() / name)
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
			std::filesystem::create_directories(this->root / "scripts");
		}
		~TempProjectDir()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
		}
		//! (over)write scripts/<relative> with the given source (relative may
		//! contain sub-directories, which are created)
		void write(std::string const & relative, std::string const & source)
		{
			const std::filesystem::path path = this->root / "scripts" / relative;
			std::error_code ignored;
			std::filesystem::create_directories(path.parent_path(), ignored);
			std::ofstream file(path);
			file << source;
		}
		std::string scriptsDir() const { return (this->root / "scripts").string(); }
	};

	//! read shared.<table>.<key> as double (-1 when missing)
	double sharedNumber(std::string const & table, std::string const & key)
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{"shared", table, key}, -1.0);
	}
	//! read shared.<table>.<key> as string ("" when missing)
	std::string sharedString(std::string const & table, std::string const & key)
	{
		return Orkige::ScriptRuntime::getSingleton().getString(
			{"shared", table, key}, "");
	}
	//! the script-BEHAVIOR tests need a live backend; in ORKIGE_SCRIPTING=OFF
	//! builds they pass trivially (the disabled-but-still-loads path has its
	//! own test below)
	bool scriptingAvailable()
	{
		if (Orkige::ScriptRuntime::available())
		{
			return true;
		}
		SUCCEED("scripting disabled - script behavior test skipped");
		return false;
	}
}

TEST_CASE("ScriptComponentRegistry derives a kind name from a file name", "[script][kind]")
{
	using Orkige::ScriptComponentRegistry;
	// the base name minus ".component.lua"; sub-directories collapse to the base
	CHECK(ScriptComponentRegistry::componentNameForFile("player.component.lua") == "player");
	CHECK(ScriptComponentRegistry::componentNameForFile("a/b/enemy.component.lua") == "enemy");
	// a plain library script is NOT a kind
	CHECK(ScriptComponentRegistry::componentNameForFile("player.lua").empty());
	CHECK(ScriptComponentRegistry::componentNameForFile("helpers.lua").empty());
	// the bare suffix (no base name) is not a kind either
	CHECK(ScriptComponentRegistry::componentNameForFile(".component.lua").empty());
	CHECK(ScriptComponentRegistry::componentNameForFile("component.lua").empty());
}

TEST_CASE("ScriptComponentRegistry discovers *.component.lua kinds", "[script][kind]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();	// boots the app singletons + component factory
	TempProjectDir dir("orkige_kind_discovery_test");
	dir.write("player.component.lua", "function update(self, dt) end\n");
	dir.write("ai/enemy.component.lua", "function update(self, dt) end\n");	// nested
	dir.write("helpers.lua", "-- a plain library, never a component\n");
	// a script whose kind name would SHADOW a real C++ component is refused
	dir.write("TransformComponent.component.lua", "function update(self, dt) end\n");

	ScriptComponentRegistry & registry = ScriptComponentRegistry::getSingleton();
	registry.scanProject(dir.scriptsDir(), dir.root.string());

	// only the two honest kinds are discovered (sorted); the plain .lua and the
	// shadowing name are excluded
	const StringVector names = registry.componentNames();
	REQUIRE(names.size() == 2);
	CHECK(names[0] == "enemy");
	CHECK(names[1] == "player");
	CHECK(registry.isScriptComponent("player"));
	CHECK(registry.isScriptComponent("enemy"));
	CHECK_FALSE(registry.isScriptComponent("helpers"));
	CHECK_FALSE(registry.isScriptComponent("TransformComponent"));
	// the stored path is project-relative (resolves like any script), nested dir
	// preserved
	CHECK(registry.scriptFileForComponent("player") == "scripts/player.component.lua");
	CHECK(registry.scriptFileForComponent("enemy") == "scripts/ai/enemy.component.lua");

	// each kind is now a registered component TYPE (the factory alias): so it is
	// addable, listable and loadable exactly like a C++ component
	CHECK(GameObject::isComponentRegistered(TypeInfo("player")));
	CHECK(GameObject::isComponentRegistered(TypeInfo("enemy")));

	// clear drops the kinds AND their factory aliases (project switch hygiene)
	registry.clear();
	CHECK(registry.componentNames().empty());
	CHECK_FALSE(GameObject::isComponentRegistered(TypeInfo("player")));
}

TEST_CASE("Several script kinds coexist on one object, each ticking its own sandbox",
	"[script][kind]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();
	TempProjectDir dir("orkige_kind_multi_test");
	dir.write("move.component.lua", R"lua(
		function update(self, dt)
			shared.kinds = shared.kinds or {}
			shared.kinds.move = (shared.kinds.move or 0) + 1
		end
	)lua");
	dir.write("blink.component.lua", R"lua(
		function update(self, dt)
			shared.kinds = shared.kinds or {}
			shared.kinds.blink = (shared.kinds.blink or 0) + 1
		end
	)lua");
	ScriptComponentRegistry & registry = ScriptComponentRegistry::getSingleton();
	registry.scanProject(dir.scriptsDir(), dir.root.string());
	// the runtimes point the script search root at the project root alongside
	// the scan, so a kind's relative path resolves - mirror that here
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();
	env.scriptRuntime.runString("if shared then shared.kinds = nil end");

	optr<GameObject> hero = env.gameObjectManager.createGameObject("Hero").lock();
	REQUIRE(hero);
	// two DIFFERENT script kinds attach to ONE object - the type-keyed container
	// keeps them apart by their kind keys (this was impossible before)
	REQUIRE(hero->addComponent(TypeInfo("move")));
	REQUIRE(hero->addComponent(TypeInfo("blink")));

	// each bound its own script file from the registry on attach
	std::vector<ScriptComponent*> scripts = ScriptComponent::collectFrom(*hero);
	REQUIRE(scripts.size() == 2);
	// the collected set carries both kind names (order is map-key order)
	bool sawMove = false, sawBlink = false;
	for (ScriptComponent* s : scripts)
	{
		if (s->getComponentName() == "move")
		{
			sawMove = true;
			CHECK(s->getScriptFile() == "scripts/move.component.lua");
		}
		else if (s->getComponentName() == "blink")
		{
			sawBlink = true;
			CHECK(s->getScriptFile() == "scripts/blink.component.lua");
		}
	}
	CHECK(sawMove);
	CHECK(sawBlink);

	if (scriptingAvailable())
	{
		env.gameObjectManager.update(0.016f);
		// BOTH scripts ticked, in their OWN sandboxes
		CHECK(sharedNumber("kinds", "move") == 1.0);
		CHECK(sharedNumber("kinds", "blink") == 1.0);
	}

	// the same kind twice stays unsupported (the container rejects a duplicate key)
	CHECK_FALSE(hero->addComponent(TypeInfo("move")));

	env.gameObjectManager.clear();
	registry.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("A script kind's declared property override reaches self before init",
	"[script][kind][export]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	TempProjectDir dir("orkige_kind_property_test");
	dir.write("mover.component.lua", R"lua(
		properties = {
			speed = { type = "number", default = 1.0, min = 0, max = 50 },
		}
		function init(self)
			shared.mv = { speed = self.speed }
		end
		function update(self, dt) end
	)lua");
	ScriptComponentRegistry & registry = ScriptComponentRegistry::getSingleton();
	registry.scanProject(dir.scriptsDir(), dir.root.string());
	// the runtimes point the script search root at the project root alongside
	// the scan, so a kind's relative path resolves - mirror that here
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();
	env.scriptRuntime.runString("if shared then shared.mv = nil end");

	optr<GameObject> hero = env.gameObjectManager.createGameObject("Hero").lock();
	REQUIRE(hero->addComponent(TypeInfo("mover")));
	ScriptComponent* script =
		dynamic_cast<ScriptComponent*>(hero->getComponentPtr(TypeInfo("mover")));
	REQUIRE(script != nullptr);

	// the declared property surfaces through the union schema under the kind's
	// dynamic schema (auto-exposed in the inspector, no per-kind wiring)
	const PropertySchema unionSchema = getComponentSchema(*script);
	PropertyDesc const * speed = unionSchema.find("speed");
	REQUIRE(speed != nullptr);
	CHECK(speed->meta.hasRange);
	CHECK(speed->meta.maxValue == Catch::Approx(50.0f));

	// a designer override beats the declared default and is injected onto self
	script->setExportValue("speed", PropertyValue::makeFloat(9.0));
	env.gameObjectManager.update(0.016f);
	REQUIRE(script->isScriptStarted());
	CHECK_FALSE(script->hasScriptError());
	CHECK(sharedNumber("mv", "speed") == 9.0);

	env.gameObjectManager.clear();
	registry.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("Named script kinds round-trip through the scene serializer, with overrides",
	"[script][kind][export]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();
	TempProjectDir dir("orkige_kind_serialize_test");
	dir.write("move.component.lua", R"lua(
		properties = { speed = { type = "number", default = 4.5 } }
		function update(self, dt) end
	)lua");
	dir.write("blink.component.lua", R"lua(
		properties = { rate = { type = "number", default = 2.0 } }
		function update(self, dt) end
	)lua");
	ScriptComponentRegistry & registry = ScriptComponentRegistry::getSingleton();
	registry.scanProject(dir.scriptsDir(), dir.root.string());
	// the runtimes point the script search root at the project root alongside
	// the scan, so a kind's relative path resolves - mirror that here
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();
	const std::string scenePath = (std::filesystem::temp_directory_path() /
		"orkige_kind_roundtrip.oscene").string();

	{
		optr<GameObject> hero = env.gameObjectManager.createGameObject("Hero").lock();
		REQUIRE(hero->addComponent(TypeInfo("move")));
		REQUIRE(hero->addComponent(TypeInfo("blink")));
		if (scriptingAvailable())
		{
			// override just ONE declared property on ONE kind - the other stays
			// at its declared default
			ScriptComponent* move = dynamic_cast<ScriptComponent*>(
				hero->getComponentPtr(TypeInfo("move")));
			move->setExportValue("speed", PropertyValue::makeFloat(12.0));
		}
	}
	REQUIRE(SceneSerializer::saveScene(scenePath, env.gameObjectManager));
	env.gameObjectManager.clear();

	// the kinds are still registered (a project stays open across a scene load),
	// so the scene's "move"/"blink" type tags resolve
	REQUIRE(SceneSerializer::loadScene(scenePath, env.gameObjectManager));
	optr<GameObject> loaded = env.gameObjectManager.getGameObject("Hero").lock();
	REQUIRE(loaded);
	// BOTH kinds came back, by name, each bound to its own script file
	ScriptComponent* move = dynamic_cast<ScriptComponent*>(
		loaded->getComponentPtr(TypeInfo("move")));
	ScriptComponent* blink = dynamic_cast<ScriptComponent*>(
		loaded->getComponentPtr(TypeInfo("blink")));
	REQUIRE(move != nullptr);
	REQUIRE(blink != nullptr);
	CHECK(move->getComponentName() == "move");
	CHECK(blink->getComponentName() == "blink");
	CHECK(move->getScriptFile() == "scripts/move.component.lua");
	CHECK(blink->getScriptFile() == "scripts/blink.component.lua");

	if (scriptingAvailable())
	{
		// the per-instance override survived; the untouched one is the default
		CHECK(move->getExportValue("speed").asFloat() == Catch::Approx(12.0));
		CHECK(blink->getExportValue("rate").asFloat() == Catch::Approx(2.0));
	}

	env.gameObjectManager.clear();
	registry.clear();
	std::error_code ignored;
	std::filesystem::remove(scenePath, ignored);
}

TEST_CASE("Contact and app-lifecycle events reach EVERY script kind on an object",
	"[script][kind]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	TempProjectDir dir("orkige_kind_events_test");
	// two kinds on one object, each defining BOTH optional hooks; each records
	// into its own shared bucket so we can prove both fired
	dir.write("guard.component.lua", R"lua(
		function onContactBegin(self, other)
			shared.ev = shared.ev or {}
			shared.ev.guardContact = (shared.ev.guardContact or 0) + 1
		end
		function onAppPause(self)
			shared.ev = shared.ev or {}
			shared.ev.guardPause = (shared.ev.guardPause or 0) + 1
		end
		function update(self, dt) end
	)lua");
	dir.write("chime.component.lua", R"lua(
		function onContactBegin(self, other)
			shared.ev = shared.ev or {}
			shared.ev.chimeContact = (shared.ev.chimeContact or 0) + 1
		end
		function onAppPause(self)
			shared.ev = shared.ev or {}
			shared.ev.chimePause = (shared.ev.chimePause or 0) + 1
		end
		function update(self, dt) end
	)lua");
	ScriptComponentRegistry & registry = ScriptComponentRegistry::getSingleton();
	registry.scanProject(dir.scriptsDir(), dir.root.string());
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();
	env.scriptRuntime.runString("if shared then shared.ev = nil end");

	optr<GameObject> hero = env.gameObjectManager.createGameObject("Hero").lock();
	optr<GameObject> other = env.gameObjectManager.createGameObject("Other").lock();
	REQUIRE(hero->addComponent(TypeInfo("guard")));
	REQUIRE(hero->addComponent(TypeInfo("chime")));
	// start both scripts (hooks only fire on a loaded, healthy instance)
	env.gameObjectManager.update(0.016f);

	// contact fan-out: the same collect-all path RigidBodyComponent::deliverContact
	// uses - both scripts hear the SAME contact
	for (ScriptComponent* s : ScriptComponent::collectFrom(*hero))
	{
		s->dispatchContact(other.get(), true);
	}
	CHECK(sharedNumber("ev", "guardContact") == 1.0);
	CHECK(sharedNumber("ev", "chimeContact") == 1.0);

	// app-lifecycle fan-out: the manager-level broadcast reaches BOTH scripts
	ScriptComponent::dispatchAppLifecycle(env.gameObjectManager, true);
	CHECK(sharedNumber("ev", "guardPause") == 1.0);
	CHECK(sharedNumber("ev", "chimePause") == 1.0);

	env.gameObjectManager.clear();
	registry.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("A scene with a script kind LOADS even without scripting (inert component)",
	"[script][kind]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();
	TempProjectDir dir("orkige_kind_noscript_test");
	dir.write("logic.component.lua", R"lua(
		properties = { power = { type = "number", default = 3.0 } }
		function update(self, dt) end
	)lua");
	// discovery is a plain directory walk - it registers the kind alias in EVERY
	// build, so the scene's component type resolves with or without a backend
	ScriptComponentRegistry & registry = ScriptComponentRegistry::getSingleton();
	registry.scanProject(dir.scriptsDir(), dir.root.string());
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	CHECK(registry.isScriptComponent("logic"));
	CHECK(GameObject::isComponentRegistered(TypeInfo("logic")));
	env.gameObjectManager.clear();
	const std::string scenePath = (std::filesystem::temp_directory_path() /
		"orkige_kind_noscript.oscene").string();

	{
		optr<GameObject> obj = env.gameObjectManager.createGameObject("Obj").lock();
		REQUIRE(obj->addComponent(TypeInfo("logic")));
	}
	REQUIRE(SceneSerializer::saveScene(scenePath, env.gameObjectManager));
	env.gameObjectManager.clear();

	// the scene loads and the kind is present; in ORKIGE_SCRIPTING=OFF it is
	// simply inert (its ScriptRuntime load fails honestly on the first update)
	REQUIRE(SceneSerializer::loadScene(scenePath, env.gameObjectManager));
	optr<GameObject> loaded = env.gameObjectManager.getGameObject("Obj").lock();
	REQUIRE(loaded);
	ScriptComponent* logic = dynamic_cast<ScriptComponent*>(
		loaded->getComponentPtr(TypeInfo("logic")));
	REQUIRE(logic != nullptr);
	CHECK(logic->getComponentName() == "logic");

	env.gameObjectManager.update(0.016f);
	if (!ScriptRuntime::available())
	{
		// honest: the component exists and serialized, it just cannot run
		CHECK(logic->hasScriptError());
	}

	env.gameObjectManager.clear();
	registry.clear();
	std::error_code ignored;
	std::filesystem::remove(scenePath, ignored);
	(void)sharedString;	// silence unused in configs that skip the behavior legs
}

TEST_CASE("The save table persists distinct keys from every script kind's sandbox",
	"[script][kind][save]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	TempProjectDir dir("orkige_kind_save_test");
	dir.write("saver_a.component.lua", R"lua(
		function init(self)
			save.set("a.coins", 10)
			save.flush()
		end
		function update(self, dt) end
	)lua");
	dir.write("saver_b.component.lua", R"lua(
		function init(self)
			save.set("b.gems", 20)
			save.set("b.name", "hero")
			save.flush()
		end
		function update(self, dt) end
	)lua");
	ScriptComponentRegistry & registry = ScriptComponentRegistry::getSingleton();
	registry.scanProject(dir.scriptsDir(), dir.root.string());
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();

	const std::string savePath = (std::filesystem::temp_directory_path() /
		"orkige_kind_save.osave").string();
	std::error_code ignored;
	std::filesystem::remove(savePath, ignored);
	{
		// a booted SaveStore (its ctor registers it as the singleton the `save`
		// Lua table resolves) pointed at a temp file
		SaveStore store;
		store.setSaveFile(savePath);

		optr<GameObject> hero = env.gameObjectManager.createGameObject("Hero").lock();
		REQUIRE(hero->addComponent(TypeInfo("saver_a")));
		REQUIRE(hero->addComponent(TypeInfo("saver_b")));
		// one tick loads + inits BOTH kinds; each writes from its OWN sandbox
		env.gameObjectManager.update(0.016f);

		// both kinds' writes landed in the ONE shared store - the save table is a
		// global surface, identical from every sandbox
		CHECK(store.getNumber("a.coins", -1.0) == Catch::Approx(10.0));
		CHECK(store.getNumber("b.gems", -1.0) == Catch::Approx(20.0));
		CHECK(store.getString("b.name", "") == "hero");
	}
	// they PERSISTED: a fresh store reading the same file back sees both kinds' keys
	{
		SaveStore reloaded;
		reloaded.setSaveFile(savePath);
		REQUIRE(reloaded.load());
		CHECK(reloaded.getNumber("a.coins", -1.0) == Catch::Approx(10.0));
		CHECK(reloaded.getNumber("b.gems", -1.0) == Catch::Approx(20.0));
		CHECK(reloaded.getString("b.name", "") == "hero");
	}

	env.gameObjectManager.clear();
	registry.clear();
	env.scriptRuntime.setScriptSearchRoot("");
	std::filesystem::remove(savePath, ignored);
}

TEST_CASE("Attaching and removing script kinds interleaved is order-safe",
	"[script][kind]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();
	TempProjectDir dir("orkige_kind_order_test");
	dir.write("one.component.lua", R"lua(
		function update(self, dt)
			shared.ord = shared.ord or {}
			shared.ord.one = (shared.ord.one or 0) + 1
		end
	)lua");
	dir.write("two.component.lua", R"lua(
		function update(self, dt)
			shared.ord = shared.ord or {}
			shared.ord.two = (shared.ord.two or 0) + 1
		end
	)lua");
	dir.write("three.component.lua", R"lua(
		function update(self, dt)
			shared.ord = shared.ord or {}
			shared.ord.three = (shared.ord.three or 0) + 1
		end
	)lua");
	ScriptComponentRegistry & registry = ScriptComponentRegistry::getSingleton();
	registry.scanProject(dir.scriptsDir(), dir.root.string());
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();
	env.scriptRuntime.runString("if shared then shared.ord = nil end");

	optr<GameObject> obj = env.gameObjectManager.createGameObject("Obj").lock();
	REQUIRE(obj);
	// interleaved add / remove: add three kinds, drop the MIDDLE one, re-add it
	// last - the container must keep the survivors intact and never dangle the
	// update-registration vector (the removal path re-keys by the kind key)
	REQUIRE(obj->addComponent(TypeInfo("one")));
	REQUIRE(obj->addComponent(TypeInfo("two")));
	REQUIRE(obj->addComponent(TypeInfo("three")));
	REQUIRE(obj->removeComponent(TypeInfo("two")));
	CHECK(obj->hasComponent(TypeInfo("one")));
	CHECK_FALSE(obj->hasComponent(TypeInfo("two")));
	CHECK(obj->hasComponent(TypeInfo("three")));
	REQUIRE(obj->addComponent(TypeInfo("two")));	// re-add last
	CHECK(ScriptComponent::collectFrom(*obj).size() == 3);

	if (scriptingAvailable())
	{
		// the survivors + the re-added one all still TICK (no stale/dangling
		// update entry, no double-tick)
		env.gameObjectManager.update(0.016f);
		CHECK(sharedNumber("ord", "one") == 1.0);
		CHECK(sharedNumber("ord", "two") == 1.0);
		CHECK(sharedNumber("ord", "three") == 1.0);
	}

	// removing the whole object tears every kind down without a crash
	env.gameObjectManager.delGameObject("Obj");
	CHECK_FALSE(env.gameObjectManager.objectExists("Obj"));

	env.gameObjectManager.clear();
	registry.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("A prefab-provided child's script-kind declared-property override round-trips",
	"[script][kind][prefab]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();
	TempProjectDir dir("orkige_kind_prefab_test");
	dir.write("root_logic.component.lua", "function update(self, dt) end\n");
	dir.write("tile_logic.component.lua", R"lua(
		properties = { power = { type = "number", default = 5 } }
		function update(self, dt) end
	)lua");
	ScriptComponentRegistry & registry = ScriptComponentRegistry::getSingleton();
	registry.scanProject(dir.scriptsDir(), dir.root.string());
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();

	const std::string prefabPath = (std::filesystem::temp_directory_path() /
		"orkige_kind.oprefab").string();
	const std::string prefabName = "orkige_kind.oprefab";
	const std::string scenePath = (std::filesystem::temp_directory_path() /
		"orkige_kind_prefab.oscene").string();
	std::error_code ignored;

	// a prototype: root (root_logic) + a child (tile_logic, the declared-property
	// carrier). Save it as a prefab.
	{
		optr<GameObject> root = env.gameObjectManager.createGameObject("Proto").lock();
		REQUIRE(root->addComponent(TypeInfo("root_logic")));
		optr<GameObject> child =
			env.gameObjectManager.createGameObject("Proto_Body").lock();
		REQUIRE(child->addComponent(TypeInfo("tile_logic")));
		REQUIRE(child->setParent("Proto", false));
	}
	REQUIRE(PrefabSerializer::savePrefab(prefabPath, env.gameObjectManager, "Proto"));
	env.gameObjectManager.clear();

	// instantiate, mark the instance root, and OVERRIDE the provided child's
	// declared property (5 -> 42)
	REQUIRE(PrefabSerializer::instantiatePrefab(prefabPath, env.gameObjectManager,
		"Tile1", StringVector()) == PrefabSerializer::INSTANTIATE_OK);
	optr<GameObject> root = env.gameObjectManager.getGameObject("Tile1").lock();
	REQUIRE(root);
	root->setPrefabRef(prefabName, "");
	ScriptComponent * childScript = dynamic_cast<ScriptComponent*>(
		env.gameObjectManager.getGameObject("Tile1/Body").lock()
			->getComponentPtr(TypeInfo("tile_logic")));
	REQUIRE(childScript != nullptr);
	if (!scriptingAvailable())
	{
		// no dynamic schema without scripting - the prefab still round-trips
		// structurally, which the other prefab tests already prove
		env.gameObjectManager.clear();
		registry.clear();
		std::filesystem::remove(prefabPath, ignored);
		return;
	}
	CHECK(childScript->getExportValue("power").asFloat() == Catch::Approx(5.0));
	childScript->setExportValue("power", PropertyValue::makeFloat(42.0));

	REQUIRE(SceneSerializer::saveScene(scenePath, env.gameObjectManager));
	// the save-time diff recorded the override on the provided CHILD "Body"
	GameObject::ChildOverrideMap const & saved = root->getPrefabChildOverrides();
	REQUIRE(saved.count("Body") == 1);

	env.gameObjectManager.clear();
	REQUIRE(SceneSerializer::loadScene(scenePath, env.gameObjectManager));
	// the child override re-applied OVER the freshly instantiated prefab default
	ScriptComponent * reloaded = dynamic_cast<ScriptComponent*>(
		env.gameObjectManager.getGameObject("Tile1/Body").lock()
			->getComponentPtr(TypeInfo("tile_logic")));
	REQUIRE(reloaded != nullptr);
	CHECK(reloaded->getExportValue("power").asFloat() == Catch::Approx(42.0));

	env.gameObjectManager.clear();
	registry.clear();
	env.scriptRuntime.setScriptSearchRoot("");
	std::filesystem::remove(prefabPath, ignored);
	std::filesystem::remove(scenePath, ignored);
}

TEST_CASE("Hundreds of script components tick within a frame-scale budget",
	"[script][kind][perf]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	TempProjectDir dir("orkige_kind_perf_test");
	// the FLOOR: lifecycle overhead with no work and no declared properties
	dir.write("floor.component.lua", "function update(self, dt) end\n");
	// a heavier kind: a dynamic schema (declared property) + real per-frame work
	dir.write("worker.component.lua", R"lua(
		properties = { speed = { type = "number", default = 1.5 } }
		function init(self) self.t = 0 end
		function update(self, dt)
			self.t = self.t + dt * self.speed
			if self.t > 1000 then self.t = 0 end
		end
	)lua");
	ScriptComponentRegistry & registry = ScriptComponentRegistry::getSingleton();
	registry.scanProject(dir.scriptsDir(), dir.root.string());
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();

	const int kCount = 500;	// a quarter worker-heavy, the rest the empty floor
	for (int i = 0; i < kCount; ++i)
	{
		optr<GameObject> obj = env.gameObjectManager.createGameObject(
			"Perf" + std::to_string(i)).lock();
		REQUIRE(obj);
		REQUIRE(obj->addComponent(TypeInfo((i % 4 == 0) ? "worker" : "floor")));
	}
	// warm up: the first tick lazy-loads + inits every component (the one-time
	// compile cost is NOT part of the steady-state tick we measure)
	env.gameObjectManager.update(0.016f);

	const int kFrames = 120;
	const auto start = std::chrono::steady_clock::now();
	for (int f = 0; f < kFrames; ++f)
	{
		env.gameObjectManager.update(0.016f);
	}
	const double totalMs = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - start).count();
	const double msPerFrame = totalMs / kFrames;
	const double usPerComponent = (msPerFrame * 1000.0) / kCount;

	// no hard threshold (machine-dependent) - the NUMBER is the deliverable, so
	// log it; the CHECK is only a generous regression tripwire (a per-component
	// cost that ballooned into whole milliseconds would be a real regression)
	WARN("script tick perf: " << kCount << " components ("
		<< (kCount / 4) << " with declared properties + work), "
		<< msPerFrame << " ms/frame, " << usPerComponent
		<< " us/component, over " << kFrames << " frames");
	CHECK(msPerFrame < 1000.0);

	env.gameObjectManager.clear();
	registry.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}
