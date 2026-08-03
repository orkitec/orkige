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
	//! a synthetic pointer button edge at a window-pixel position
	void injectPointerButton(InputManager & input, Uint8 button, bool down,
		float x, float y)
	{
		SDL_Event event{};
		event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
			: SDL_EVENT_MOUSE_BUTTON_UP;
		event.button.button = button;
		event.button.down = down;
		event.button.x = x;
		event.button.y = y;
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
		unsigned int noGamepadButtons = 0;
		ar << noGamepadButtons;			// gamepadButtons[]
		int gamepadAxis = static_cast<int>(Gamepad::GA_LEFTX);
		ar << gamepadAxis;
		float deadzone = 0.25f;
		ar << deadzone;
		bool invert = false;
		ar << invert;
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

//---------------------------------------------------------
//--- controllers -----------------------------------------
//---------------------------------------------------------
TEST_CASE("InputActionMap: applyDeadzone is a rescaled curve, not a cut-off",
	"[input][gamepad]")
{
	// inside the zone reads exactly 0 - a stick at mechanical rest never
	// reports 0, and an un-deadzoned binding would drift the player
	CHECK(InputActionMap::applyDeadzone(0.0f, 0.25f) == Approx(0.0f));
	CHECK(InputActionMap::applyDeadzone(0.2f, 0.25f) == Approx(0.0f));
	CHECK(InputActionMap::applyDeadzone(-0.25f, 0.25f) == Approx(0.0f));

	// past the zone the value RESCALES: the first movement out starts near 0
	// (no jump to the zone's width) and full deflection still reaches +-1
	CHECK(InputActionMap::applyDeadzone(0.26f, 0.25f) < 0.05f);
	CHECK(InputActionMap::applyDeadzone(0.26f, 0.25f) > 0.0f);
	CHECK(InputActionMap::applyDeadzone(0.625f, 0.25f) == Approx(0.5f));
	CHECK(InputActionMap::applyDeadzone(1.0f, 0.25f) == Approx(1.0f));
	CHECK(InputActionMap::applyDeadzone(-1.0f, 0.25f) == Approx(-1.0f));

	// a zero zone passes through (clamped); a full one reads nothing
	CHECK(InputActionMap::applyDeadzone(0.1f, 0.0f) == Approx(0.1f));
	CHECK(InputActionMap::applyDeadzone(1.4f, 0.0f) == Approx(1.0f));
	CHECK(InputActionMap::applyDeadzone(1.0f, 1.0f) == Approx(0.0f));

	// garbage in, honest zero out (never a NaN into the action value)
	const float nan = std::numeric_limits<float>::quiet_NaN();
	CHECK(InputActionMap::applyDeadzone(nan, 0.25f) == Approx(0.0f));
	CHECK(InputActionMap::applyDeadzone(0.5f, nan) == Approx(0.0f));
}

//---------------------------------------------------------
TEST_CASE("InputManager: injected gamepad events feed the pad state",
	"[input][gamepad]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);

	// nothing injected yet: no pad, no button, no axis
	CHECK(input.getGamepadCount() == 0);
	CHECK_FALSE(input.isGamepadConnected());
	CHECK_FALSE(input.isGamepadButtonDown(Gamepad::GB_SOUTH));
	CHECK(input.getGamepadAxis(Gamepad::GA_LEFTX) == Approx(0.0f));

	// a pad that SPEAKS is a pad that is there (the injected-input rule: a
	// synthetic device is as real as a plugged-in one)
	REQUIRE(input.injectGamepadButton(Gamepad::GB_SOUTH, true));
	CHECK(input.isGamepadButtonDown(Gamepad::GB_SOUTH));
	CHECK(input.isGamepadConnected());
	CHECK(input.getGamepadCount() == 1);
	CHECK_FALSE(input.isGamepadButtonDown(Gamepad::GB_NORTH));

	REQUIRE(input.injectGamepadButton(Gamepad::GB_SOUTH, false));
	CHECK_FALSE(input.isGamepadButtonDown(Gamepad::GB_SOUTH));

	// axes round-trip through SDL's signed-short units within its resolution
	REQUIRE(input.injectGamepadAxis(Gamepad::GA_LEFTX, -0.5f));
	CHECK(input.getGamepadAxis(Gamepad::GA_LEFTX) == Approx(-0.5f).margin(1e-3));
	REQUIRE(input.injectGamepadAxis(Gamepad::GA_LEFTY, 1.0f));
	CHECK(input.getGamepadAxis(Gamepad::GA_LEFTY) == Approx(1.0f).margin(1e-3));
	// a trigger never reads negative
	REQUIRE(input.injectGamepadAxis(Gamepad::GA_LEFTTRIGGER, -1.0f));
	CHECK(input.getGamepadAxis(Gamepad::GA_LEFTTRIGGER) == Approx(0.0f));
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: the defaults answer a controller too",
	"[input][gamepad]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);
	InputActionMap actions;	// the built-in defaults

	// the bottom face button IS "jump" with zero authoring
	input.injectGamepadButton(Gamepad::GB_SOUTH, true);
	actions.update(1.0f / 60.0f);
	CHECK(actions.down("jump"));
	CHECK(actions.pressed("jump"));
	input.injectGamepadButton(Gamepad::GB_SOUTH, false);
	actions.update(1.0f / 60.0f);
	CHECK(actions.released("jump"));

	// the left stick IS "move" - and the deadzone eats the resting slop
	input.injectGamepadAxis(Gamepad::GA_LEFTX, 0.1f);
	actions.update(1.0f / 60.0f);
	CHECK(actions.value2("move").x == Approx(0.0f));
	input.injectGamepadAxis(Gamepad::GA_LEFTX, 1.0f);
	input.injectGamepadAxis(Gamepad::GA_LEFTY, -1.0f);
	actions.update(1.0f / 60.0f);
	CHECK(actions.value2("move").x == Approx(1.0f).margin(1e-3));
	// a stick's +y is DOWN, matching the key axis (W/UP is negative)
	CHECK(actions.value2("move").y == Approx(-1.0f).margin(1e-3));

	// the dpad drives the digital menu actions
	input.injectGamepadAxis(Gamepad::GA_LEFTX, 0.0f);
	input.injectGamepadAxis(Gamepad::GA_LEFTY, 0.0f);
	input.injectGamepadButton(Gamepad::GB_DPAD_RIGHT, true);
	input.injectGamepadButton(Gamepad::GB_START, true);
	actions.update(1.0f / 60.0f);
	CHECK(actions.down("menu_right"));
	CHECK(actions.down("menu_toggle"));
	CHECK_FALSE(actions.down("menu_left"));
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: keys and a stick combine by max magnitude",
	"[input][gamepad]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);
	InputActionMap actions;

	// the multi-binding contract, across DEVICE KINDS: whichever source pushes
	// harder owns the component - no special case for "a controller is present"
	input.injectGamepadAxis(Gamepad::GA_LEFTX, 0.5f);
	actions.update(1.0f / 60.0f);
	const float stickOnly = actions.value2("move").x;
	CHECK(stickOnly > 0.2f);
	CHECK(stickOnly < 1.0f);

	// a held key is a full +-1 and outranks a half-pushed stick
	injectKey(input, SDL_SCANCODE_LEFT, true);
	actions.update(1.0f / 60.0f);
	CHECK(actions.value2("move").x == Approx(-1.0f));

	// release the key: the stick is heard again, unchanged
	injectKey(input, SDL_SCANCODE_LEFT, false);
	actions.update(1.0f / 60.0f);
	CHECK(actions.value2("move").x == Approx(stickOnly));

	// full stick deflection ties with a key press - the incoming candidate
	// wins a tie, so the value stays +-1 either way
	input.injectGamepadAxis(Gamepad::GA_LEFTX, 1.0f);
	actions.update(1.0f / 60.0f);
	CHECK(actions.value2("move").x == Approx(1.0f).margin(1e-3));
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: gamepad bindings round-trip through .oactions",
	"[input][gamepad]")
{
	EngineTestEnvironment::get();

	InputManager input(false, false);
	const std::filesystem::path path =
		std::filesystem::temp_directory_path() /
		"orkige_gamepad_roundtrip.oactions";

	// an authored action carrying BOTH new binding shapes, with NON-default
	// axis / deadzone / invert so a field the codec drops cannot pass unnoticed
	InputAction authored;
	authored.name = "aim";
	authored.kind = InputActionKind::Analog2D;
	{
		InputActionBinding axis;
		axis.type = InputActionBinding::GamepadAxis;
		axis.gamepadAxis = Gamepad::GA_RIGHTY;
		axis.deadzone = 0.5f;
		axis.invert = true;
		axis.outputComponent = 1;
		authored.bindings.push_back(axis);
		InputActionBinding button;
		button.type = InputActionBinding::GamepadButton;
		button.gamepadButtons =
			{ Gamepad::GB_RIGHTSHOULDER, Gamepad::GB_NORTH };
		button.outputComponent = 0;
		authored.bindings.push_back(button);
	}

	{
		InputActionMap saved;
		saved.setAction(authored);
		REQUIRE(saved.saveActions(path.string()));
	}

	InputActionMap loaded;
	REQUIRE(loaded.loadActions(path.string()));
	REQUIRE(loaded.hasAction("aim"));

	// the proof is BEHAVIOUR, not bytes: the reloaded bindings must read the
	// same axis, honour the same deadzone and still be inverted
	input.injectGamepadAxis(Gamepad::GA_RIGHTY, 0.4f);	// inside the 0.5 zone
	loaded.update(1.0f / 60.0f);
	CHECK(loaded.value2("aim").y == Approx(0.0f));
	input.injectGamepadAxis(Gamepad::GA_RIGHTY, 1.0f);
	loaded.update(1.0f / 60.0f);
	CHECK(loaded.value2("aim").y == Approx(-1.0f).margin(1e-3));	// inverted
	// the OTHER stick axis is not what this binding reads
	input.injectGamepadAxis(Gamepad::GA_RIGHTY, 0.0f);
	input.injectGamepadAxis(Gamepad::GA_LEFTY, 1.0f);
	loaded.update(1.0f / 60.0f);
	CHECK(loaded.value2("aim").y == Approx(0.0f));
	// either listed button reads +1 on the x component
	input.injectGamepadButton(Gamepad::GB_NORTH, true);
	loaded.update(1.0f / 60.0f);
	CHECK(loaded.value2("aim").x == Approx(1.0f));
	input.injectGamepadButton(Gamepad::GB_NORTH, false);
	input.injectGamepadButton(Gamepad::GB_RIGHTSHOULDER, true);
	loaded.update(1.0f / 60.0f);
	CHECK(loaded.value2("aim").x == Approx(1.0f));
	input.injectGamepadButton(Gamepad::GB_RIGHTSHOULDER, false);
	loaded.update(1.0f / 60.0f);
	CHECK(loaded.value2("aim").x == Approx(0.0f));

	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

//---------------------------------------------------------
TEST_CASE("InputActionMap: an .oactions of another version is refused",
	"[input]")
{
	EngineTestEnvironment::get();

	const std::filesystem::path path =
		std::filesystem::temp_directory_path() / "orkige_oldversion.oactions";
	{
		optr<XMLArchive> ar = onew(new XMLArchive());
		REQUIRE(ar->startWriting(path.string()));
		ar << InputActionMap::ACTIONS_FILE_MAGIC;
		int version = InputActionMap::ACTIONS_FORMAT_VERSION - 1;
		ar << version;
		unsigned int actionCount = 0;
		ar << actionCount;
		REQUIRE(ar->stopWriting());
	}

	InputActionMap actions;
	// clean cutover: an older file is refused by name, and the refusal is
	// NON-DESTRUCTIVE - the live set is untouched
	CHECK_FALSE(actions.loadActions(path.string()));
	CHECK(actions.hasAction("move"));

	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

//---------------------------------------------------------
//--- the raw pointer / touch frame snapshot --------------
//---------------------------------------------------------
TEST_CASE("InputManager: the touch snapshot reports began/moved/ended once",
	"[input][touch]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);
	input.setWindowExtents(800, 600);

	// nothing touched yet
	input.updateFrameState();
	CHECK(input.getTouchCount() == 0);

	// a finger lands: BEGAN for exactly one frame, no phantom delta
	REQUIRE(input.injectTouch(0, TP_BEGAN, 100.0f, 200.0f));
	input.updateFrameState();
	REQUIRE(input.getTouchCount() == 1);
	TouchPoint point = input.getTouchPoint(0);
	CHECK(point.phase == TP_BEGAN);
	CHECK(point.x == Approx(100.0f).margin(0.5));
	CHECK(point.y == Approx(200.0f).margin(0.5));
	CHECK(point.deltaX == Approx(0.0f).margin(0.5));

	// held without moving: MOVED, zero delta
	input.updateFrameState();
	REQUIRE(input.getTouchCount() == 1);
	CHECK(input.getTouchPoint(0).phase == TP_MOVED);
	CHECK(input.getTouchPoint(0).deltaX == Approx(0.0f).margin(0.5));

	// dragged: the delta is measured against the PREVIOUS frame
	REQUIRE(input.injectTouch(0, TP_MOVED, 160.0f, 200.0f));
	input.updateFrameState();
	CHECK(input.getTouchPoint(0).phase == TP_MOVED);
	CHECK(input.getTouchPoint(0).x == Approx(160.0f).margin(0.5));
	CHECK(input.getTouchPoint(0).deltaX == Approx(60.0f).margin(0.5));

	// lifted: reported ONE more time as ENDED, then gone
	REQUIRE(input.injectTouch(0, TP_ENDED, 160.0f, 200.0f));
	input.updateFrameState();
	REQUIRE(input.getTouchCount() == 1);
	CHECK(input.getTouchPoint(0).phase == TP_ENDED);
	input.updateFrameState();
	CHECK(input.getTouchCount() == 0);
}

//---------------------------------------------------------
TEST_CASE("InputManager: a one-frame tap is never swallowed", "[input][touch]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);
	input.setWindowExtents(800, 600);

	// down AND up between two snapshots: the frame still reads BEGAN, and the
	// release is published next frame - a fast tap cannot vanish
	REQUIRE(input.injectTouch(3, TP_BEGAN, 10.0f, 20.0f));
	REQUIRE(input.injectTouch(3, TP_ENDED, 10.0f, 20.0f));
	input.updateFrameState();
	REQUIRE(input.getTouchCount() == 1);
	CHECK(input.getTouchPoint(0).phase == TP_BEGAN);
	input.updateFrameState();
	REQUIRE(input.getTouchCount() == 1);
	CHECK(input.getTouchPoint(0).phase == TP_ENDED);
	input.updateFrameState();
	CHECK(input.getTouchCount() == 0);
}

//---------------------------------------------------------
TEST_CASE("InputManager: two fingers are tracked independently",
	"[input][touch]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);
	input.setWindowExtents(1000, 500);

	REQUIRE(input.injectTouch(0, TP_BEGAN, 100.0f, 100.0f));
	REQUIRE(input.injectTouch(1, TP_BEGAN, 900.0f, 400.0f));
	input.updateFrameState();
	REQUIRE(input.getTouchCount() == 2);
	// distinct ids, each at its own position
	CHECK(input.getTouchPoint(0).id != input.getTouchPoint(1).id);
	CHECK(input.getTouchPoint(0).x == Approx(100.0f).margin(1.0));
	CHECK(input.getTouchPoint(1).x == Approx(900.0f).margin(1.0));

	// one lifts, the other stays
	REQUIRE(input.injectTouch(0, TP_ENDED, 100.0f, 100.0f));
	input.updateFrameState();
	CHECK(input.getTouchCount() == 2);	// the lift is still reported once
	input.updateFrameState();
	REQUIRE(input.getTouchCount() == 1);
	CHECK(input.getTouchPoint(0).x == Approx(900.0f).margin(1.0));
}

//---------------------------------------------------------
TEST_CASE("InputManager: the pointer snapshot carries position and edges",
	"[input][touch]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);

	injectPointerButton(input, SDL_BUTTON_LEFT, true, 320.0f, 240.0f);
	input.updateFrameState();
	CHECK(input.getPointerPosition().x == Approx(320.0f));
	CHECK(input.getPointerPosition().y == Approx(240.0f));
	CHECK(input.isPointerDown(MouseEventData::MB_Left));
	CHECK(input.isPointerPressed(MouseEventData::MB_Left));
	CHECK_FALSE(input.isPointerReleased(MouseEventData::MB_Left));
	CHECK_FALSE(input.isPointerDown(MouseEventData::MB_Right));

	// held: down stays, the press edge is one frame only
	input.updateFrameState();
	CHECK(input.isPointerDown(MouseEventData::MB_Left));
	CHECK_FALSE(input.isPointerPressed(MouseEventData::MB_Left));

	injectPointerButton(input, SDL_BUTTON_LEFT, false, 320.0f, 240.0f);
	input.updateFrameState();
	CHECK_FALSE(input.isPointerDown(MouseEventData::MB_Left));
	CHECK(input.isPointerReleased(MouseEventData::MB_Left));

	// a click that opens and closes inside one frame still reports BOTH edges
	injectPointerButton(input, SDL_BUTTON_RIGHT, true, 10.0f, 10.0f);
	injectPointerButton(input, SDL_BUTTON_RIGHT, false, 10.0f, 10.0f);
	input.updateFrameState();
	CHECK(input.isPointerPressed(MouseEventData::MB_Right));
	CHECK(input.isPointerReleased(MouseEventData::MB_Right));
	CHECK_FALSE(input.isPointerDown(MouseEventData::MB_Right));
}
