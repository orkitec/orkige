/********************************************************************
	created:	Sunday 2026/07/12 at 12:00
	filename: 	AppHostPolicyTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// AppHost policy tests - the pure, headless corners of the shared boot
// scaffold (engine_runtime/AppHost.h): the frame-delta clamp policy, the
// boot-option defaults and the shared quit-on-ESC listener. The windowed
// boot/teardown spine itself is covered by every host's selfcheck ctest.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <engine_runtime/AppHost.h>
#include <engine_input/KeyEventData.h>
#include <core_event/Event.h>

using Catch::Matchers::WithinAbs;

TEST_CASE("clampFrameDelta advances automated runs by a fixed 1/60 tick",
	"[apphost]")
{
	// a frame-scripted run is machine-independent: every frame advances
	// simulated time by the SAME tick, so frame N always shows the same world
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(0.0001f, true),
		WithinAbs(Orkige::AppHost::AUTOMATED_FRAME_DELTA, 1e-6f));
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(0.016f, true),
		WithinAbs(Orkige::AppHost::AUTOMATED_FRAME_DELTA, 1e-6f));
	// a host SLOWER than the tick gets the tick too - the measured dt never
	// reaches the simulation, so a loaded machine's frames stay in step with a
	// fast one's (a floor-only policy would let 0.05 and 0.09 through here and
	// drift two frame-paced captures apart in simulated time)
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(0.05f, true),
		WithinAbs(Orkige::AppHost::AUTOMATED_FRAME_DELTA, 1e-6f));
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(0.09f, true),
		WithinAbs(Orkige::AppHost::AUTOMATED_FRAME_DELTA, 1e-6f));
	// two frames measured wildly apart still advance the world equally
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(0.002f, true),
		WithinAbs(Orkige::AppHost::clampFrameDelta(0.08f, true), 1e-6f));
}

TEST_CASE("clampFrameDelta keeps human runs real-time", "[apphost]")
{
	// no 1/60 floor: flooring made gameplay run FASTER than real time
	// whenever rendering beat 60 fps
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(0.004f, false),
		WithinAbs(0.004f, 1e-6f));
	// only a tiny positive floor guards against a zero/negative clock read
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(0.0f, false),
		WithinAbs(0.0001f, 1e-6f));
}

TEST_CASE("clampFrameDelta absorbs a stall on both run kinds", "[apphost]")
{
	// a human run caps at 0.1s - the catch-up spiral guard
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(3.0f, false),
		WithinAbs(0.1f, 1e-6f));
	// an automated run needs no cap: the fixed tick already ignores the stall
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(3.0f, true),
		WithinAbs(Orkige::AppHost::AUTOMATED_FRAME_DELTA, 1e-6f));
}

TEST_CASE("AppHostConfig defaults describe the standard host", "[apphost]")
{
	const Orkige::AppHostConfig config;
	CHECK(config.windowWidth == 1280);
	CHECK(config.windowHeight == 720);
	CHECK_FALSE(config.resizableWindow);
	// a human run by default: hosts opt INTO the vsync-free automated mode
	CHECK_FALSE(config.automatedRun);
	CHECK(config.classicMediaDir.empty());
	CHECK(config.hlmsMediaDir.empty());
	CHECK(config.createWindowCamera);
	CHECK(config.createCubeMesh);
}

namespace
{
	Orkige::Event makeKeyEvent(Orkige::KeyEventData::KeyCode key)
	{
		Orkige::optr<Orkige::KeyEventData> data =
			Orkige::onew(new Orkige::KeyEventData());
		data->key = key;
		Orkige::Event event("apphost.test.keyPressed");
		event.setData(data);
		return event;
	}
}

TEST_CASE("QuitOnEscape quits on ESC and ignores other keys", "[apphost]")
{
	Orkige::QuitOnEscape quitOnEscape;
	quitOnEscape.onKeyPressed(makeKeyEvent(Orkige::KeyEventData::KC_SPACE));
	CHECK_FALSE(quitOnEscape.quitRequested);
	quitOnEscape.onKeyPressed(makeKeyEvent(Orkige::KeyEventData::KC_ESCAPE));
	CHECK(quitOnEscape.quitRequested);
}

TEST_CASE("QuitOnEscape lets an intercept consume the press", "[apphost]")
{
	// the editor idiom: the first ESC clears the selection, the second quits
	Orkige::QuitOnEscape quitOnEscape;
	bool selectionExists = true;
	quitOnEscape.intercept = [&selectionExists]()
	{
		if (selectionExists)
		{
			selectionExists = false;
			return true;	// consumed
		}
		return false;
	};
	quitOnEscape.onKeyPressed(makeKeyEvent(Orkige::KeyEventData::KC_ESCAPE));
	CHECK_FALSE(quitOnEscape.quitRequested);
	CHECK_FALSE(selectionExists);
	quitOnEscape.onKeyPressed(makeKeyEvent(Orkige::KeyEventData::KC_ESCAPE));
	CHECK(quitOnEscape.quitRequested);
}
