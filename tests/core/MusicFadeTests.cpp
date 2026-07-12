/**************************************************************
	created:	2026/07/12 at 16:00
	filename: 	MusicFadeTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless music-crossfade gain math: endpoints hand the swap off cleanly
	(t=0 all outgoing, t=1 all incoming), the fade is monotone, and it is
	equal-power (outGain^2 + inGain^2 == 1) so the perceived loudness stays
	flat through the swap. The rendered proof (music.crossFade switching
	tracks through the real loop) is the player_gameplay_selfcheck run.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <core_util/MusicFade.h>

using Catch::Approx;
using namespace Orkige::MusicFade;

TEST_CASE("MusicFade endpoints hand the swap off cleanly", "[unit][music]")
{
	float outGain = -1.0f;
	float inGain = -1.0f;
	crossfadeGains(0.0f, outGain, inGain);
	REQUIRE(outGain == Approx(1.0f));	// fully the outgoing track
	REQUIRE(inGain == Approx(0.0f));
	crossfadeGains(1.0f, outGain, inGain);
	REQUIRE(outGain == Approx(0.0f));	// fully the incoming track
	REQUIRE(inGain == Approx(1.0f));
}

TEST_CASE("MusicFade is monotone across the fade", "[unit][music]")
{
	float previousOut = 2.0f;
	float previousIn = -1.0f;
	for(int i = 0; i <= 10; ++i)
	{
		const float t = static_cast<float>(i) / 10.0f;
		float outGain = 0.0f;
		float inGain = 0.0f;
		crossfadeGains(t, outGain, inGain);
		REQUIRE(outGain <= previousOut + 1.0e-6f);	// outgoing falls
		REQUIRE(inGain >= previousIn - 1.0e-6f);	// incoming rises
		previousOut = outGain;
		previousIn = inGain;
	}
}

TEST_CASE("MusicFade is equal-power (constant summed energy)", "[unit][music]")
{
	for(int i = 0; i <= 20; ++i)
	{
		const float t = static_cast<float>(i) / 20.0f;
		float outGain = 0.0f;
		float inGain = 0.0f;
		crossfadeGains(t, outGain, inGain);
		// equal-power: the energies sum to 1 at every point of the fade, so the
		// midpoint does NOT dip the way a linear (1-t) crossfade would
		REQUIRE(outGain * outGain + inGain * inGain == Approx(1.0f));
	}
	// concretely, the midpoint sits above the 0.5 a linear fade would give
	float outMid = 0.0f;
	float inMid = 0.0f;
	crossfadeGains(0.5f, outMid, inMid);
	REQUIRE(outMid > 0.5f);
	REQUIRE(inMid > 0.5f);
}

TEST_CASE("MusicFade clamps out-of-range progress", "[unit][music]")
{
	float outGain = 0.0f;
	float inGain = 0.0f;
	crossfadeGains(-0.5f, outGain, inGain);
	REQUIRE(outGain == Approx(1.0f));
	REQUIRE(inGain == Approx(0.0f));
	crossfadeGains(2.0f, outGain, inGain);
	REQUIRE(outGain == Approx(0.0f));
	REQUIRE(inGain == Approx(1.0f));
}
