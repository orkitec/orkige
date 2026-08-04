/**************************************************************
	created:	2026/08/04 at 10:10
	filename: 	InputTestDriveTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The test tier's input driver: the pure target grammar (an action name, an
	axis direction, a raw key name, and every refusal), the held-key ledger,
	and - the load-bearing one - WHERE a press has to land in the canonical
	tick order to be seen. A test body is resumed in the SCRIPT phase, after
	the input slot that takes the action map's once-per-frame edge snapshot,
	so a press made there must show up as pressed() in the NEXT frame's slot,
	before the game scripts of that frame run. Injected any later and
	`t.press("jump")` would silently never press: a test that looks right and
	proves nothing. These cases drive advanceGameWorld itself, so the
	assertion is about the real tick order rather than a restatement of it.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "EngineTestEnvironment.h"

#include <engine_input/InputActionMap.h>
#include <engine_input/InputManager.h>
#include <engine_input/InputTestDrive.h>
#include <engine_runtime/GameHost.h>

#include <vector>

using namespace Orkige;

namespace
{
	const float FRAME_SECONDS = 1.0f / 60.0f;

	//! one action binding shorthand: a digital button on one key
	InputAction digital(String const & name, KeyEventData::KeyCode key)
	{
		InputAction action;
		action.name = name;
		action.kind = InputActionKind::Digital;
		InputActionBinding binding;
		binding.type = InputActionBinding::Key;
		binding.keys.push_back(key);
		action.bindings.push_back(binding);
		return action;
	}

	//! a key-axis binding on one component
	InputActionBinding keyAxis(KeyEventData::KeyCode negative,
		KeyEventData::KeyCode positive, int component)
	{
		InputActionBinding binding;
		binding.type = InputActionBinding::KeyAxis;
		binding.negativeKeys.push_back(negative);
		binding.positiveKeys.push_back(positive);
		binding.outputComponent = component;
		return binding;
	}

	//! an action nothing on a keyboard can push
	InputAction tiltOnly(String const & name)
	{
		InputAction action;
		action.name = name;
		action.kind = InputActionKind::Analog1D;
		InputActionBinding binding;
		binding.type = InputActionBinding::TiltAxis;
		binding.tiltComponent = 0;
		binding.outputComponent = 0;
		action.bindings.push_back(binding);
		return action;
	}
}

//---------------------------------------------------------
TEST_CASE("InputTestDrive: a target splits into a name and a direction",
	"[input][testdrive]")
{
	String name;
	String direction;
	InputTestDrive::splitTarget("move+x", name, direction);
	CHECK(name == "move");
	CHECK(direction == "+x");
	InputTestDrive::splitTarget("move-y", name, direction);
	CHECK(name == "move");
	CHECK(direction == "-y");
	// upper case reads the same (key names are spelled loudly, so a target
	// often is too)
	InputTestDrive::splitTarget("move+X", name, direction);
	CHECK(name == "move");
	CHECK(direction == "+X");
	// no suffix: the whole target is the name
	InputTestDrive::splitTarget("jump", name, direction);
	CHECK(name == "jump");
	CHECK(direction.empty());
	InputTestDrive::splitTarget("SPACE", name, direction);
	CHECK(name == "SPACE");
	CHECK(direction.empty());
	// a name that merely ENDS in something suffix-like is not cut: only the
	// four component directions are suffixes
	InputTestDrive::splitTarget("relax", name, direction);
	CHECK(name == "relax");
	CHECK(direction.empty());
	// a bare direction has nothing to steer - kept whole so the refusal
	// quotes what the caller actually wrote
	InputTestDrive::splitTarget("+x", name, direction);
	CHECK(name == "+x");
	CHECK(direction.empty());
}

//---------------------------------------------------------
TEST_CASE("InputTestDrive: pressing an action means its key binding",
	"[input][testdrive]")
{
	const InputAction jump = digital("jump", KeyEventData::KC_SPACE);
	const InputTestDrive::Target target =
		InputTestDrive::resolveKeys(&jump, "jump", "");
	REQUIRE(target.ok());
	CHECK(target.key == KeyEventData::KC_SPACE);
}

//---------------------------------------------------------
TEST_CASE("InputTestDrive: a direction picks the keys that push that way",
	"[input][testdrive]")
{
	InputAction move;
	move.name = "move";
	move.kind = InputActionKind::Analog2D;
	move.bindings.push_back(keyAxis(KeyEventData::KC_A, KeyEventData::KC_D, 0));
	move.bindings.push_back(keyAxis(KeyEventData::KC_W, KeyEventData::KC_S, 1));

	CHECK(InputTestDrive::resolveKeys(&move, "move", "+x").key ==
		KeyEventData::KC_D);
	CHECK(InputTestDrive::resolveKeys(&move, "move", "-x").key ==
		KeyEventData::KC_A);
	CHECK(InputTestDrive::resolveKeys(&move, "move", "+y").key ==
		KeyEventData::KC_S);
	CHECK(InputTestDrive::resolveKeys(&move, "move", "-y").key ==
		KeyEventData::KC_W);
	// an axis action carries no single "press it" key: the refusal says so
	// and points at the directional spelling instead of pressing something
	// arbitrary
	const InputTestDrive::Target whole =
		InputTestDrive::resolveKeys(&move, "move", "");
	CHECK_FALSE(whole.ok());
	CHECK(whole.error.find("move+x") != String::npos);
}

//---------------------------------------------------------
TEST_CASE("InputTestDrive: a key name answers when no action does",
	"[input][testdrive]")
{
	const InputTestDrive::Target space =
		InputTestDrive::resolveKeys(NULL, "SPACE", "");
	REQUIRE(space.ok());
	CHECK(space.key == KeyEventData::KC_SPACE);
	// the key vocabulary is KeyCodeNames', case-insensitive and KC_-tolerant
	CHECK(InputTestDrive::resolveKeys(NULL, "right", "").key ==
		KeyEventData::KC_RIGHT);
	CHECK(InputTestDrive::resolveKeys(NULL, "KC_RETURN", "").key ==
		KeyEventData::KC_RETURN);
}

//---------------------------------------------------------
TEST_CASE("InputTestDrive: an unpressable target is refused by name",
	"[input][testdrive]")
{
	// a typo is a NAMED refusal, never a silent no-press - a test that
	// presses nothing and passes is worse than no test
	const InputTestDrive::Target unknown =
		InputTestDrive::resolveKeys(NULL, "jmup", "");
	CHECK_FALSE(unknown.ok());
	CHECK(unknown.error.find("jmup") != String::npos);
	// a direction only applies to an action
	const InputTestDrive::Target keyDirection =
		InputTestDrive::resolveKeys(NULL, "SPACE", "+x");
	CHECK_FALSE(keyDirection.ok());
	// an action bound to tilt alone has no key at all, and the reason says
	// what it IS bound to
	const InputAction steer = tiltOnly("steer");
	const InputTestDrive::Target tilt =
		InputTestDrive::resolveKeys(&steer, "steer", "+x");
	CHECK_FALSE(tilt.ok());
	CHECK(tilt.error.find("tilt") != String::npos);
}

//---------------------------------------------------------
TEST_CASE("InputTestDrive: the held ledger makes every edge exactly one",
	"[input][testdrive]")
{
	InputTestDrive::HeldKeys held;
	CHECK(held.empty());
	// the first hold is the one that injects a down edge
	CHECK(held.hold(KeyEventData::KC_SPACE));
	// the second is not: two targets can name the same key ("jump" and
	// "SPACE"), and a keyboard never repeats a down without an up
	CHECK_FALSE(held.hold(KeyEventData::KC_SPACE));
	CHECK(held.holds(KeyEventData::KC_SPACE));
	CHECK(held.size() == 1);
	CHECK(held.hold(KeyEventData::KC_D));
	CHECK(held.size() == 2);
	// letting go of something never held injects nothing
	CHECK_FALSE(held.letGo(KeyEventData::KC_A));
	CHECK(held.letGo(KeyEventData::KC_SPACE));
	CHECK_FALSE(held.letGo(KeyEventData::KC_SPACE));
	// takeAll is the run's boundary: everything still held comes back once
	const std::vector<KeyEventData::KeyCode> remaining = held.takeAll();
	REQUIRE(remaining.size() == 1);
	CHECK(remaining.front() == KeyEventData::KC_D);
	CHECK(held.empty());
	CHECK(held.takeAll().empty());
}

//---------------------------------------------------------
TEST_CASE("InputTestDriver: a press made in the script phase is pressed in "
	"the very next frame", "[input][testdrive]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);	// no native init (no sensor/window)
	InputActionMap actions;				// the constructor loads the defaults
	InputTestDriver driver;
	GameTick tick;
	tick.inputActions = &actions;

	// frame 1: nobody pressed anything
	advanceGameWorld(tick, FRAME_SECONDS);
	CHECK_FALSE(actions.down("jump"));
	CHECK_FALSE(actions.pressed("jump"));

	// THE SCRIPT PHASE of frame 1 is where a play-mode test body is resumed -
	// after this frame's input slot has already taken its snapshot. So a
	// press made here is NOT this frame's press...
	String error;
	REQUIRE(driver.press("jump", error));
	CHECK(error.empty());
	CHECK(actions.pressed("jump") == false);

	// ...it is the NEXT frame's, seen by the input slot before that frame's
	// game scripts run. This is the whole guarantee `t.press` rests on.
	advanceGameWorld(tick, FRAME_SECONDS);
	CHECK(actions.down("jump"));
	CHECK(actions.pressed("jump"));

	// held, but the edge is spent: pressed() is true for exactly one frame
	advanceGameWorld(tick, FRAME_SECONDS);
	CHECK(actions.down("jump"));
	CHECK_FALSE(actions.pressed("jump"));

	// and the release is an edge of its own, one frame later
	REQUIRE(driver.release("jump", error));
	advanceGameWorld(tick, FRAME_SECONDS);
	CHECK_FALSE(actions.down("jump"));
	CHECK(actions.released("jump"));
}

//---------------------------------------------------------
TEST_CASE("InputTestDriver: a one-frame tap is exactly one press edge",
	"[input][testdrive]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);
	InputActionMap actions;
	InputTestDriver driver;
	GameTick tick;
	tick.inputActions = &actions;
	String error;

	// t.tap("jump") in the script phase is: press, waitFrames(1), release -
	// so the release happens in the script phase of the frame whose input
	// slot already read the press. Not zero edges, and not two.
	int edges = 0;
	advanceGameWorld(tick, FRAME_SECONDS);
	edges += actions.pressed("jump") ? 1 : 0;

	REQUIRE(driver.press("jump", error));		// the tap's press
	advanceGameWorld(tick, FRAME_SECONDS);		// waitFrames(1) lands here
	edges += actions.pressed("jump") ? 1 : 0;

	REQUIRE(driver.release("jump", error));		// the tap's release
	for(int frame = 0; frame < 4; ++frame)
	{
		advanceGameWorld(tick, FRAME_SECONDS);
		edges += actions.pressed("jump") ? 1 : 0;
	}
	CHECK(edges == 1);
	CHECK_FALSE(actions.down("jump"));
}

//---------------------------------------------------------
TEST_CASE("InputTestDriver: a press released inside one script phase is never "
	"seen", "[input][testdrive]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);
	InputActionMap actions;
	InputTestDriver driver;
	GameTick tick;
	tick.inputActions = &actions;
	String error;

	// THIS is why t.tap waits a frame between its press and its release. The
	// action map snapshots edges ONCE per frame in the input slot; a key that
	// goes down and back up entirely within one script phase was never down
	// when that slot ran, so the game sees nothing at all. A tap that skipped
	// the wait would look right in the test file and prove nothing.
	REQUIRE(driver.press("jump", error));
	REQUIRE(driver.release("jump", error));
	int edges = 0;
	for(int frame = 0; frame < 4; ++frame)
	{
		advanceGameWorld(tick, FRAME_SECONDS);
		edges += actions.pressed("jump") ? 1 : 0;
	}
	CHECK(edges == 0);
}

//---------------------------------------------------------
TEST_CASE("InputTestDriver: a direction target drives the axis a game reads",
	"[input][testdrive]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);
	InputActionMap actions;
	InputTestDriver driver;
	GameTick tick;
	tick.inputActions = &actions;
	String error;

	// the reference game reads value2("move").x, so that is what a test that
	// walks the character has to move - through the action layer, not around it
	REQUIRE(driver.press("move+x", error));
	advanceGameWorld(tick, FRAME_SECONDS);
	CHECK(actions.value2("move").x > 0.5f);

	REQUIRE(driver.release("move+x", error));
	advanceGameWorld(tick, FRAME_SECONDS);
	CHECK(actions.value2("move").x == 0.0f);

	// the other way is the same target with the other sign
	REQUIRE(driver.press("move-x", error));
	advanceGameWorld(tick, FRAME_SECONDS);
	CHECK(actions.value2("move").x < -0.5f);
	driver.releaseAll();
	advanceGameWorld(tick, FRAME_SECONDS);
	CHECK(actions.value2("move").x == 0.0f);
}

//---------------------------------------------------------
TEST_CASE("InputTestDriver: releaseAll leaves nothing pressed for the next "
	"test", "[input][testdrive]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);
	InputActionMap actions;
	InputTestDriver driver;
	GameTick tick;
	tick.inputActions = &actions;
	String error;

	REQUIRE(driver.press("jump", error));
	REQUIRE(driver.press("move+x", error));
	CHECK(driver.anyHeld());
	advanceGameWorld(tick, FRAME_SECONDS);
	CHECK(actions.down("jump"));

	// a test that forgot to release must not press into the next one
	driver.releaseAll();
	CHECK_FALSE(driver.anyHeld());
	advanceGameWorld(tick, FRAME_SECONDS);
	CHECK_FALSE(actions.down("jump"));
	CHECK(actions.value2("move").x == 0.0f);
	CHECK_FALSE(input.isKeyDown(KeyEventData::KC_SPACE));
}

//---------------------------------------------------------
TEST_CASE("InputTestDriver: a refused target presses nothing",
	"[input][testdrive]")
{
	EngineTestEnvironment::get();
	InputManager input(false, false);
	InputActionMap actions;
	InputTestDriver driver;
	String error;

	CHECK_FALSE(driver.press("jmup", error));
	CHECK_FALSE(error.empty());
	CHECK_FALSE(driver.anyHeld());
	// pressing the same key twice is a no-op that still succeeds - the
	// ledger, not the caller, keeps the edges honest
	error.clear();
	REQUIRE(driver.press("jump", error));
	REQUIRE(driver.press("SPACE", error));
	CHECK(driver.anyHeld());
	driver.releaseAll();
	CHECK_FALSE(input.isKeyDown(KeyEventData::KC_SPACE));
}
