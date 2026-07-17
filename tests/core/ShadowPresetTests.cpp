/**************************************************************
	created:	2026/07/12 at 16:00
	filename: 	ShadowPresetTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless shadow quality policy: the quality knob's budgets (cascade
	count, shadow map resolutions, filter width, draw distance) grow
	monotonically, the atlas layout keeps every cascade disjoint and
	inside the atlas bounds, and the cvar word dialect round-trips.
	The rendered proof (a caster darkening a plane, off == baseline)
	is the render_facade_selfcheck shadow leg.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_util/ShadowPreset.h>

using namespace Orkige;

TEST_CASE("ShadowPreset: off means zero budgets", "[shadowpreset]")
{
	const ShadowPreset::Settings off =
		ShadowPreset::forQuality(ShadowPreset::SQ_OFF);
	CHECK(off.splitCount == 0);
	CHECK(off.baseResolution == 0u);
	CHECK(off.filterTaps == 0);
	CHECK(off.maxDistance == 0.0f);
	unsigned int width = 99u, height = 99u;
	ShadowPreset::atlasSize(off, width, height);
	CHECK(width == 0u);
	CHECK(height == 0u);
}

TEST_CASE("ShadowPreset: budgets grow monotonically with the knob",
	"[shadowpreset]")
{
	const ShadowPreset::Quality steps[] = { ShadowPreset::SQ_LOW,
		ShadowPreset::SQ_MEDIUM, ShadowPreset::SQ_HIGH };
	ShadowPreset::Settings previous = ShadowPreset::forQuality(steps[0]);
	// the GLES2/web floor is ONE focused map (no cascades) on a real texture
	// with a hard-clamped, near-world-only reach - the cheapest configuration
	// that still shadows (@see ShadowPreset remarks)
	CHECK(previous.splitCount == 1);
	CHECK(previous.baseResolution >= 512u);
	CHECK(previous.filterTaps >= 1);
	CHECK(previous.maxDistance > 0.0f);
	CHECK(previous.maxDistance <= 40.0f);
	for(int each = 1; each < 3; ++each)
	{
		const ShadowPreset::Settings current =
			ShadowPreset::forQuality(steps[each]);
		CHECK(current.splitCount >= previous.splitCount);
		CHECK(current.baseResolution >= previous.baseResolution);
		CHECK(current.filterTaps >= previous.filterTaps);
		CHECK(current.maxDistance >= previous.maxDistance);
		previous = current;
	}
	// PSSM's split count contract (Ogre-Next accepts 2..4 splits)
	CHECK(previous.splitCount <= 4);
}

TEST_CASE("ShadowPreset: the default (medium) stays a phone budget",
	"[shadowpreset]")
{
	// the mobile bar the package documents: at most 2 cascades on at most a
	// 1024-texel base map (~6 MB depth atlas), a low-tap filter
	const ShadowPreset::Settings medium =
		ShadowPreset::forQuality(ShadowPreset::SQ_MEDIUM);
	CHECK(medium.splitCount <= 2);
	CHECK(medium.baseResolution <= 1024u);
	CHECK(medium.filterTaps <= 3);
	unsigned int width = 0u, height = 0u;
	ShadowPreset::atlasSize(medium, width, height);
	// depth atlas bytes (32-bit depth) stay well under 8 MB
	CHECK(static_cast<unsigned long long>(width) * height * 4ull
		<= 8ull * 1024ull * 1024ull);
}

TEST_CASE("ShadowPreset: atlas layout is disjoint and in bounds",
	"[shadowpreset]")
{
	const ShadowPreset::Quality steps[] = { ShadowPreset::SQ_LOW,
		ShadowPreset::SQ_MEDIUM, ShadowPreset::SQ_HIGH };
	for(ShadowPreset::Quality quality : steps)
	{
		const ShadowPreset::Settings settings =
			ShadowPreset::forQuality(quality);
		unsigned int atlasWidth = 0u, atlasHeight = 0u;
		ShadowPreset::atlasSize(settings, atlasWidth, atlasHeight);
		REQUIRE(atlasWidth > 0u);
		REQUIRE(atlasHeight > 0u);
		for(int split = 0; split < settings.splitCount; ++split)
		{
			unsigned int x = 0u, y = 0u;
			ShadowPreset::splitAtlasOffset(settings, split, x, y);
			const unsigned int edge =
				ShadowPreset::splitResolution(settings, split);
			REQUIRE(edge > 0u);
			// inside the atlas
			CHECK(x + edge <= atlasWidth);
			CHECK(y + edge <= atlasHeight);
			// disjoint from every earlier cascade (axis-aligned overlap test)
			for(int other = 0; other < split; ++other)
			{
				unsigned int otherX = 0u, otherY = 0u;
				ShadowPreset::splitAtlasOffset(settings, other, otherX, otherY);
				const unsigned int otherEdge =
					ShadowPreset::splitResolution(settings, other);
				const bool overlaps = x < otherX + otherEdge &&
					otherX < x + edge && y < otherY + otherEdge &&
					otherY < y + edge;
				CHECK_FALSE(overlaps);
			}
		}
	}
}

TEST_CASE("ShadowPreset: knob words round-trip (the cvar dialect)",
	"[shadowpreset]")
{
	const ShadowPreset::Quality steps[] = { ShadowPreset::SQ_OFF,
		ShadowPreset::SQ_LOW, ShadowPreset::SQ_MEDIUM, ShadowPreset::SQ_HIGH };
	for(ShadowPreset::Quality quality : steps)
	{
		ShadowPreset::Quality parsed = ShadowPreset::SQ_OFF;
		REQUIRE(ShadowPreset::parseQuality(
			ShadowPreset::qualityName(quality), parsed));
		CHECK(parsed == quality);
	}
	// case-insensitive (console typing)
	ShadowPreset::Quality parsed = ShadowPreset::SQ_OFF;
	REQUIRE(ShadowPreset::parseQuality("MEDIUM", parsed));
	CHECK(parsed == ShadowPreset::SQ_MEDIUM);
	REQUIRE(ShadowPreset::parseQuality("High", parsed));
	CHECK(parsed == ShadowPreset::SQ_HIGH);
	// garbage is refused and leaves the out-value alone
	parsed = ShadowPreset::SQ_LOW;
	CHECK_FALSE(ShadowPreset::parseQuality("ultra", parsed));
	CHECK_FALSE(ShadowPreset::parseQuality("", parsed));
	CHECK(parsed == ShadowPreset::SQ_LOW);
}
