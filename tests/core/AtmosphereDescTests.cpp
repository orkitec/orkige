/**************************************************************
	created:	2026/07/12 at 20:00
	filename: 	AtmosphereDescTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless sky/fog atmosphere policy: the neutral default is a disabled,
	fog-free desc; the named looks fill a coherent enabled description
	(night is dim, sunset is warm + hazy, day is bright blue); and the
	look-word dialect round-trips. The rendered proof (atmosphere on
	changes background pixels, fog dims a distant object) is the
	render_facade_selfcheck atmosphere leg.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_util/AtmosphereDesc.h>

using namespace Orkige;

TEST_CASE("AtmosphereDesc: the default is disabled and fog-free",
	"[atmosphere]")
{
	const AtmosphereDesc desc;
	CHECK_FALSE(desc.enabled);
	CHECK(desc.fogDensity == 0.0f);
	// a sane daytime sky sits ready for an enabled-only desc
	CHECK(desc.skyBlue > desc.skyRed);
	CHECK(desc.skyPower > 0.0f);
	CHECK(desc.density > 0.0f);
}

TEST_CASE("AtmosphereDesc: named looks are enabled and coherent",
	"[atmosphere]")
{
	// custom just enables the neutral default
	const AtmosphereDesc custom =
		AtmospherePreset::forSky(AtmospherePreset::SKY_CUSTOM);
	CHECK(custom.enabled);
	CHECK(custom.fogDensity == 0.0f);

	// day: bright, blue-dominant, clear
	const AtmosphereDesc day =
		AtmospherePreset::forSky(AtmospherePreset::SKY_DAY);
	CHECK(day.enabled);
	CHECK(day.skyBlue > day.skyRed);
	CHECK(day.skyPower >= 1.0f);

	// sunset: warm (red-dominant) and hazy
	const AtmosphereDesc sunset =
		AtmospherePreset::forSky(AtmospherePreset::SKY_SUNSET);
	CHECK(sunset.enabled);
	CHECK(sunset.skyRed > sunset.skyBlue);
	CHECK(sunset.density > day.density);
	CHECK(sunset.fogDensity > 0.0f);

	// night: dim (low sky power, dark tint)
	const AtmosphereDesc night =
		AtmospherePreset::forSky(AtmospherePreset::SKY_NIGHT);
	CHECK(night.enabled);
	CHECK(night.skyPower < day.skyPower);
	CHECK(night.skyBlue < day.skyBlue);
	CHECK(night.fogDensity > 0.0f);
}

TEST_CASE("AtmosphereDesc: look words round-trip (the config dialect)",
	"[atmosphere]")
{
	const AtmospherePreset::Sky looks[] = { AtmospherePreset::SKY_CUSTOM,
		AtmospherePreset::SKY_DAY, AtmospherePreset::SKY_SUNSET,
		AtmospherePreset::SKY_NIGHT };
	for(AtmospherePreset::Sky sky : looks)
	{
		AtmospherePreset::Sky parsed = AtmospherePreset::SKY_CUSTOM;
		REQUIRE(AtmospherePreset::parseSky(
			AtmospherePreset::skyName(sky), parsed));
		CHECK(parsed == sky);
	}
	// case-insensitive (console/manifest typing)
	AtmospherePreset::Sky parsed = AtmospherePreset::SKY_CUSTOM;
	REQUIRE(AtmospherePreset::parseSky("SUNSET", parsed));
	CHECK(parsed == AtmospherePreset::SKY_SUNSET);
	REQUIRE(AtmospherePreset::parseSky("Night", parsed));
	CHECK(parsed == AtmospherePreset::SKY_NIGHT);
	// garbage is refused and leaves the out-value alone
	parsed = AtmospherePreset::SKY_DAY;
	CHECK_FALSE(AtmospherePreset::parseSky("storm", parsed));
	CHECK_FALSE(AtmospherePreset::parseSky("", parsed));
	CHECK(parsed == AtmospherePreset::SKY_DAY);
}
