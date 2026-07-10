/**************************************************************
	created:	2026/07/11 at 12:00
	filename: 	ScreenFadeTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit coverage for the full-screen fade transition: the pure
	alpha ramp (alphaAt endpoints + monotonicity) and the phase state machine
	driven through update() (Out -> Hold -> In -> Idle, the at-opaque callback
	firing exactly once at full opacity). No window - update() draws nothing
	without a render system, but the phase/alpha bookkeeping runs.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine_graphic/ScreenFade.h"

using namespace Orkige;
using Catch::Matchers::WithinAbs;
using Phase = ScreenFade::Phase;

TEST_CASE("ScreenFade alpha ramp endpoints", "[unit][screenfade]")
{
	CHECK_THAT(ScreenFade::alphaAt(Phase::Out, 0.0f, 1.0f), WithinAbs(0.0f, 1e-5f));
	CHECK_THAT(ScreenFade::alphaAt(Phase::Out, 1.0f, 1.0f), WithinAbs(1.0f, 1e-5f));
	CHECK_THAT(ScreenFade::alphaAt(Phase::In, 0.0f, 1.0f), WithinAbs(1.0f, 1e-5f));
	CHECK_THAT(ScreenFade::alphaAt(Phase::In, 1.0f, 1.0f), WithinAbs(0.0f, 1e-5f));
	CHECK(ScreenFade::alphaAt(Phase::Hold, 0.5f, 1.0f) == 1.0f);
	CHECK(ScreenFade::alphaAt(Phase::Idle, 0.5f, 1.0f) == 0.0f);
	// a zero/negative duration is instantaneous (fully at the phase's endpoint)
	CHECK(ScreenFade::alphaAt(Phase::Out, 0.0f, 0.0f) == 1.0f);
	CHECK(ScreenFade::alphaAt(Phase::In, 0.0f, 0.0f) == 0.0f);
}

TEST_CASE("ScreenFade alpha ramp is monotonic", "[unit][screenfade]")
{
	float previousOut = -1.0f;
	float previousIn = 2.0f;
	for (int step = 0; step <= 10; ++step)
	{
		const float elapsed = static_cast<float>(step) * 0.1f;
		const float out = ScreenFade::alphaAt(Phase::Out, elapsed, 1.0f);
		const float in = ScreenFade::alphaAt(Phase::In, elapsed, 1.0f);
		CHECK(out >= previousOut);	// fade-out climbs
		CHECK(in <= previousIn);	// fade-in falls
		previousOut = out;
		previousIn = in;
	}
}

TEST_CASE("ScreenFade phase machine ramps to opaque and back", "[unit][screenfade]")
{
	ScreenFade fade;
	CHECK(fade.getPhase() == Phase::Idle);
	CHECK_FALSE(fade.isFading());

	fade.fadeOut(0.5f);
	CHECK(fade.isFading());
	// advance well past the duration; alpha ends at 1 and the phase holds opaque
	for (int i = 0; i < 20; ++i)
	{
		fade.update(0.1f);
	}
	CHECK_THAT(fade.getAlpha(), WithinAbs(1.0f, 1e-4f));
	CHECK(fade.getPhase() == Phase::Hold);

	fade.fadeIn(0.5f);
	for (int i = 0; i < 20; ++i)
	{
		fade.update(0.1f);
	}
	CHECK_THAT(fade.getAlpha(), WithinAbs(0.0f, 1e-4f));
	CHECK(fade.getPhase() == Phase::Idle);
	CHECK_FALSE(fade.isFading());
}

TEST_CASE("ScreenFade transition fires the at-opaque hook once, at full opacity",
	"[unit][screenfade]")
{
	ScreenFade fade;
	int fired = 0;
	float alphaAtFire = -1.0f;
	fade.transition(0.4f, 0.4f, [&]()
	{
		++fired;
		alphaAtFire = fade.getAlpha();
	});

	// run the whole wipe: out -> (hook) -> hold -> in -> idle
	for (int i = 0; i < 60 && fade.isFading(); ++i)
	{
		fade.update(0.05f);
	}
	CHECK(fired == 1);						// exactly once
	CHECK_THAT(alphaAtFire, WithinAbs(1.0f, 1e-4f));	// while the screen is opaque
	CHECK(fade.getPhase() == Phase::Idle);	// and it auto-faded back in
	CHECK_THAT(fade.getAlpha(), WithinAbs(0.0f, 1e-4f));
}
