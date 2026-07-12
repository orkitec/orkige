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

TEST_CASE("clampFrameDelta floors automated runs at the simulated 1/60 tick",
	"[apphost]")
{
	// headless frames render far faster than 60 fps; the floor keeps
	// frame-scripted selfchecks accumulating simulated time
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(0.0001f, true),
		WithinAbs(1.0f / 60.0f, 1e-6f));
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(0.016f, true),
		WithinAbs(1.0f / 60.0f, 1e-6f));
	// a real dt above the floor passes through
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(0.05f, true),
		WithinAbs(0.05f, 1e-6f));
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

TEST_CASE("clampFrameDelta caps a stall at 0.1s on both run kinds",
	"[apphost]")
{
	// the cap avoids the catch-up spiral after a stall
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(3.0f, true),
		WithinAbs(0.1f, 1e-6f));
	CHECK_THAT(Orkige::AppHost::clampFrameDelta(3.0f, false),
		WithinAbs(0.1f, 1e-6f));
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
