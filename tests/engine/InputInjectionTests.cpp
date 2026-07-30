/**************************************************************
	created:	2026/07/30 at 09:40
	filename: 	InputInjectionTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for AGENT-DRIVEN INPUT: the key-name vocabulary
	(KeyCodeNames), the step-list grammar and its frame arithmetic
	(InputInjection::compile) and the tilt vector -> angle inverse. These are
	the pure decisions behind the send_input MCP verb: every refusal is a
	test, so a malformed gesture can never reach a running game as a silent
	no-op. The wire path is covered by the editor_control send_input leg and
	the player_debug_pause driver; the gameplay proof is editor_agent_loop.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "EngineTestEnvironment.h"

#include <engine_input/InputInjection.h>
#include <engine_input/InputManager.h>

#include <algorithm>
#include <cmath>

using Catch::Approx;
using namespace Orkige;

namespace
{
	//! compile a step list, requiring success, and hand back the sequence
	InputInjection::Sequence compileOk(StringVector const & steps)
	{
		InputInjection::Sequence sequence;
		String error;
		const bool ok = InputInjection::compile(steps, sequence, error);
		CHECK(error.empty());
		REQUIRE(ok);
		return sequence;
	}

	//! compile a step list, requiring a refusal, and hand back the reason
	String compileFails(StringVector const & steps)
	{
		InputInjection::Sequence sequence;
		String error;
		const bool ok = InputInjection::compile(steps, sequence, error);
		CHECK_FALSE(ok);
		CHECK_FALSE(error.empty());
		CHECK(sequence.events.empty());
		CHECK(sequence.frameSpan == 0u);
		return error;
	}

	//! the events stamped for one frame, in emission order
	std::vector<InputInjection::Event> eventsAt(
		InputInjection::Sequence const & sequence, unsigned int frame)
	{
		std::vector<InputInjection::Event> out;
		for (InputInjection::Event const & each : sequence.events)
		{
			if (each.frame == frame)
			{
				out.push_back(each);
			}
		}
		return out;
	}
}

//--- KeyCodeNames: the name vocabulary --------------------------------------

TEST_CASE("key names resolve case-insensitively, with or without the KC_ prefix",
	"[unit][input][injection]")
{
	CHECK(KeyCodeNames::fromName("SPACE") == KeyEventData::KC_SPACE);
	CHECK(KeyCodeNames::fromName("space") == KeyEventData::KC_SPACE);
	CHECK(KeyCodeNames::fromName("Space") == KeyEventData::KC_SPACE);
	CHECK(KeyCodeNames::fromName("KC_SPACE") == KeyEventData::KC_SPACE);
	CHECK(KeyCodeNames::fromName("kc_space") == KeyEventData::KC_SPACE);
	CHECK(KeyCodeNames::fromName("RIGHT") == KeyEventData::KC_RIGHT);
	CHECK(KeyCodeNames::fromName("d") == KeyEventData::KC_D);
	CHECK(KeyCodeNames::fromName("7") == KeyEventData::KC_7);
	CHECK(KeyCodeNames::fromName("F11") == KeyEventData::KC_F11);
	// the Android hardware back button is a key like any other
	CHECK(KeyCodeNames::fromName("WEBBACK") == KeyEventData::KC_WEBBACK);
}

TEST_CASE("friendly key aliases resolve to their canonical code",
	"[unit][input][injection]")
{
	CHECK(KeyCodeNames::fromName("ENTER") == KeyEventData::KC_RETURN);
	CHECK(KeyCodeNames::fromName("esc") == KeyEventData::KC_ESCAPE);
	CHECK(KeyCodeNames::fromName("BACKSPACE") == KeyEventData::KC_BACK);
	CHECK(KeyCodeNames::fromName("shift") == KeyEventData::KC_LSHIFT);
	CHECK(KeyCodeNames::fromName("CTRL") == KeyEventData::KC_LCONTROL);
	CHECK(KeyCodeNames::fromName("alt") == KeyEventData::KC_LMENU);
	// an alias is NOT a canonical name: allNames lists one spelling per key
	StringVector const names = KeyCodeNames::allNames();
	CHECK(std::find(names.begin(), names.end(), String("ENTER")) == names.end());
	CHECK(std::find(names.begin(), names.end(), String("RETURN")) != names.end());
}

TEST_CASE("an unknown key name resolves to KC_UNASSIGNED, never to a guess",
	"[unit][input][injection]")
{
	CHECK(KeyCodeNames::fromName("") == KeyEventData::KC_UNASSIGNED);
	CHECK(KeyCodeNames::fromName("KC_") == KeyEventData::KC_UNASSIGNED);
	CHECK(KeyCodeNames::fromName("JUMP") == KeyEventData::KC_UNASSIGNED);
	CHECK(KeyCodeNames::fromName("SPACEBAR") == KeyEventData::KC_UNASSIGNED);
	// an exotic code deliberately outside the table stays unreachable by name
	CHECK(KeyCodeNames::fromName("KANJI") == KeyEventData::KC_UNASSIGNED);
}

TEST_CASE("every canonical name round-trips through toName",
	"[unit][input][injection]")
{
	StringVector const names = KeyCodeNames::allNames();
	REQUIRE_FALSE(names.empty());
	for (String const & name : names)
	{
		const KeyEventData::KeyCode key = KeyCodeNames::fromName(name);
		CHECK(key != KeyEventData::KC_UNASSIGNED);
		CHECK(KeyCodeNames::toName(key) == name);
	}
	CHECK(KeyCodeNames::toName(KeyEventData::KC_UNASSIGNED).empty());
	CHECK(KeyCodeNames::toName(KeyEventData::KC_KANJI).empty());
}

//--- the step grammar -------------------------------------------------------

TEST_CASE("a key down/up pair compiles to two events on the same frame",
	"[unit][input][injection]")
{
	InputInjection::Sequence const sequence =
		compileOk({ "key down SPACE", "key up SPACE" });
	REQUIRE(sequence.events.size() == 2);
	CHECK(sequence.frameSpan == 1u);
	CHECK(sequence.events[0].frame == 0u);
	CHECK(sequence.events[0].kind == InputInjection::EventKind::KeyDown);
	CHECK(sequence.events[0].key == KeyEventData::KC_SPACE);
	CHECK(sequence.events[1].frame == 0u);
	CHECK(sequence.events[1].kind == InputInjection::EventKind::KeyUp);
}

TEST_CASE("'key press' holds for the requested number of frames",
	"[unit][input][injection]")
{
	// the load-bearing arithmetic: "press RIGHT for 3 frames" must leave the
	// key DOWN across frames 0,1,2 and release on frame 3 - three ticks of
	// gameplay see it held, and the span covers the release frame
	InputInjection::Sequence const sequence =
		compileOk({ "key press RIGHT 3" });
	REQUIRE(sequence.events.size() == 2);
	CHECK(sequence.events[0].kind == InputInjection::EventKind::KeyDown);
	CHECK(sequence.events[0].frame == 0u);
	CHECK(sequence.events[1].kind == InputInjection::EventKind::KeyUp);
	CHECK(sequence.events[1].frame == 3u);
	CHECK(sequence.frameSpan == 4u);
}

TEST_CASE("'key press' without a frame count holds exactly one frame",
	"[unit][input][injection]")
{
	InputInjection::Sequence const sequence = compileOk({ "key press SPACE" });
	REQUIRE(sequence.events.size() == 2);
	CHECK(sequence.events[0].frame == 0u);
	CHECK(sequence.events[1].frame == 1u);
	CHECK(sequence.frameSpan == 2u);
}

TEST_CASE("'wait' advances the timeline without emitting an event",
	"[unit][input][injection]")
{
	InputInjection::Sequence const sequence = compileOk(
		{ "key down A", "wait 5", "key up A" });
	REQUIRE(sequence.events.size() == 2);
	CHECK(sequence.events[0].frame == 0u);
	CHECK(sequence.events[1].frame == 5u);
	CHECK(sequence.frameSpan == 6u);
}

TEST_CASE("consecutive held presses stack up along the timeline",
	"[unit][input][injection]")
{
	// two presses in a row: the second must start where the first released,
	// so a scripted gesture reads in the order it was written
	InputInjection::Sequence const sequence = compileOk(
		{ "key press RIGHT 2", "key press SPACE 4" });
	REQUIRE(sequence.events.size() == 4);
	CHECK(sequence.events[0].key == KeyEventData::KC_RIGHT);
	CHECK(sequence.events[0].frame == 0u);
	CHECK(sequence.events[1].key == KeyEventData::KC_RIGHT);
	CHECK(sequence.events[1].frame == 2u);
	CHECK(sequence.events[2].key == KeyEventData::KC_SPACE);
	CHECK(sequence.events[2].frame == 2u);
	CHECK(sequence.events[3].key == KeyEventData::KC_SPACE);
	CHECK(sequence.events[3].frame == 6u);
	CHECK(sequence.frameSpan == 7u);
	// frame order: the runtime walks the timeline once, so events must be sorted
	for (std::size_t i = 1; i < sequence.events.size(); ++i)
	{
		CHECK(sequence.events[i - 1].frame <= sequence.events[i].frame);
	}
}

TEST_CASE("pointer steps carry window pixels and a button",
	"[unit][input][injection]")
{
	InputInjection::Sequence const sequence = compileOk(
		{ "pointer move 100 200",
		  "pointer down 100 200",
		  "pointer up 100 200 right" });
	REQUIRE(sequence.events.size() == 3);
	CHECK(sequence.events[0].kind == InputInjection::EventKind::PointerMove);
	CHECK(sequence.events[0].x == Approx(100.0f));
	CHECK(sequence.events[0].y == Approx(200.0f));
	CHECK(sequence.events[1].kind == InputInjection::EventKind::PointerDown);
	CHECK(sequence.events[1].button == InputInjection::PointerButton::Left);
	CHECK(sequence.events[2].kind == InputInjection::EventKind::PointerUp);
	CHECK(sequence.events[2].button == InputInjection::PointerButton::Right);
	CHECK(sequence.frameSpan == 1u);
}

TEST_CASE("'pointer click' expands to move + press held one frame + release",
	"[unit][input][injection]")
{
	InputInjection::Sequence const sequence =
		compileOk({ "pointer click 320.5 240" });
	REQUIRE(sequence.events.size() == 3);
	std::vector<InputInjection::Event> const frame0 = eventsAt(sequence, 0);
	REQUIRE(frame0.size() == 2);
	CHECK(frame0[0].kind == InputInjection::EventKind::PointerMove);
	CHECK(frame0[1].kind == InputInjection::EventKind::PointerDown);
	CHECK(frame0[1].x == Approx(320.5f));
	std::vector<InputInjection::Event> const frame1 = eventsAt(sequence, 1);
	REQUIRE(frame1.size() == 1);
	CHECK(frame1[0].kind == InputInjection::EventKind::PointerUp);
	CHECK(sequence.frameSpan == 2u);
}

TEST_CASE("tilt steps compile to the SIMULATION angle, both spellings",
	"[unit][input][injection]")
{
	InputInjection::Sequence const byAngle =
		compileOk({ "tilt angle 0.4" });
	REQUIRE(byAngle.events.size() == 1);
	CHECK(byAngle.events[0].kind == InputInjection::EventKind::TiltAngle);
	CHECK(byAngle.events[0].angle == Approx(0.4f));

	// a gravity VECTOR is the same seam, expressed the way a game reads it:
	// upright is (0,-1) and must compile to angle 0
	InputInjection::Sequence const upright =
		compileOk({ "tilt vector 0 -1" });
	REQUIRE(upright.events.size() == 1);
	CHECK(upright.events[0].angle == Approx(0.0f));
}

TEST_CASE("tiltAngleFromVector inverts InputManager::tiltVectorFromAngle",
	"[unit][input][injection]")
{
	// the two spellings MUST agree: a vector step and an angle step drive the
	// one simulated tilt, so the round trip has to be exact
	const float angles[] = { 0.0f, 0.25f, -0.25f, 0.9f, -1.1f };
	for (float angle : angles)
	{
		Vec3 const vector = InputManager::tiltVectorFromAngle(angle);
		CHECK(InputInjection::tiltAngleFromVector(vector.x, vector.y) ==
			Approx(angle).margin(1e-5));
	}
	// a zero vector has no direction: upright, not a NaN
	CHECK(InputInjection::tiltAngleFromVector(0.0f, 0.0f) == Approx(0.0f));
	CHECK(std::isfinite(InputInjection::tiltAngleFromVector(
		std::nanf(""), 1.0f)));
}

//--- every refusal is a test ------------------------------------------------

TEST_CASE("an empty or event-free step list is refused, not silently accepted",
	"[unit][input][injection]")
{
	CHECK(compileFails({}).find("no input steps") != String::npos);
	// a gesture that only waits does nothing - a mistake, not a no-op
	CHECK(compileFails({ "wait 3" }).find("no input events") != String::npos);
	CHECK_FALSE(compileFails({ "" }).empty());
}

TEST_CASE("a malformed step is refused with its 1-based index in the reason",
	"[unit][input][injection]")
{
	CHECK(compileFails({ "key down SPACE", "jump" }).find("step 2") !=
		String::npos);
	CHECK(compileFails({ "key down SPACE", "key sideways SPACE" })
		.find("step 2") != String::npos);
	CHECK(compileFails({ "key down NOSUCHKEY" }).find("NOSUCHKEY") !=
		String::npos);
	CHECK(compileFails({ "key down" }).find("step 1") != String::npos);
	// a stray extra token is a typo, not something to guess about
	CHECK_FALSE(compileFails({ "key down SPACE extra" }).empty());
}

TEST_CASE("non-numeric or non-positive counts and coordinates are refused",
	"[unit][input][injection]")
{
	CHECK_FALSE(compileFails({ "wait 0" }).empty());
	CHECK_FALSE(compileFails({ "wait -1" }).empty());
	CHECK_FALSE(compileFails({ "wait soon" }).empty());
	CHECK_FALSE(compileFails({ "key press SPACE 0" }).empty());
	CHECK_FALSE(compileFails({ "key press SPACE lots" }).empty());
	CHECK_FALSE(compileFails({ "pointer move left 20" }).empty());
	CHECK_FALSE(compileFails({ "pointer move 10" }).empty());
	CHECK_FALSE(compileFails({ "pointer down 10 20 thumb" }).empty());
	CHECK_FALSE(compileFails({ "tilt angle" }).empty());
	CHECK_FALSE(compileFails({ "tilt angle sideways" }).empty());
	CHECK_FALSE(compileFails({ "tilt vector 1" }).empty());
	CHECK_FALSE(compileFails({ "tilt sideways 1" }).empty());
}

TEST_CASE("a gesture past the frame or step bound is refused",
	"[unit][input][injection]")
{
	// the bounds exist so ONE request can never occupy a play session
	StringVector tooLong;
	tooLong.push_back("key down SPACE");
	tooLong.push_back("wait " + std::to_string(InputInjection::MAX_FRAMES));
	tooLong.push_back("key up SPACE");
	CHECK(compileFails(tooLong).find("frames") != String::npos);

	StringVector tooMany;
	for (unsigned int i = 0; i <= InputInjection::MAX_STEPS; ++i)
	{
		tooMany.push_back("key down SPACE");
	}
	CHECK(compileFails(tooMany).find("too many") != String::npos);
}

TEST_CASE("the grammar tolerates extra whitespace and mixed case verbs",
	"[unit][input][injection]")
{
	InputInjection::Sequence const sequence = compileOk(
		{ "  KEY   Press   right   2  ", "\tWait 1", "Pointer CLICK 5 6" });
	CHECK(sequence.events.size() == 5);
	CHECK(sequence.events[0].key == KeyEventData::KC_RIGHT);
}
