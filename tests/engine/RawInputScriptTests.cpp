/**************************************************************
	created:	2026/08/03 at 11:00
	filename: 	RawInputScriptTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the `input` script table - the RAW device surface
	(touch, pointer, keys, controller) a named action cannot carry. A real
	Lua script reads it, driven by SYNTHETIC SDL events through
	InputManager::injectEvent, so the whole chain from an injected finger to
	the number a game sees is under test. The sandbox leg proves the table
	adds NO capability: every denied global stays nil with `input` present.
	The wired-in-a-running-game proof is the player_raw_input_selfcheck
	integration run (the frame snapshot taken in the loop's input slot).
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "EngineTestEnvironment.h"

#include <engine_gocomponent/ScriptComponent.h>
#include <engine_input/InputManager.h>
#include <core_game/GameObject.h>
#include <core_script/ScriptRuntime.h>

#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>

using Orkige::optr;

namespace
{
	//! a throwaway project-like directory with a scripts/ subfolder
	struct TempInputScriptDir
	{
		std::filesystem::path root;
		explicit TempInputScriptDir(std::string const & name)
			: root(std::filesystem::temp_directory_path() / name)
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
			std::filesystem::create_directories(this->root / "scripts");
		}
		~TempInputScriptDir()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
		}
		std::string write(std::string const & name, std::string const & source)
		{
			const std::filesystem::path path = this->root / "scripts" / name;
			std::ofstream file(path);
			file << source;
			return path.string();
		}
	};

	//! read shared.raw.<key> as double (-1 when missing)
	double rawNumber(std::string const & key)
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{"shared", "raw", key}, -1.0);
	}
	//! read shared.raw.<key> as string ("" when missing)
	Orkige::String rawString(std::string const & key)
	{
		return Orkige::ScriptRuntime::getSingleton().getString(
			{"shared", "raw", key}, "");
	}
	//! read shared.raw.<key> as bool
	bool rawBool(std::string const & key)
	{
		return Orkige::ScriptRuntime::getSingleton().getBool(
			{"shared", "raw", key}, false);
	}
	//! the script-behavior tests need a live backend; ORKIGE_SCRIPTING=OFF
	//! builds pass trivially (nothing runs at all there)
	bool inputScriptingAvailable()
	{
		if (Orkige::ScriptRuntime::available())
		{
			return true;
		}
		SUCCEED("scripting disabled - input table test skipped");
		return false;
	}
}

//---------------------------------------------------------
TEST_CASE("the input table reports injected touches to a Lua script",
	"[script][input][touch]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	if (!inputScriptingAvailable())
	{
		return;
	}
	Orkige::InputManager input(false, false);
	input.setWindowExtents(800, 600);

	TempInputScriptDir dir("orkige_raw_input_touch_test");
	dir.write("touchprobe.lua", R"lua(
		function init(self)
			shared.raw = { count = -1, x = -1, y = -1, phase = "?",
				id = -1, dx = -1 }
		end
		function update(self, dt)
			shared.raw.count = input.touchCount()
			local id, x, y, phase = input.touch(1)
			shared.raw.id = id
			shared.raw.x = x
			shared.raw.y = y
			shared.raw.phase = phase
			local dx, dy = input.touchDelta(1)
			shared.raw.dx = dx
		end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();

	optr<Orkige::GameObject> probe =
		env.gameObjectManager.createGameObject("TouchProbe").lock();
	REQUIRE(probe);
	REQUIRE(probe->addComponent<Orkige::ScriptComponent>());
	probe->getComponentPtr<Orkige::ScriptComponent>()
		->setScriptFile("scripts/touchprobe.lua");

	// no finger down: the script sees an empty frame and an out-of-range
	// touch answers honestly rather than erroring
	input.updateFrameState();
	env.gameObjectManager.update(1.0f / 60.0f);
	CHECK(rawNumber("count") == 0.0);
	CHECK(rawNumber("id") == -1.0);
	CHECK(rawString("phase") == "none");

	// a finger lands at a window-pixel position - the SAME numbers the gui
	// hit-tests widgets in
	REQUIRE(input.injectTouch(0, Orkige::TP_BEGAN, 120.0f, 340.0f));
	input.updateFrameState();
	env.gameObjectManager.update(1.0f / 60.0f);
	CHECK(rawNumber("count") == 1.0);
	CHECK(rawNumber("x") == Catch::Approx(120.0).margin(1.0));
	CHECK(rawNumber("y") == Catch::Approx(340.0).margin(1.0));
	CHECK(rawString("phase") == "began");
	CHECK(rawNumber("dx") == Catch::Approx(0.0).margin(1.0));

	// dragged: phase moves, the delta is against the previous frame
	REQUIRE(input.injectTouch(0, Orkige::TP_MOVED, 200.0f, 340.0f));
	input.updateFrameState();
	env.gameObjectManager.update(1.0f / 60.0f);
	CHECK(rawString("phase") == "moved");
	CHECK(rawNumber("x") == Catch::Approx(200.0).margin(1.0));
	CHECK(rawNumber("dx") == Catch::Approx(80.0).margin(1.0));

	// lifted: reported once more so the game can act on the release
	REQUIRE(input.injectTouch(0, Orkige::TP_ENDED, 200.0f, 340.0f));
	input.updateFrameState();
	env.gameObjectManager.update(1.0f / 60.0f);
	CHECK(rawString("phase") == "ended");
	input.updateFrameState();
	env.gameObjectManager.update(1.0f / 60.0f);
	CHECK(rawNumber("count") == 0.0);

	env.gameObjectManager.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}

//---------------------------------------------------------
TEST_CASE("the input table reports the pointer and a controller to Lua",
	"[script][input][gamepad]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	if (!inputScriptingAvailable())
	{
		return;
	}
	Orkige::InputManager input(false, false);
	input.setWindowExtents(800, 600);

	TempInputScriptDir dir("orkige_raw_input_pointer_test");
	dir.write("pointerprobe.lua", R"lua(
		function init(self)
			shared.raw = {}
		end
		function update(self, dt)
			local p = input.pointer()
			shared.raw.px = p.x
			shared.raw.py = p.y
			shared.raw.down = input.pointerDown()
			shared.raw.pressed = input.pointerPressed("left")
			shared.raw.rightDown = input.pointerDown("right")
			shared.raw.pad = input.gamepadConnected()
			shared.raw.pads = input.gamepadCount()
			shared.raw.south = input.gamepadButton("south")
			shared.raw.a = input.gamepadButton("a")
			shared.raw.lx = input.gamepadAxis("leftx")
			shared.raw.unknownButton = input.gamepadButton("nosuchbutton")
			shared.raw.unknownAxis = input.gamepadAxis("nosuchaxis")
			shared.raw.space = input.keyDown("SPACE")
		end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();

	optr<Orkige::GameObject> probe =
		env.gameObjectManager.createGameObject("PointerProbe").lock();
	REQUIRE(probe);
	REQUIRE(probe->addComponent<Orkige::ScriptComponent>());
	probe->getComponentPtr<Orkige::ScriptComponent>()
		->setScriptFile("scripts/pointerprobe.lua");

	// nothing connected, nothing pressed: honest zeros, and an unknown
	// button/axis name reads false/0 instead of erroring mid-frame
	input.updateFrameState();
	env.gameObjectManager.update(1.0f / 60.0f);
	CHECK_FALSE(rawBool("down"));
	CHECK_FALSE(rawBool("pad"));
	CHECK(rawNumber("pads") == 0.0);
	CHECK_FALSE(rawBool("unknownButton"));
	CHECK(rawNumber("unknownAxis") == 0.0);

	// a pointer press at a window-pixel position
	SDL_Event press{};
	press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	press.button.button = SDL_BUTTON_LEFT;
	press.button.down = true;
	press.button.x = 250.0f;
	press.button.y = 130.0f;
	input.injectEvent(press);
	input.updateFrameState();
	env.gameObjectManager.update(1.0f / 60.0f);
	CHECK(rawNumber("px") == Catch::Approx(250.0));
	CHECK(rawNumber("py") == Catch::Approx(130.0));
	CHECK(rawBool("down"));
	CHECK(rawBool("pressed"));
	CHECK_FALSE(rawBool("rightDown"));

	// the press edge lasts exactly one frame (the snapshot contract)
	input.updateFrameState();
	env.gameObjectManager.update(1.0f / 60.0f);
	CHECK(rawBool("down"));
	CHECK_FALSE(rawBool("pressed"));

	// a controller speaks: the script can show the right button prompt, and
	// the positional and lettered names answer about the SAME button
	input.injectGamepadButton(Orkige::Gamepad::GB_SOUTH, true);
	input.injectGamepadAxis(Orkige::Gamepad::GA_LEFTX, -1.0f);
	input.updateFrameState();
	env.gameObjectManager.update(1.0f / 60.0f);
	CHECK(rawBool("pad"));
	CHECK(rawNumber("pads") == 1.0);
	CHECK(rawBool("south"));
	CHECK(rawBool("a"));
	CHECK(rawNumber("lx") == Catch::Approx(-1.0).margin(1e-3));

	// raw keys by name, the same vocabulary the injected-input grammar spells
	CHECK_FALSE(rawBool("space"));
	input.injectKey(Orkige::KeyEventData::KC_SPACE, true);
	input.updateFrameState();
	env.gameObjectManager.update(1.0f / 60.0f);
	CHECK(rawBool("space"));

	env.gameObjectManager.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}

//---------------------------------------------------------
TEST_CASE("the input table adds no capability to the script sandbox",
	"[script][input][security]")
{
	// THREAT MODEL (Docs/lua-api.md): a script is CONTENT. `input` is
	// READ-ONLY DEVICE STATE - it carries no file, process or code-loading
	// capability, which is why it belongs in the permitted set. This asserts
	// both halves at once: the table is really there, and having it changes
	// nothing about what is denied (no denied global is reachable THROUGH it,
	// its metatable included).
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	if (!inputScriptingAvailable())
	{
		return;
	}
	Orkige::InputManager input(false, false);

	TempInputScriptDir dir("orkige_raw_input_sandbox_test");
	dir.write("sandbox.lua", R"lua(
		function init(self)
			shared.raw = { ok = false }
			-- the sanctioned table is present and is a plain read-only API
			assert(type(input) == "table", "input table missing")
			assert(type(input.touchCount) == "function", "input.touchCount")
			assert(type(input.pointer) == "function", "input.pointer")
			assert(type(input.gamepadButton) == "function", "input.gamepadButton")
			-- every denial still holds with `input` installed
			assert(io == nil, "io reachable")
			assert(require == nil, "require reachable")
			assert(package == nil, "package reachable")
			assert(load == nil, "load reachable")
			assert(loadstring == nil, "loadstring reachable")
			assert(loadfile == nil, "loadfile reachable")
			assert(dofile == nil, "dofile reachable")
			assert(debug == nil, "debug reachable")
			assert(os.execute == nil, "os.execute reachable")
			assert(os.getenv == nil, "os.getenv reachable")
			-- and nothing denied hides inside the table or its metatable
			assert(input.io == nil, "input.io reachable")
			assert(input.load == nil, "input.load reachable")
			assert(input.require == nil, "input.require reachable")
			assert(getmetatable(input) == nil, "input carries a metatable")
			for key, value in pairs(input) do
				assert(type(value) == "function",
					"input carries a non-function member: " .. tostring(key))
			end
			shared.raw.ok = true
		end
		function update(self, dt) end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();

	optr<Orkige::GameObject> probe =
		env.gameObjectManager.createGameObject("SandboxProbe").lock();
	REQUIRE(probe);
	REQUIRE(probe->addComponent<Orkige::ScriptComponent>());
	Orkige::ScriptComponent* script =
		probe->getComponentPtr<Orkige::ScriptComponent>();
	script->setScriptFile("scripts/sandbox.lua");

	env.gameObjectManager.update(1.0f / 60.0f);
	// an assertion failure would have errored the script out; a clean run with
	// the flag set is the whole proof
	CHECK_FALSE(script->hasScriptError());
	CHECK(rawBool("ok"));

	env.gameObjectManager.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}
