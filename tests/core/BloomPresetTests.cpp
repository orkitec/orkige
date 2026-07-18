/**************************************************************
	created:	2026/07/18 at 09:00
	filename: 	BloomPresetTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless LDR bloom policy: the quality knob's blur budget (downsample
	factor, blur passes) is a sane phone-sized ladder, the cvar word
	dialect round-trips, and the BloomDesc defaults + sanitiser keep the
	LDR threshold honest (below 1.0 or nothing blooms). The rendered proof
	(a bright emissive object brightening its neighbourhood, a bright 2D
	sprite staying crisp, off == baseline) is the render_facade_selfcheck
	bloom leg.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_util/BloomPreset.h>

using namespace Orkige;

TEST_CASE("BloomPreset: off means zero blur budget", "[bloompreset]")
{
	const BloomPreset::Settings off =
		BloomPreset::forQuality(BloomPreset::BQ_OFF);
	CHECK(off.downsampleFactor == 0);
	CHECK(off.blurPasses == 0);
}

TEST_CASE("BloomPreset: the blur budget grows with the knob", "[bloompreset]")
{
	const BloomPreset::Settings low =
		BloomPreset::forQuality(BloomPreset::BQ_LOW);
	const BloomPreset::Settings medium =
		BloomPreset::forQuality(BloomPreset::BQ_MEDIUM);
	const BloomPreset::Settings high =
		BloomPreset::forQuality(BloomPreset::BQ_HIGH);
	// every enabled tier renders into a real (downsampled) buffer with at
	// least one separable blur pass
	CHECK(low.downsampleFactor >= 1);
	CHECK(low.blurPasses >= 1);
	// blur passes never shrink as the tier climbs
	CHECK(medium.blurPasses >= low.blurPasses);
	CHECK(high.blurPasses >= medium.blurPasses);
	// HIGH samples a finer (less downsampled) buffer than the phone tiers
	CHECK(high.downsampleFactor <= medium.downsampleFactor);
	// the GLES2/web floor is the cheapest configuration that still blooms
	CHECK(low.blurPasses == 1);
}

TEST_CASE("BloomPreset: the default (medium) stays a phone budget",
	"[bloompreset]")
{
	// the mobile bar the package documents: a downsampled (quarter-res)
	// bloom buffer and a small pass count
	const BloomPreset::Settings medium =
		BloomPreset::forQuality(BloomPreset::BQ_MEDIUM);
	CHECK(medium.downsampleFactor >= 4);
	CHECK(medium.blurPasses <= 2);
}

TEST_CASE("BloomPreset: knob words round-trip (the cvar dialect)",
	"[bloompreset]")
{
	const BloomPreset::Quality steps[] = { BloomPreset::BQ_OFF,
		BloomPreset::BQ_LOW, BloomPreset::BQ_MEDIUM, BloomPreset::BQ_HIGH };
	for(BloomPreset::Quality quality : steps)
	{
		BloomPreset::Quality parsed = BloomPreset::BQ_OFF;
		REQUIRE(BloomPreset::parseQuality(
			BloomPreset::qualityName(quality), parsed));
		CHECK(parsed == quality);
	}
	// case-insensitive (console typing)
	BloomPreset::Quality parsed = BloomPreset::BQ_OFF;
	REQUIRE(BloomPreset::parseQuality("MEDIUM", parsed));
	CHECK(parsed == BloomPreset::BQ_MEDIUM);
	REQUIRE(BloomPreset::parseQuality("High", parsed));
	CHECK(parsed == BloomPreset::BQ_HIGH);
	// garbage is refused and leaves the out-value alone
	parsed = BloomPreset::BQ_LOW;
	CHECK_FALSE(BloomPreset::parseQuality("ultra", parsed));
	CHECK_FALSE(BloomPreset::parseQuality("", parsed));
	CHECK(parsed == BloomPreset::BQ_LOW);
}

TEST_CASE("BloomDesc: neutral defaults are bloom-off and LDR-honest",
	"[bloompreset]")
{
	const BloomDesc desc;
	CHECK_FALSE(desc.enabled);			// per-scene opt-in, default OFF
	CHECK(desc.threshold < 1.0f);		// a 1.0 threshold blooms nothing in LDR
	CHECK(desc.threshold > 0.0f);
	CHECK(desc.intensity > 0.0f);
}

TEST_CASE("BloomDesc: the sanitiser keeps the LDR threshold honest",
	"[bloompreset]")
{
	// a threshold at/above 1.0 would extract no pixels from the clamped LDR
	// target - the sanitiser pulls it just below 1.0 so an enabled bloom
	// always has something to blur
	BloomDesc tooHigh;
	tooHigh.threshold = 1.0f;
	CHECK(tooHigh.sanitised().threshold < 1.0f);
	BloomDesc wayHigh;
	wayHigh.threshold = 4.0f;
	CHECK(wayHigh.sanitised().threshold < 1.0f);
	// negatives clamp to zero on both knobs
	BloomDesc negative;
	negative.threshold = -0.5f;
	negative.intensity = -2.0f;
	CHECK(negative.sanitised().threshold == 0.0f);
	CHECK(negative.sanitised().intensity == 0.0f);
	// an in-range desc passes through untouched
	BloomDesc ok;
	ok.enabled = true;
	ok.threshold = 0.6f;
	ok.intensity = 1.5f;
	const BloomDesc clean = ok.sanitised();
	CHECK(clean.enabled);
	CHECK(clean.threshold == 0.6f);
	CHECK(clean.intensity == 1.5f);
}
