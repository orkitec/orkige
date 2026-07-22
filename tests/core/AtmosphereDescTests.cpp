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
#include <catch2/catch_approx.hpp>

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
	// the un-tonemapped exposure is safe below the native PI sun drive (which
	// clips lit surfaces to white) yet bright enough to light the scene
	CHECK(desc.sunPower > 0.0f);
	CHECK(desc.sunPower < 3.14159f);
	CHECK(desc.ambientPower > 0.0f);
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

	// sunset: the same blue-dominant Rayleigh tint as day (the tint is an
	// ABSORPTION spectrum in the sky model - a warm-authored tint renders an
	// inverted cyan sunset), distinguished by the THICK haze: the warm look
	// comes from density + a low sun
	const AtmosphereDesc sunset =
		AtmospherePreset::forSky(AtmospherePreset::SKY_SUNSET);
	CHECK(sunset.enabled);
	CHECK(sunset.skyBlue > sunset.skyRed);
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

TEST_CASE("AtmosphereDesc: blend interpolates between tested looks",
	"[atmosphere]")
{
	const AtmosphereDesc day = AtmospherePreset::forSky(AtmospherePreset::SKY_DAY);
	const AtmosphereDesc night =
		AtmospherePreset::forSky(AtmospherePreset::SKY_NIGHT);

	// the endpoints ARE the named looks (and are enabled)
	const AtmosphereDesc atStart =
		AtmospherePreset::blend(AtmospherePreset::SKY_DAY,
			AtmospherePreset::SKY_NIGHT, 0.0f);
	CHECK(atStart.enabled);
	CHECK(atStart.sunPower == day.sunPower);
	CHECK(atStart.skyBlue == day.skyBlue);
	const AtmosphereDesc atEnd =
		AtmospherePreset::blend(AtmospherePreset::SKY_DAY,
			AtmospherePreset::SKY_NIGHT, 1.0f);
	CHECK(atEnd.sunPower == Catch::Approx(night.sunPower));

	// a mid blend lands strictly between the endpoints (day is brighter)
	const AtmosphereDesc mid =
		AtmospherePreset::blend(AtmospherePreset::SKY_DAY,
			AtmospherePreset::SKY_NIGHT, 0.5f);
	CHECK(mid.sunPower < day.sunPower);
	CHECK(mid.sunPower > night.sunPower);
	CHECK(mid.skyPower < day.skyPower);
	CHECK(mid.skyPower > night.skyPower);

	// t is clamped to [0;1]
	CHECK(AtmospherePreset::blend(AtmospherePreset::SKY_DAY,
		AtmospherePreset::SKY_NIGHT, -1.0f).sunPower == day.sunPower);
	CHECK(AtmospherePreset::blend(AtmospherePreset::SKY_DAY,
		AtmospherePreset::SKY_NIGHT, 2.0f).sunPower ==
		Catch::Approx(night.sunPower));
}

TEST_CASE("AtmosphereDesc: the sky type defaults procedural and presets keep it",
	"[atmosphere]")
{
	// existing content never names a sky type - the default IS today's look
	const AtmosphereDesc desc;
	CHECK(desc.skyType == AtmosphereSky::ST_PROCEDURAL);
	CHECK(desc.skyboxTexture.empty());

	// the named looks author the procedural sky: forSky/blend leave the sky
	// type + skybox ref at their defaults (the Engine wrappers carry a chosen
	// type across preset calls)
	const AtmosphereDesc sunset =
		AtmospherePreset::forSky(AtmospherePreset::SKY_SUNSET);
	CHECK(sunset.skyType == AtmosphereSky::ST_PROCEDURAL);
	CHECK(sunset.skyboxTexture.empty());
	const AtmosphereDesc mid =
		AtmospherePreset::blend(AtmospherePreset::SKY_DAY,
			AtmospherePreset::SKY_NIGHT, 0.5f);
	CHECK(mid.skyType == AtmosphereSky::ST_PROCEDURAL);
	CHECK(mid.skyboxTexture.empty());
}

TEST_CASE("AtmosphereDesc: sky type words round-trip (the Lua dialect)",
	"[atmosphere]")
{
	const AtmosphereSky::Type types[] = { AtmosphereSky::ST_PROCEDURAL,
		AtmosphereSky::ST_SKYBOX, AtmosphereSky::ST_COLOUR };
	for(AtmosphereSky::Type type : types)
	{
		AtmosphereSky::Type parsed = AtmosphereSky::ST_PROCEDURAL;
		REQUIRE(AtmosphereSky::parseType(
			AtmosphereSky::typeName(type), parsed));
		CHECK(parsed == type);
	}
	// case-insensitive + the American spelling
	AtmosphereSky::Type parsed = AtmosphereSky::ST_PROCEDURAL;
	REQUIRE(AtmosphereSky::parseType("SkyBox", parsed));
	CHECK(parsed == AtmosphereSky::ST_SKYBOX);
	REQUIRE(AtmosphereSky::parseType("color", parsed));
	CHECK(parsed == AtmosphereSky::ST_COLOUR);
	// garbage is refused and leaves the out-value alone
	parsed = AtmosphereSky::ST_SKYBOX;
	CHECK_FALSE(AtmosphereSky::parseType("gradient", parsed));
	CHECK_FALSE(AtmosphereSky::parseType("", parsed));
	CHECK(parsed == AtmosphereSky::ST_SKYBOX);
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
