/**************************************************************
	created:	2026/07/09 at 10:20
	filename: 	InputActionTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless input-action-mapping unit tests: the pure binding math
	(axisFromKeys / combineMaxMagnitude), the built-in default set, the
	once-per-frame edge snapshot driven by SYNTHETIC SDL key events through
	InputManager::injectEvent (no window/GPU - the same injectEvent path the
	selfchecks and scripted runs use), the tilt-axis mapping via setTiltAngle
	and the .oactions XMLArchive round-trip / override. The in-game proof is
	the player_jumper_lua_selfcheck + player_roller_selfcheck integration runs
	on the migrated scripts.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "EngineTestEnvironment.h"

#include <engine_input/InputActionMap.h>
#include <engine_input/InputManager.h>
#include <core_project/Project.h>
#include <core_serialization/XMLArchive.h>

#include <SDL3/SDL.h>
#include <cmath>
#include <filesystem>
#include <limits>

using Catch::Approx;
using namespace Orkige;

namespace
{
	//! push one synthetic key state change straight into the InputManager
	//! (the injectEvent path, exactly like the player's SDL poll loop)
	void injectKey(InputManager & input, SDL_Scancode scancode, bool down)
	{
		SDL_Event event{};
		event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
		event.key.scancode = scancode;
		event.key.down = down;
		input.injectEvent(event);
	}
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: axisFromKeys is the promoted axis() helper", "[input]")
{
	CHECK(InputActionMap::axisFromKeys(false, false) == Approx(0.0f));
	CHECK(InputActionMap::axisFromKeys(false, true) == Approx(1.0f));
	CHECK(InputActionMap::axisFromKeys(true, false) == Approx(-1.0f));
	// both held cancel out (opposite keys pressed together)
	CHECK(InputActionMap::axisFromKeys(true, true) == Approx(0.0f));
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: combineMaxMagnitude keeps the strongest push", "[input]")
{
	CHECK(InputActionMap::combineMaxMagnitude(0.0f, 0.7f) == Approx(0.7f));
	CHECK(InputActionMap::combineMaxMagnitude(0.7f, 0.0f) == Approx(0.7f));
	// larger magnitude wins regardless of sign (tilt OR arrows)
	CHECK(InputActionMap::combineMaxMagnitude(0.4f, -0.9f) == Approx(-0.9f));
	CHECK(InputActionMap::combineMaxMagnitude(-0.9f, 0.4f) == Approx(-0.9f));
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: the built-in defaults cover both reference games", "[input]")
{
	EngineTestEnvironment::get();
	InputActionMap actions;	// the constructor loads the defaults

	CHECK(actions.hasAction("move"));		// jumper 2D movement
	CHECK(actions.hasAction("jump"));		// jumper jump
	CHECK(actions.hasAction("steer"));		// roller tilt/arrow steering
	CHECK(actions.hasAction("menu_toggle"));	// roller TAB
	CHECK(actions.hasAction("menu_left"));
	CHECK(actions.hasAction("menu_right"));
	CHECK(actions.hasAction("menu_up"));
	CHECK(actions.hasAction("menu_down"));
	CHECK_FALSE(actions.hasAction("no_such_action"));
	CHECK(actions.getActionCount() >= 8u);
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: digital down/pressed/released edges", "[input]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);	// no native init (no sensor/window)
	InputActionMap actions;

	// nothing held yet
	actions.update(1.0f / 60.0f);
	CHECK_FALSE(actions.down("jump"));
	CHECK_FALSE(actions.pressed("jump"));

	// SPACE goes down -> pressed exactly this frame, down stays true
	injectKey(input, SDL_SCANCODE_SPACE, true);
	actions.update(1.0f / 60.0f);
	CHECK(actions.down("jump"));
	CHECK(actions.pressed("jump"));
	CHECK_FALSE(actions.released("jump"));

	// held into the next frame -> still down, no longer a fresh press
	actions.update(1.0f / 60.0f);
	CHECK(actions.down("jump"));
	CHECK_FALSE(actions.pressed("jump"));

	// SPACE releases -> released exactly this frame
	injectKey(input, SDL_SCANCODE_SPACE, false);
	actions.update(1.0f / 60.0f);
	CHECK_FALSE(actions.down("jump"));
	CHECK_FALSE(actions.pressed("jump"));
	CHECK(actions.released("jump"));
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: pressed is stable within a frame and true one frame", "[input]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);
	InputActionMap actions;

	injectKey(input, SDL_SCANCODE_SPACE, true);
	actions.update(1.0f / 60.0f);
	// two queries in the SAME frame must agree (snapshot, not recomputed)
	CHECK(actions.pressed("jump"));
	CHECK(actions.pressed("jump"));

	// without another update the snapshot is unchanged; the NEXT update (key
	// still held) clears the one-frame press
	actions.update(1.0f / 60.0f);
	CHECK_FALSE(actions.pressed("jump"));
	CHECK(actions.down("jump"));
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: analog2D keyAxis value2 for move", "[input]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);
	InputActionMap actions;

	// RIGHT alone -> move.x = +1, move.y = 0
	injectKey(input, SDL_SCANCODE_RIGHT, true);
	actions.update(1.0f / 60.0f);
	CHECK(actions.value2("move").x == Approx(1.0f));
	CHECK(actions.value2("move").y == Approx(0.0f));

	// add LEFT -> the two opposite keys cancel on the x axis
	injectKey(input, SDL_SCANCODE_LEFT, true);
	actions.update(1.0f / 60.0f);
	CHECK(actions.value2("move").x == Approx(0.0f));

	// drop RIGHT -> LEFT wins, move.x = -1
	injectKey(input, SDL_SCANCODE_RIGHT, false);
	actions.update(1.0f / 60.0f);
	CHECK(actions.value2("move").x == Approx(-1.0f));

	// DOWN drives the depth axis (move.y positive: W/UP negative, S/DOWN positive)
	injectKey(input, SDL_SCANCODE_DOWN, true);
	actions.update(1.0f / 60.0f);
	CHECK(actions.value2("move").y == Approx(1.0f));
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: tiltAxis reads the tilt component (0 at rest)", "[input]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);
	InputActionMap actions;

	// the desktop tilt simulation is only meaningful without a real sensor
	if(input.isTiltSensorAvailable())
	{
		SUCCEED("a real accelerometer drives getTilt on this host - skipped");
		return;
	}

	// upright: getTilt() is (0,-1,0), so the X component (steer) reads 0
	input.setTiltAngle(0.0f);
	actions.update(1.0f / 60.0f);
	CHECK(actions.value("steer") == Approx(0.0f).margin(1e-4));

	// tilt right: getTilt().x = sin(angle) > 0 -> steer positive
	input.setTiltAngle(0.5f);
	actions.update(1.0f / 60.0f);
	CHECK(actions.value("steer") > 0.1f);

	// tilt left: negative
	input.setTiltAngle(-0.5f);
	actions.update(1.0f / 60.0f);
	CHECK(actions.value("steer") < -0.1f);
}

//---------------------------------------------------------
TEST_CASE("InputManager: tilt sensor samples gate on finiteness and gravity",
	"[input]")
{
	// the pure sample classification behind the accelerometer stream: a
	// browser's devicemotion shim delivers null -> NaN fields on desktops
	// and in headless runs - those samples must be discarded, and only a
	// finite, gravity-bearing sample may put the sensor in charge
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	CHECK_FALSE(InputManager::tiltSampleUsable(nan, 0.0f, 9.8f));
	CHECK_FALSE(InputManager::tiltSampleUsable(0.0f, nan, 9.8f));
	CHECK_FALSE(InputManager::tiltSampleUsable(0.0f, 0.0f, inf));
	CHECK(InputManager::tiltSampleUsable(0.0f, 0.0f, 0.0f));
	CHECK(InputManager::tiltSampleUsable(0.0f, 9.8f, 0.0f));

	// a NaN sample never counts as gravity, an all-zero one (no data /
	// free fall) does not either; a resting device in any pose does
	CHECK_FALSE(InputManager::tiltSampleGravityBearing(nan, nan, nan));
	CHECK_FALSE(InputManager::tiltSampleGravityBearing(0.0f, 0.0f, 0.0f));
	CHECK_FALSE(InputManager::tiltSampleGravityBearing(0.1f, 0.1f, 0.1f));
	CHECK(InputManager::tiltSampleGravityBearing(0.0f, 0.0f, 9.8f));
	CHECK(InputManager::tiltSampleGravityBearing(0.0f, 9.8f, 0.0f));
	CHECK(InputManager::tiltSampleGravityBearing(-6.9f, -6.9f, 0.0f));
}

//---------------------------------------------------------
TEST_CASE("InputManager: an open-but-silent sensor leaves the keys driving",
	"[input]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);

	// a machine with a REAL accelerometer that has already spoken reports
	// available; everywhere else - including a browser whose devicemotion
	// sensor exists but never delivers - the tilt must stay finite and the
	// simulation must stay reachable (the web roller regression: NaN tilt
	// sank the ball)
	const Vec3 tilt = input.getTilt();
	CHECK(std::isfinite(tilt.x));
	CHECK(std::isfinite(tilt.y));
	CHECK(std::isfinite(tilt.z));
	if(!input.isTiltSensorAvailable())
	{
		// the simulated path answers: setTiltAngle steers getTilt
		input.setTiltAngle(0.5f);
		CHECK(input.getTilt().x > 0.1f);
		input.setTiltAngle(0.0f);
	}
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: .oactions round-trips through XMLArchive", "[input]")
{
	EngineTestEnvironment::get();

	const std::filesystem::path path =
		std::filesystem::temp_directory_path() / "orkige_roundtrip.oactions";

	// InputActionMap is a Singleton - only one may live at a time, so the
	// "save" instance is scoped out before the "load" instance is created
	size_t defaultCount = 0;
	{
		InputActionMap saved;	// the defaults
		defaultCount = saved.getActionCount();
		REQUIRE(saved.saveActions(path.string()));
	}

	InputActionMap loaded;
	// mutate first so we can prove load truly replaced the set
	InputAction extra;
	extra.name = "temporary";
	extra.kind = InputActionKind::Digital;
	loaded.setAction(extra);
	REQUIRE(loaded.hasAction("temporary"));

	REQUIRE(loaded.loadActions(path.string()));
	CHECK(loaded.getActionCount() == defaultCount);
	CHECK(loaded.hasAction("move"));
	CHECK(loaded.hasAction("jump"));
	CHECK_FALSE(loaded.hasAction("temporary"));	// the load REPLACED the set

	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: a file override replaces the defaults", "[input]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);

	const std::filesystem::path path =
		std::filesystem::temp_directory_path() / "orkige_override.oactions";

	// hand-author a one-action override file in the .oactions format (magic,
	// version, count, then per-action name/kind and per-binding fields - the
	// positional XMLArchive contract of InputActionMap::loadActions)
	{
		optr<XMLArchive> ar = onew(new XMLArchive());
		REQUIRE(ar->startWriting(path.string()));
		ar << InputActionMap::ACTIONS_FILE_MAGIC;
		int version = InputActionMap::ACTIONS_FORMAT_VERSION;
		ar << version;
		unsigned int actionCount = 1;
		ar << actionCount;
		// action "fire", digital
		String name = "fire";
		ar << name;
		int kind = static_cast<int>(InputActionKind::Digital);
		ar << kind;
		unsigned int bindingCount = 1;
		ar << bindingCount;
		// one Key binding on SPACE, output component 0
		int type = static_cast<int>(InputActionBinding::Key);
		ar << type;
		int outputComponent = 0;
		ar << outputComponent;
		int tiltComponent = 0;
		ar << tiltComponent;
		unsigned int keyCount = 1;
		ar << keyCount;					// keys[]
		int keyCode = static_cast<int>(KeyEventData::KC_SPACE);
		ar << keyCode;
		unsigned int noKeys = 0;
		ar << noKeys;					// negativeKeys[]
		ar << noKeys;					// positiveKeys[]
		REQUIRE(ar->stopWriting());
	}

	InputActionMap actions;	// defaults first
	REQUIRE(actions.hasAction("move"));
	REQUIRE(actions.loadActions(path.string()));
	CHECK(actions.getActionCount() == 1u);
	CHECK(actions.hasAction("fire"));
	CHECK_FALSE(actions.hasAction("move"));	// the override fully replaced

	// the loaded binding actually works: SPACE fires
	injectKey(input, SDL_SCANCODE_SPACE, true);
	actions.update(1.0f / 60.0f);
	CHECK(actions.pressed("fire"));

	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: loadForProject falls back to defaults", "[input]")
{
	EngineTestEnvironment::get();
	InputActionMap actions;
	// an unloaded project has no "input.actions" setting -> defaults stand
	Project project;
	actions.loadForProject(project);
	CHECK(actions.hasAction("move"));
	CHECK(actions.hasAction("menu_toggle"));
}
