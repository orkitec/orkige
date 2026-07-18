/**************************************************************
	created:	2026/07/18 at 09:30
	filename: 	IblPresetTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless image-based-lighting quality policy: the quality knob's
	chain-resolution budget grows monotonically, the mip-skip derivation
	fits any source cubemap under the tier cap without consuming the
	chain, and the cvar word dialect round-trips. The rendered proof
	(a reflective surface taking the cubemap's colour signature, off ==
	baseline) is the render_facade_selfcheck image-lighting leg.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_util/IblPreset.h>

using namespace Orkige;

TEST_CASE("IblPreset: off means a zero budget", "[iblpreset]")
{
	const IblPreset::Settings off = IblPreset::forQuality(IblPreset::IQ_OFF);
	CHECK(off.chainResolution == 0u);
}

TEST_CASE("IblPreset: budgets grow monotonically with the knob",
	"[iblpreset]")
{
	const IblPreset::Quality steps[] = { IblPreset::IQ_LOW,
		IblPreset::IQ_MEDIUM, IblPreset::IQ_HIGH };
	unsigned int previous = 0u;
	for(IblPreset::Quality step : steps)
	{
		const IblPreset::Settings settings = IblPreset::forQuality(step);
		CHECK(settings.chainResolution > previous);
		previous = settings.chainResolution;
	}
}

TEST_CASE("IblPreset: the mip skip fits a source under the tier cap",
	"[iblpreset]")
{
	const IblPreset::Settings medium =
		IblPreset::forQuality(IblPreset::IQ_MEDIUM);	// 64-texel cap

	// already within the cap: nothing to drop
	CHECK(IblPreset::mipSkipForSource(64u, medium) == 0u);
	CHECK(IblPreset::mipSkipForSource(16u, medium) == 0u);

	// each dropped mip halves the edge until it fits
	CHECK(IblPreset::mipSkipForSource(128u, medium) == 1u);
	CHECK(IblPreset::mipSkipForSource(256u, medium) == 2u);
	CHECK(IblPreset::mipSkipForSource(1024u, medium) == 4u);

	// the stock 128-texel skies per tier: low halves twice, high keeps all
	CHECK(IblPreset::mipSkipForSource(128u,
		IblPreset::forQuality(IblPreset::IQ_LOW)) == 2u);
	CHECK(IblPreset::mipSkipForSource(128u,
		IblPreset::forQuality(IblPreset::IQ_HIGH)) == 0u);
}

TEST_CASE("IblPreset: the mip skip never consumes the whole chain",
	"[iblpreset]")
{
	// degenerate inputs answer zero instead of over-skipping
	const IblPreset::Settings off = IblPreset::forQuality(IblPreset::IQ_OFF);
	CHECK(IblPreset::mipSkipForSource(512u, off) == 0u);
	CHECK(IblPreset::mipSkipForSource(0u,
		IblPreset::forQuality(IblPreset::IQ_MEDIUM)) == 0u);

	// a tiny cap against a big source still leaves the 1-texel tail: the
	// skip halves down to 1 and stops
	IblPreset::Settings tiny = { 1u };
	CHECK(IblPreset::mipSkipForSource(256u, tiny) == 8u);	// 256 -> 1
}

TEST_CASE("IblPreset: the knob word dialect round-trips", "[iblpreset]")
{
	const IblPreset::Quality steps[] = { IblPreset::IQ_OFF, IblPreset::IQ_LOW,
		IblPreset::IQ_MEDIUM, IblPreset::IQ_HIGH };
	for(IblPreset::Quality step : steps)
	{
		IblPreset::Quality parsed = IblPreset::IQ_HIGH;
		CHECK(IblPreset::parseQuality(IblPreset::qualityName(step), parsed));
		CHECK(parsed == step);
	}
	// case-insensitive, unknown words refuse without touching the out param
	IblPreset::Quality parsed = IblPreset::IQ_MEDIUM;
	CHECK(IblPreset::parseQuality("HIGH", parsed));
	CHECK(parsed == IblPreset::IQ_HIGH);
	CHECK_FALSE(IblPreset::parseQuality("ultra", parsed));
	CHECK(parsed == IblPreset::IQ_HIGH);
}
